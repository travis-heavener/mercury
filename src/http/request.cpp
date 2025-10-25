#include "request.hpp"

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

namespace http {

    Request::Request(headers_map_t& headers, const std::string& raw, std::string clientIP, const bool isHTTPS, const bool isContentTooLarge)
        : headers(headers), ipStr(clientIP), isHTTPS(isHTTPS), _isContentTooLarge(isContentTooLarge) {
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

}