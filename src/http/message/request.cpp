#include "request.hpp"

#include "../../conf/conf.hpp"
#include "../../conf/rules/match.hpp"
#include "../../logs/logger.hpp"

namespace http {

    // Parse RFC 9110 qvalue as integer milliquality (0 to 1000)
    static int parseQValue(std::string_view qstr) {
        while (!qstr.empty() && (qstr.front() == ' ' || qstr.front() == '\t'))
            qstr.remove_prefix(1);
        while (!qstr.empty() && (qstr.back() == ' ' || qstr.back() == '\t'))
            qstr.remove_suffix(1);

        if (qstr.empty()) return 1000;

        if (qstr[0] == '0') {
            if (qstr.size() == 1 || qstr[1] != '.') return 0;
            int val = 0;
            int multiplier = 100;
            for (size_t i = 2; i < qstr.size() && i < 5; ++i) {
                if (!std::isdigit(static_cast<unsigned char>(qstr[i]))) break;
                val += (qstr[i] - '0') * multiplier;
                multiplier /= 10;
            }
            return val;
        } else if (qstr[0] == '1') {
            if (qstr.size() == 1 || qstr[1] != '.') return 1000;
            for (size_t i = 2; i < qstr.size() && i < 5; ++i) {
                if (qstr[i] != '0') return 1000;
            }
            return 1000;
        }
        return 1000;
    }

    // Extract qvalue weight parameter from token parameters
    static int extractQValue(std::string_view params) {
        size_t pos = 0;
        while (pos < params.size()) {
            const size_t nextSemi = params.find(';', pos);
            std::string_view param = (nextSemi == std::string_view::npos)
                ? params.substr(pos)
                : params.substr(pos, nextSemi - pos);
            pos = (nextSemi == std::string_view::npos) ? params.size() : nextSemi + 1;

            while (!param.empty() && (param.front() == ' ' || param.front() == '\t'))
                param.remove_prefix(1);
            while (!param.empty() && (param.back() == ' ' || param.back() == '\t'))
                param.remove_suffix(1);

            if (param.size() >= 2 && (param[0] == 'q' || param[0] == 'Q')) {
                std::string_view rest = param.substr(1);
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
                    rest.remove_prefix(1);
                if (!rest.empty() && rest.front() == '=') {
                    rest.remove_prefix(1);
                    return parseQValue(rest);
                }
            }
        }
        return 1000;
    }

