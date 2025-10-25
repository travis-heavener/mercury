#include "request.hpp"

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

namespace http {

    void loadEarlyHeaders(headers_map_t& headers, const std::string& raw) {
        // Skip status line
        std::string line;
        size_t startIndex = 0;
        readLine(raw, line, startIndex);

        // Read headers from buffer
        while (readLine(raw, line, startIndex)) {
            if (line.size() <= 1) break; // Parse body

            // Check for invalid line format
            if (line.back() != '\r')
                throw http::Exception();
            line.pop_back();

            // Find first colon-space delimiter
            size_t firstSpaceIndex = line.find(": ");
            if (firstSpaceIndex != std::string::npos) {
                std::string key = line.substr(0, firstSpaceIndex);
                std::string value = line.substr(firstSpaceIndex+2);
                strToUpper(key);
                headers.insert({key, value});
            }
        }
    }

    // Fwd. decs
    void parseAcceptHeader(std::unordered_set<std::string>&, std::string&);
    void parseRangeHeader(std::vector<byte_range_t>&, std::string&);

    Request::Request(headers_map_t& headers, const std::string& raw, std::string clientIP, const bool isHTTPS, const bool isContentTooLarge)
        : headers(headers) {
        this->ipStr = clientIP;
        this->isHTTPS = isHTTPS;
        this->_isContentTooLarge = isContentTooLarge;

        std::string line;
        size_t startIndex = 0;

        // Read verb, path, & protocol version
        readLine(raw, line, startIndex);

        // Check for invalid status line
        if (line.size() == 0 || line.back() != '\r')
            throw http::Exception(); // Force close connection
        line.pop_back();

        const size_t firstSpaceIndex = line.find(" ");
        const size_t secondSpaceIndex = line.find(" ", firstSpaceIndex+1);

        this->methodStr = line.substr(0, firstSpaceIndex);
        this->pathStr = line.substr(firstSpaceIndex + 1, secondSpaceIndex - firstSpaceIndex - 1);

        // Check for HTTP/0.9 unique status line
        if (secondSpaceIndex != std::string::npos) {
            this->httpVersionStr = line.substr(secondSpaceIndex + 1);

            // Prevent explicit HTTP/0.9 version in status line
            if (this->httpVersionStr == "HTTP/0.9")
                this->_hasExplicitHTTP0_9 = true;
        } else {
            this->httpVersionStr = "HTTP/0.9";
        }

        // Replace backslash w/ fwd slash
        std::replace( this->pathStr.begin(), this->pathStr.end(), '\\', '/');
        this->rawPathStr = this->pathStr;

        if (this->rawPathStr.empty() || this->rawPathStr[0] == '?')
            this->_has400Error |= true;

        // Decode URI
        try {
            decodeURI(this->pathStr);
        } catch (std::invalid_argument&) {
            this->_has400Error |= true;
        }

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
        startIndex = raw.find("\r\n\r\n");
        if (startIndex != std::string::npos) {
            this->body = std::move(raw);
            this->body.erase(0, startIndex + 4);
        }

        // Extract accepted MIME types
        if (this->headers.find("ACCEPT") != this->headers.end())
            parseAcceptHeader(acceptedMIMETypes, this->headers["ACCEPT"]);

        // Extract accepted encodings
        if (this->headers.find("ACCEPT-ENCODING") != this->headers.end())
            splitStringUnique(acceptedEncodings, this->headers["ACCEPT-ENCODING"], ',', true);

        // Extract byte ranges
        if (this->headers.find("RANGE") != this->headers.end())
            parseRangeHeader(byteRanges, this->headers["RANGE"]);

        // Determine compression method
        if (this->isEncodingAccepted("zstd"))
            this->compressMethod = COMPRESS_ZSTD;
        else if (this->isHTTPS && this->isEncodingAccepted("br"))
            this->compressMethod = COMPRESS_BROTLI;
        else if (this->isEncodingAccepted("gzip"))
            this->compressMethod = COMPRESS_GZIP;
        else if (this->isEncodingAccepted("deflate"))
            this->compressMethod = COMPRESS_DEFLATE;

        // Verify Host header is present for HTTP/1.1+ (RFC 2616)
        if (this->httpVersionStr != "HTTP/0.9" && this->httpVersionStr != "HTTP/1.0"
            && this->headers.find("HOST") == this->headers.end())
            this->_has400Error |= true;
    }

