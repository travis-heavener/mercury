#include "request.hpp"

namespace HTTP {

    void parseAcceptHeader(std::unordered_set<std::string>&, std::string&);

    Request::Request(const char* raw, const char* clientIP) {
        this->ipStr = clientIP;

        std::stringstream buffer( raw );
        std::string line;

        // Read verb, path, & protocol version
        std::getline(buffer, line);

        // Check for invalid status line
        if (line.size() == 0 || line.back() != '\r')
            throw http::Exception();
        line.pop_back();

        size_t firstSpaceIndex = line.find(" ");
        size_t secondSpaceIndex = line.find(" ", firstSpaceIndex+1);

        this->methodStr = line.substr(0, firstSpaceIndex);
        this->pathStr = line.substr(firstSpaceIndex + 1, secondSpaceIndex - firstSpaceIndex - 1);
        this->httpVersionStr = line.substr(secondSpaceIndex + 1);

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
        else
            this->method = METHOD::UNKNOWN;

        // Read headers from buffer
        while (std::getline(buffer, line)) {
            if (line.size() == 0) break; // Parse body

            // Check for invalid line format
            if (line.back() != '\r')
                throw http::Exception();
            line.pop_back();

            // Find first colon-space delimiter
            firstSpaceIndex = line.find(": ");
            if (firstSpaceIndex != std::string::npos) {
                std::string key = line.substr(0, firstSpaceIndex);
                std::string value = line.substr(firstSpaceIndex+2);
                strToUpper(key);
                this->headers.insert({key, value});
            }
        }

        // Read remaining body
        this->body.clear();
        while (std::getline(buffer, line)) {
            // Check for invalid line format
            if (line.back() != '\r')
                throw http::Exception();
            line.pop_back();
            this->body += line;
        }

        // Extract accepted MIME types
        if (headers.find("ACCEPT") != headers.end())
            parseAcceptHeader(acceptedMIMETypes, headers["ACCEPT"]);

        // Extract accepted encodings
        if (headers.find("ACCEPT-ENCODING") != headers.end())
            splitStringUnique(acceptedEncodings, headers["ACCEPT-ENCODING"], ',', true);
    }

    const std::string* Request::getHeader(const std::string& header) const {
        const auto result = this->headers.find(header);
        return (result != this->headers.end()) ? &result->second : nullptr;
    }

    void Request::loadResponse(Response& response) const {
        // Handle invalid HTTP version
        if (this->httpVersionStr != "HTTP/1.1" && this->httpVersionStr != "HTTP/1.0") {
            response.setStatus(505);
            if (this->isMIMEAccepted("text/html"))
                response.loadBodyFromErrorDoc(505);
            return;
        }

        // Decode URI after removing query string
        std::string querylessPath = this->rawPathStr.substr(0, this->rawPathStr.find("?"));
        decodeURI(querylessPath); // Doesn't need try-catch since it's already checked in constructor

        // Prevent lookups to files outside of the document root
        if (this->pathStr.size() == 0 || querylessPath.find("..") != std::string::npos || this->pathStr[0] != '/') {
            response.setHeader("Allow", ALLOWED_METHODS);
            response.setStatus(400);
            if (this->isMIMEAccepted("text/html"))
                response.loadBodyFromErrorDoc(400);
            return;
        }

        // Switch on method
        switch (this->method) {
            case METHOD::HEAD:
            case METHOD::GET: {
                if (pathStr.size() == 0 || pathStr[0] != '/') {
                    // If the request allows HTML, return an HTML display
                    response.setHeader("Allow", ALLOWED_METHODS);
                    response.setStatus(400);
                    if (this->isMIMEAccepted("text/html"))
                        response.loadBodyFromErrorDoc(400);
                    break;
                }

                // Lookup file
                File file(pathStr);

                // Catch early caught IO failure
                if (file.ioFailure) {
                    // Internal server error from IO
                    response.setStatus(500);
                    if (this->isMIMEAccepted("text/html"))
                        response.loadBodyFromErrorDoc(500);
                    break;
                }

                // Verify not a symlink or hardlink
                if (file.isLinked) {
                    // If the request allows HTML, return an HTML display
                    response.setStatus(403);
                    if (this->isMIMEAccepted("text/html"))
                        response.loadBodyFromErrorDoc(403);
                    break;
                }

                // Handle directory listing
                if (file.isDirectory) {
                    bool isDirectoryIndexDisabled = false;
                    for (conf::Match* pMatch : conf::matchConfigs) {
                        if (!pMatch->showDirectoryIndexes() && std::regex_match(file.path, pMatch->getPattern())) {
                            // Hide the directory index
                            response.setStatus(403);
                            if (this->isMIMEAccepted("text/html")) // If the request allows HTML, return an HTML display
                                response.loadBodyFromErrorDoc(403);

                            // Indicate to break outer switch statement
                            isDirectoryIndexDisabled = true;
                            break;
                        }
                    }

                    // Break if status was set
                    if (isDirectoryIndexDisabled) break;
                }

                if (!file.exists) {
                    response.setStatus(404);
                    if (this->isMIMEAccepted("text/html")) // If the request allows HTML, return an HTML display
                        response.loadBodyFromErrorDoc(404);
                    break;
                }

                // Check accepted MIMES
                if (!this->isMIMEAccepted(file.MIME)) {
                    response.setStatus(406);
                    if (this->isMIMEAccepted("text/html")) // If the request allows HTML, return an HTML display
                        response.loadBodyFromErrorDoc(406);
                    break;
                }

                // Check if previously cached
                const auto pLastModTS = this->getHeader("IF-MODIFIED-SINCE");
                if (pLastModTS != nullptr) {
                    // Compare timestamps
                    try {
                        const std::time_t serverTime = getFileModTimeT(file.path);
                        const std::time_t clientTime = getTimeTFromGMT(*pLastModTS);

                        if (serverTime <= clientTime) { // Send 304
                            response.setStatus(304); // Not Modified
                            break;
                        }
                    } catch (...) {
                        // Comparison failed, re-send content as if updated
                    }
                }

                // Attempt to buffer resource
                if (response.loadBodyFromFile(file) == IO_FAILURE) {
                    response.setStatus(500);
                    if (this->isMIMEAccepted("text/html"))
                        response.loadBodyFromErrorDoc(500);
                    break;
                }

                // Otherwise, body loaded successfully
                response.setStatus(200);
                response.setHeader("Content-Type", file.MIME);

                // Load additional headers for body loading
                for (conf::Match* pMatch : conf::matchConfigs) {
                    if (std::regex_match(file.path, pMatch->getPattern())) {
                        // Apply headers
                        for (auto [name, value] : pMatch->getHeaders()) {
                            response.setHeader(name, value);
                        }
                    }
                }
                break;
            }
            case METHOD::OPTIONS: {
                response.setHeader("Allow", ALLOWED_METHODS);
                response.setStatus(200);
                break;
            }
            default: {
                // If the request allows HTML, return an HTML display
                response.setHeader("Allow", ALLOWED_METHODS);
                response.setStatus(405);
                if (this->isMIMEAccepted("text/html"))
                    response.loadBodyFromErrorDoc(405);
                break;
            }
        }
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

        // append last snippet
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