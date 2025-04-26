#include "request.hpp"

namespace HTTP {

    void parseAcceptHeader(std::unordered_set<std::string>&, std::string&);

    Request::Request(const char* raw, const char* clientIP) {
        this->ipStr = clientIP;

        std::stringstream buffer( raw );
        std::string line;

        // Read verb, path, & protocol version
        std::getline(buffer, line);

        if (line.size() && line.back() == '\r') // remove carriage return
            line.pop_back();

        size_t firstSpaceIndex = line.find(" ");
        size_t secondSpaceIndex = line.find(" ", firstSpaceIndex+1);

        this->methodStr = line.substr(0, firstSpaceIndex);
        this->pathStr = line.substr(firstSpaceIndex + 1, secondSpaceIndex - firstSpaceIndex - 1);
        this->httpVersionStr = line.substr(secondSpaceIndex + 1);

        // Decode URI
        decodeURI(this->pathStr);

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
        if (buffer.tellg() != -1)
            this->body = buffer.str().substr(buffer.tellg());
        else
            this->body.clear();

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
        // Switch on method
        switch (this->method) {
            case METHOD::HEAD:
            case METHOD::GET: {
                if (pathStr.size() == 0 || pathStr[0] != '/') {
                    // If the request allows HTML, return an HTML display
                    response.setHeader("Allow", ALLOWED_METHODS);
                    if (this->isMIMEAccepted("text/html"))
                        response.loadBodyFromErrorDoc(400);
                    break;
                }

                // Lookup file
                File file(pathStr);

                if (!file.exists) {
                    if (this->isMIMEAccepted("text/html")) // If the request allows HTML, return an HTML display
                        response.loadBodyFromErrorDoc(404);
                    break;
                }

                // Check accepted MIMES
                if (!this->isMIMEAccepted(file.MIME)) {
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