    Request::Request(headers_map_t& headers, std::string_view raw, const std::string& clientIP, const bool isHTTPS, const RequestFlags& reqFlags)
        : headers(headers), ipStr(clientIP), isHTTPS(isHTTPS), reqFlags(reqFlags) {
        // Read verb, path, & protocol version
        const size_t lineEnd = raw.find("\r\n");
        if (lineEnd == std::string_view::npos || lineEnd == 0)
            throw http::Exception(); // Force close connection

        const std::string_view line = raw.substr(0, lineEnd);
        const size_t firstSpaceIndex = line.find(' ');
        if (firstSpaceIndex == std::string_view::npos)
            throw http::Exception();

        const size_t secondSpaceIndex = line.find(' ', firstSpaceIndex + 1);

        this->methodStr = std::string(line.substr(0, firstSpaceIndex));

        // Check for HTTP/0.9 unique status line
        if (secondSpaceIndex != std::string_view::npos) {
            this->httpVersionStr = std::string(line.substr(secondSpaceIndex + 1));

            // Prevent explicit HTTP/0.9 version in status line
            if (this->httpVersionStr == "HTTP/0.9")
                this->_hasExplicitHTTP0_9 = true;
        } else {
            this->httpVersionStr = "HTTP/0.9";
        }

        // Parse path logic
        const std::string rawPathFromRequest = (secondSpaceIndex != std::string_view::npos)
            ? std::string(line.substr(firstSpaceIndex + 1, secondSpaceIndex - firstSpaceIndex - 1))
            : std::string(line.substr(firstSpaceIndex + 1));
        this->_has400Error |= !loadRequestPaths(paths, rawPathFromRequest);

        // Determine method
        if      (this->methodStr == "GET")      this->method = METHOD::GET;
        else if (this->methodStr == "HEAD")     this->method = METHOD::HEAD;
        else if (this->methodStr == "OPTIONS")  this->method = METHOD::OPTIONS;
        else if (this->methodStr == "POST")     this->method = METHOD::POST;
        else if (this->methodStr == "PUT")      this->method = METHOD::PUT;
        else if (this->methodStr == "DELETE")   this->method = METHOD::DEL;
        else if (this->methodStr == "PATCH")    this->method = METHOD::PATCH;
        else                                    this->method = METHOD::UNKNOWN;

        // Skip headers & read remaining buffer
        const size_t bodyStart = raw.find("\r\n\r\n");
        if (bodyStart != std::string_view::npos && bodyStart + 4 <= raw.size())
            this->body = std::string(raw.substr(bodyStart + 4));

        // Extract accepted MIME types
        const auto itAccept = this->headers.find("ACCEPT");
        if (itAccept != this->headers.end())
            parseAcceptHeader(acceptedMIMETypes, itAccept->second);

        // Extract accepted encodings with quality values (RFC 9110 Section 12.5.3)
        const auto itEncoding = this->headers.find("ACCEPT-ENCODING");
        if (itEncoding != this->headers.end()) {
            this->hasAcceptEncoding = true;
            int qStar = -1;
            int explicitZstd = -1;
            int explicitBrotli = -1;
            int explicitGzip = -1;
            int explicitDeflate = -1;
            int explicitIdentity = -1;

            std::string_view encView = itEncoding->second;
            size_t startIndex = 0;
            while (startIndex < encView.size()) {
                const size_t comma = encView.find(',', startIndex);
                std::string_view token = (comma == std::string_view::npos)
                    ? encView.substr(startIndex)
                    : encView.substr(startIndex, comma - startIndex);
                startIndex = (comma == std::string_view::npos) ? encView.size() : comma + 1;

                const size_t firstNotSpace = token.find_first_not_of(" \t");
                if (firstNotSpace != std::string_view::npos) {
                    const size_t lastNotSpace = token.find_last_not_of(" \t");
                    token = token.substr(firstNotSpace, lastNotSpace - firstNotSpace + 1);

                    std::string_view coding = token;
                    int q = 1000;
                    const size_t semi = token.find(';');
                    if (semi != std::string_view::npos) {
                        coding = token.substr(0, semi);
                        const size_t codingEnd = coding.find_last_not_of(" \t");
                        if (codingEnd != std::string_view::npos)
                            coding = coding.substr(0, codingEnd + 1);
                        q = extractQValue(token.substr(semi + 1));
                    }

                    if (!coding.empty()) {
                        std::string lowerCoding;
                        lowerCoding.reserve(coding.size());
                        for (char c : coding)
                            lowerCoding.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

                        this->encodingQualities[lowerCoding] = q;

                        if (lowerCoding == "*") {
                            qStar = q;
                        } else if (lowerCoding == "zstd") {
                            explicitZstd = q;
                        } else if (lowerCoding == "br") {
                            explicitBrotli = q;
                        } else if (lowerCoding == "gzip" || lowerCoding == "x-gzip") {
                            explicitGzip = q;
                        } else if (lowerCoding == "deflate") {
                            explicitDeflate = q;
                        } else if (lowerCoding == "identity") {
                            explicitIdentity = q;
                        }

                        if (q > 0)
                            this->acceptedEncodings.emplace(std::move(lowerCoding));
                    }
                }
            }

            this->qZstd = (explicitZstd != -1) ? explicitZstd : (qStar != -1 ? qStar : 0);
            this->qBrotli = (this->isHTTPS && explicitBrotli != -1) ? explicitBrotli : (this->isHTTPS && qStar != -1 ? qStar : 0);
            this->qGzip = (explicitGzip != -1) ? explicitGzip : (qStar != -1 ? qStar : 0);
            this->qDeflate = (explicitDeflate != -1) ? explicitDeflate : (qStar != -1 ? qStar : 0);

            // RFC 9110 Section 12.5.3: identity is acceptable unless explicitly refused (identity;q=0)
            // or refused by wildcard (*;q=0) without explicit non-zero identity
            if (explicitIdentity != -1)
                this->qIdentity = explicitIdentity;
            else if (qStar != -1)
                this->qIdentity = qStar;
            else
                this->qIdentity = 1000;

            if (this->qZstd > 0) {
                this->compressMethods |= COMPRESS_ZSTD;
                this->acceptedEncodings.emplace("zstd");
            }
            if (this->qBrotli > 0) {
                this->compressMethods |= COMPRESS_BROTLI;
                this->acceptedEncodings.emplace("br");
            }
            if (this->qGzip > 0) {
                this->compressMethods |= COMPRESS_GZIP;
                this->acceptedEncodings.emplace("gzip");
            }
            if (this->qDeflate > 0) {
                this->compressMethods |= COMPRESS_DEFLATE;
                this->acceptedEncodings.emplace("deflate");
            }
        }

        // Extract byte ranges
        const auto itRange = this->headers.find("RANGE");
        if (itRange != this->headers.end())
            parseRangeHeader(byteRanges, itRange->second);

        // Verify Host header is present for HTTP/1.1+ (RFC 2616)
        if (this->httpVersionStr != "HTTP/0.9" && this->httpVersionStr != "HTTP/1.0"
            && this->headers.find("HOST") == this->headers.end())
            this->_has400Error |= true;
    }