    std::optional<std::string> Request::getHeader(std::string header) const {
        std::optional<std::string> opt;
        strToUpper(header);
        const auto result = this->headers.find(header);
        if (result != headers.end()) opt.emplace(result->second);
        return opt;
    }

    // This method exists to combine common methods from the version handlers
    bool Request::isInDocumentRoot(Response& response, const std::string& allowedMethods) const {
        // Decode URI after removing query string
        std::string querylessPath = rawPathStr.substr(0, rawPathStr.find('?'));
        decodeURI(querylessPath); // Doesn't need try-catch, already checked in Request constructor

        // Prevent lookups to files outside of the document root
        if (pathStr.size() == 0 || pathStr[0] != '/' || querylessPath.find("..") != std::string::npos) {
            response.setHeader("Allow", allowedMethods);
            setStatusMaybeErrorDoc(response, 400);
            return false;
        }

        // Base case, both are valid
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
                if (!pMatch->showDirectoryIndexes() &&
                    std::regex_match(file.path, pMatch->getPattern())) {
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

    void parseAcceptHeader(std::unordered_set<std::string>& splitVec, std::string& string) {
        std::vector<std::string> splitBuf;
        size_t startIndex = 0;
        for (size_t i = 0; i < string.size(); i++) {
            if (string[i] == ',') {
                std::string substr = string.substr(startIndex, i-startIndex);
                trimString(substr);
                if (substr.size() > 0) splitBuf.push_back(substr);
                startIndex = i+1;
            }
        }

        // Append last snippet
        if (startIndex < string.size()) {
            std::string substr = string.substr(startIndex);
            trimString(substr);
            if (substr.size() > 0) splitBuf.push_back(substr);
        }

        // Format split buffer
        for (std::string& mime : splitBuf)
            splitVec.insert(mime.substr(0, mime.find(';')));
    }

    void parseRangeHeader(std::vector<byte_range_t>& splitVec, std::string& rawHeader) {
        trimString(rawHeader);
        size_t unitStart = rawHeader.find("bytes");
        if (unitStart == std::string::npos) return;

        // Break apart ranges into string pairs
        std::vector<std::string> intermediateVec;
        std::string rawRanges( rawHeader.substr(unitStart + 6) );
        splitString(intermediateVec, rawRanges, ',', true);

        // Parse each range
        std::string startBuf, endBuf;
        for (const std::string& rangePair : intermediateVec) {
            size_t dashIndex = rangePair.find('-');
            if (dashIndex == std::string::npos) {
                splitVec.clear(); return;
            }

            startBuf = rangePair.substr(0, dashIndex);
            endBuf = rangePair.substr(dashIndex+1);
            trimString(startBuf); trimString(endBuf);

            // Parse and create buffer
            if (startBuf.empty() && endBuf.empty()) {
                splitVec.clear(); return;
            }

            size_t startIndex = std::string::npos;
            size_t endIndex = std::string::npos;
            try {
                if (!startBuf.empty())  startIndex = std::stoull(startBuf);
                if (!endBuf.empty())    endIndex =   std::stoull(endBuf);
            } catch (std::invalid_argument&) {
                // Handle invalid range
                ERROR_LOG << "Parse failure for Range header" << std::endl;
                splitVec.clear();
                return;
            }

            // Emplace byte range
            splitVec.emplace_back( byte_range_t(startIndex, endIndex) );
        }
    }

}