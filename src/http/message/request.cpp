#include "request.hpp"

#include "../../conf/conf.hpp"
#include "../../logs/logger.hpp"
#include "../../util/string_tools.hpp"
#include "../../util/toolbox.hpp"

namespace http {

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

        // Extract accepted encodings
        const auto itEncoding = this->headers.find("ACCEPT-ENCODING");
        if (itEncoding != this->headers.end()) {
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

                    const size_t semi = token.find(';');
                    if (semi != std::string_view::npos)
                        token = token.substr(0, semi);
                    if (!token.empty())
                        this->acceptedEncodings.emplace(token);
                }
            }
        }

        // Extract byte ranges
        const auto itRange = this->headers.find("RANGE");
        if (itRange != this->headers.end())
            parseRangeHeader(byteRanges, itRange->second);

        // Determine compression method
        if (this->isEncodingAccepted("zstd"))
            this->compressMethods |= COMPRESS_ZSTD;
        if (this->isHTTPS && this->isEncodingAccepted("br"))
            this->compressMethods |= COMPRESS_BROTLI;
        if (this->isEncodingAccepted("gzip"))
            this->compressMethods |= COMPRESS_GZIP;
        if (this->isEncodingAccepted("deflate"))
            this->compressMethods |= COMPRESS_DEFLATE;

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
        // Decide what compression type to use of what's available
        if ((this->compressMethods & (COMPRESS_ZSTD | COMPRESS_BROTLI)) == (COMPRESS_ZSTD | COMPRESS_BROTLI)) {
            // Pick Zstandard or Brotli based on MIME
            const std::string mediaType = MIME.substr(0, MIME.find(';'));
            if (mediaType == "text/javascript" || mediaType == "application/json" || mediaType == "text/html" ||
                mediaType == "text/css" || mediaType == "image/svg+xml" || mediaType == "text/plain" ||
                mediaType == "application/wasm" || mediaType == "application/xml" || mediaType == "application/xhtml+xml" ||
                mediaType == "application/ld+json")
                return COMPRESS_BROTLI;

            // Base case, use ZSTD
            return COMPRESS_ZSTD;
        } else if (this->compressMethods & COMPRESS_ZSTD) {
            return COMPRESS_ZSTD;
        } else if (this->compressMethods & COMPRESS_BROTLI) {
            return COMPRESS_BROTLI;
        } else if (this->compressMethods & COMPRESS_GZIP) {
            return COMPRESS_GZIP;
        } else if (this->compressMethods & COMPRESS_DEFLATE) {
            return COMPRESS_DEFLATE;
        }

        // Base case, no compression
        return NO_COMPRESS;
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

        // Handle directory listings
        if (file.isDirectory) {
            for (const std::unique_ptr<conf::Match>& pMatch : conf::matchConfigs) {
                if (!pMatch->showDirectoryIndexes() && pMatch->doesRequestMatch(paths.decodedURI, headers)) {
                    // Hide the directory index
                    this->setStatusMaybeErrorDoc(response, 403);
                    return false;
                }
            }
        }

        // Verify file exists
        if (!file.exists) {
            this->setStatusMaybeErrorDoc(response, 404);
            return false;
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

}