    std::optional<std::string> Request::getHeader(std::string_view header) const {
        std::string upperKey;
        upperKey.resize(header.size());
        for (size_t i = 0; i < header.size(); ++i)
            upperKey[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(header[i])));
        const auto result = this->headers.find(upperKey);
        if (result != headers.end()) return result->second;
        return std::nullopt;
    }

    int Request::getCompressMethod(const std::string& MIME) const {
        if (this->compressMethods == NO_COMPRESS)
            return NO_COMPRESS;

        // Determine MIME preference between Brotli and Zstandard
        const std::string mediaType = MIME.substr(0, MIME.find(';'));
        const bool preferBrotli = (mediaType == "text/javascript" || mediaType == "application/json" || mediaType == "text/html" ||
            mediaType == "text/css" || mediaType == "image/svg+xml" || mediaType == "text/plain" ||
            mediaType == "application/wasm" || mediaType == "application/xml" || mediaType == "application/xhtml+xml" ||
            mediaType == "application/ld+json");

        // Rank candidate compression algorithms by quality weight and server preference
        struct Candidate {
            int method;
            int q;
            int serverPriority;
        };

        const Candidate candidates[] = {
            { COMPRESS_BROTLI, this->qBrotli, preferBrotli ? 4 : 3 },
            { COMPRESS_ZSTD,   this->qZstd,   preferBrotli ? 3 : 4 },
            { COMPRESS_GZIP,   this->qGzip,   2 },
            { COMPRESS_DEFLATE,this->qDeflate,1 }
        };

        const Candidate* best = nullptr;
        for (const auto& candidate : candidates) {
            if (candidate.q <= 0) continue;
            if (best == nullptr ||
                candidate.q > best->q ||
                (candidate.q == best->q && candidate.serverPriority > best->serverPriority)) {
                best = &candidate;
            }
        }

        return best ? best->method : NO_COMPRESS;
    }

    bool Request::isDNT() const {
        const auto itDnt = this->headers.find("DNT");
        const auto itGpc = this->headers.find("SEC-GPC");
        return (itDnt != this->headers.end() && itDnt->second == "1") ||
               (itGpc != this->headers.end() && itGpc->second == "1");
    }

    void Request::rewriteRawPath(const std::string& newPath) {
        // Redo path logic
        this->_has400Error |= !loadRequestPaths(paths, newPath, true);
        this->matchedRulesEvaluated = false;
        this->matchedRules.clear();
        this->canonicalMatchedRulesEvaluated = false;
        this->canonicalMatchedRules.clear();
    }

    // This method exists to combine common methods from the version handlers
    bool Request::isInDocumentRoot(Response& response, const std::string& allowedMethods) const {
        // Prevent lookups to files outside of the document root
        if (paths.decodedURI.size() == 0 || paths.decodedURI[0] != '/') {
            response.setHeader("Allow", allowedMethods);
            setStatusMaybeErrorDoc(response, 400);
            return false;
        }

        return true;
    }

    // Sets the status and loads an error doc, if applicable
    void Request::setStatusMaybeErrorDoc(Response& response, const int status) const {
        response.setStatus(status);
        if (isMIMEAccepted("text/html"))
            response.loadBodyFromErrorDoc(status);
    }

    bool Request::isFileValid(Response& response, const File& file) const {
        // Catch early caught IO failure
        if (file.ioFailure) {
            ERROR_LOG << "File constructor caught IO failure" << std::endl;
            this->setStatusMaybeErrorDoc(response, 500);
            return false;
        }

        // Verify not a symlink or hardlink
        if (file.isLinked) {
            this->setStatusMaybeErrorDoc(response, 403);
            return false;
        }

#ifdef _WIN32
        const bool pathDiffers = !equalsCi(paths.decodedURI, file.decodedURIWithoutPathInfo);
#else
        const bool pathDiffers = (paths.decodedURI != file.decodedURIWithoutPathInfo);
#endif

        // Handle directory listings: check both requested and canonical rules
        if (file.isDirectory) {
            for (const conf::Match* pMatch : this->getMatchedRules()) {
                if (!pMatch->showDirectoryIndexes()) {
                    // Hide the directory index
                    this->setStatusMaybeErrorDoc(response, 403);
                    return false;
                }
            }

            if (pathDiffers) {
                for (const conf::Match* pMatch : this->getCanonicalMatchedRules(file.decodedURIWithoutPathInfo)) {
                    if (!pMatch->showDirectoryIndexes()) {
                        // Hide the directory index
                        this->setStatusMaybeErrorDoc(response, 403);
                        return false;
                    }
                }
            }
        }

        // Verify file exists
        if (!file.exists) {
            this->setStatusMaybeErrorDoc(response, 404);
            return false;
        }

        // Re-verify access control on resolved canonical path when it differs from request URI
        if (!conf::matchConfigs.empty() && pathDiffers) {
            const auto& canonicalRules = this->getCanonicalMatchedRules(file.decodedURIWithoutPathInfo);
            if (!canonicalRules.empty()) {
                try {
                    conf::SanitizedIP sip(conf::parseSanitizedClientIP(this->getIPStr()));
                    for (const conf::Match* pMatch : canonicalRules) {
                        if (!pMatch->getAccessControl()->isIPAccepted(sip)) {
                            this->setStatusMaybeErrorDoc(response, 403);
                            return false;
                        }
                    }
                } catch (const std::invalid_argument&) {
                    ERROR_LOG << "Invalid IP address while parsing sanitized IP (" << this->getIPStr() << ')' << std::endl;
                    this->setStatusMaybeErrorDoc(response, 500);
                    return false;
                }
            }
        }

        // Base case, file is valid
        return true;
    }

    // Returns true if the MIME type is accepted by the request OR if there aren't any present
    bool Request::isMIMEAccepted(const std::string& MIME) const {
        if (!acceptedMIMETypes.size()) return true;
        return (acceptedMIMETypes.find(MIME) != acceptedMIMETypes.end() || acceptedMIMETypes.find("*/*") != acceptedMIMETypes.end());
    }

    bool Request::isEncodingAccepted(const std::string& encoding) const {
        return acceptedEncodings.find(encoding) != acceptedEncodings.end();
    }

    // Check backend coding against explicit and wildcard preferences
    bool Request::acceptsContentCoding(std::string_view coding) const {
        // Accept all codings when header absent
        if (!hasAcceptEncoding) return true;

        // Normalize coding token
        const size_t first = coding.find_first_not_of(" \t");
        if (first == std::string_view::npos) return false;

        const size_t last = coding.find_last_not_of(" \t");
        std::string name(coding.substr(first, last - first + 1));
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == "identity") return isIdentityAccepted();

        // Prefer explicit coding or gzip alias over wildcard
        auto it = encodingQualities.find(name);
        if (it == encodingQualities.end() && name == "gzip") it = encodingQualities.find("x-gzip");
        if (it == encodingQualities.end() && name == "x-gzip") it = encodingQualities.find("gzip");
        if (it != encodingQualities.end()) return it->second > 0;
        it = encodingQualities.find("*");

        return it != encodingQualities.end() && it->second > 0;
    }

    const std::vector<const conf::Match*>& Request::getMatchedRules() const {
        if (!matchedRulesEvaluated) {
            matchedRulesEvaluated = true;
            if (!conf::matchConfigs.empty()) {
                matchedRules.reserve(conf::matchConfigs.size());
                for (const std::unique_ptr<conf::Match>& pMatch : conf::matchConfigs) {
                    if (pMatch && pMatch->doesRequestMatch(paths.decodedURI, headers))
                        matchedRules.push_back(pMatch.get());
                }
            }
        }
        return matchedRules;
    }

    const std::vector<const conf::Match*>& Request::getCanonicalMatchedRules(const std::string& canonicalURI) const {
#ifdef _WIN32
        if (equalsCi(paths.decodedURI, canonicalURI))
#else
        if (paths.decodedURI == canonicalURI)
#endif
            return this->getMatchedRules();

        if (!canonicalMatchedRulesEvaluated) {
            canonicalMatchedRulesEvaluated = true;
            if (!conf::matchConfigs.empty()) {
                canonicalMatchedRules.reserve(conf::matchConfigs.size());
                for (const std::unique_ptr<conf::Match>& pMatch : conf::matchConfigs) {
                    if (pMatch && pMatch->doesRequestMatch(canonicalURI, headers))
                        canonicalMatchedRules.push_back(pMatch.get());
                }
            }
        }
        return canonicalMatchedRules;
    }

}
