#include "request.hpp"

#include "exception.hpp"
#include "../conf/conf.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

namespace http {

    void loadEarlyHeaders(headers_map_t& headers, const std::string& partialReq) {
        std::stringstream buffer( partialReq );
        std::string line;
        
        // Skip status line
        std::getline(buffer, line);

        // Read headers from buffer
        while (std::getline(buffer, line)) {
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

    void parseAcceptHeader(std::unordered_set<std::string>&, std::string&);

    Request::Request(headers_map_t& headers, const std::string& raw, std::string clientIP, const bool isHTTPS, const bool isContentTooLarge)
        : headers(headers) {
        this->ipStr = clientIP;
        this->isHTTPS = isHTTPS;
        this->_isContentTooLarge = isContentTooLarge;

        std::stringstream buffer( raw );
        std::string line;

        // Read verb, path, & protocol version
        std::getline(buffer, line);

        // Check for invalid status line
        if (line.size() == 0 || line.back() != '\r')
            throw http::Exception(); // Force close connection
        line.pop_back();

        size_t firstSpaceIndex = line.find(" ");
        size_t secondSpaceIndex = line.find(" ", firstSpaceIndex+1);

        this->methodStr = line.substr(0, firstSpaceIndex);
        this->pathStr = line.substr(firstSpaceIndex + 1, secondSpaceIndex - firstSpaceIndex - 1);

        // Check for HTTP/0.9 unique status line
        if (secondSpaceIndex != std::string::npos) {
            this->httpVersionStr = line.substr(secondSpaceIndex + 1);

            // Prevent explicit HTTP/0.9 version in status line
            if (this->httpVersionStr == "HTTP/0.9")
                this->hasExplicitlyDefinedHTTPVersion0_9 = true;
        } else {
            this->httpVersionStr = "HTTP/0.9";
        }

        // Replace backslash w/ fwd slash
        std::replace( this->pathStr.begin(), this->pathStr.end(), '\\', '/');
        this->rawPathStr = this->pathStr;

        // Decode URI
        try {
            decodeURI(this->pathStr);
        } catch (std::invalid_argument&) {
            this->hasBadURI = true;
        }

        // Determine method
        if (this->methodStr == "GET")
            this->method = METHOD::GET;
        else if (this->methodStr == "HEAD")
            this->method = METHOD::HEAD;
        else if (this->methodStr == "OPTIONS")
            this->method = METHOD::OPTIONS;
        else if (this->methodStr == "POST")
            this->method = METHOD::POST;
        else if (this->methodStr == "PUT")
            this->method = METHOD::PUT;
        else if (this->methodStr == "DELETE")
            this->method = METHOD::DEL;
        else if (this->methodStr == "PATCH")
            this->method = METHOD::PATCH;
        else
            this->method = METHOD::UNKNOWN;

        // Skip headers
        buffer.seekg( raw.find("\r\n\r\n") + 4 );

        // Read remaining body
        std::ostringstream oss;
        oss << buffer.rdbuf();
        this->body = oss.str();

        // Extract accepted MIME types
        if (this->headers.find("ACCEPT") != this->headers.end())
            parseAcceptHeader(acceptedMIMETypes, this->headers["ACCEPT"]);

        // Extract accepted encodings
        if (this->headers.find("ACCEPT-ENCODING") != this->headers.end())
            splitStringUnique(acceptedEncodings, this->headers["ACCEPT-ENCODING"], ',', true);

        // Determine compression method
        if (this->isHTTPS && this->isEncodingAccepted("br"))
            this->compressMethod = COMPRESS_BROTLI;
        else if (this->isEncodingAccepted("gzip"))
            this->compressMethod = COMPRESS_GZIP;
        else if (this->isEncodingAccepted("deflate"))
            this->compressMethod = COMPRESS_DEFLATE;
    }

    const std::string* Request::getHeader(std::string header) const {
        strToUpper(header);
        const auto result = this->headers.find(header);
        return (result != this->headers.end()) ? &result->second : nullptr;
    }

    // This method exists to combine common methods from the version handlers
    bool Request::isInDocumentRoot(Response& response, const std::string& allowedMethods) const {
        // Decode URI after removing query string
        std::string querylessPath = rawPathStr.substr(0, rawPathStr.find("?"));
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

}