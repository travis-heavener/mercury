#include "request.hpp"

namespace HTTP {

    Request::Request(const char* raw, const char* clientIP) {
        this->ipStr = clientIP;

        std::stringstream buffer( raw );
        std::string line;

        // Read verb, path, & protocol version
        std::getline(buffer, line);

        if (line.size() && line[line.size()-1] == '\r') // remove carriage return
            line.pop_back();

        size_t firstSpaceIndex = line.find(" ");
        size_t secondSpaceIndex = line.find(" ", firstSpaceIndex+1);

        this->methodStr = line.substr(0, firstSpaceIndex);
        this->pathStr = line.substr(firstSpaceIndex + 1, secondSpaceIndex - firstSpaceIndex - 1);
        this->httpVersionStr = line.substr(secondSpaceIndex + 1);

        // Format path
        if (this->pathStr.back() == '/')
            this->pathStr += "index.html";

        // Determine method
        if (this->methodStr == "GET") this->method = METHOD::GET;

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
            this->body = "";

        // Extract accepted MIME types
        if (headers.find("ACCEPT") != headers.end())
            splitStringUnique(acceptedMIMETypes, headers["ACCEPT"], ',', true);

        // Extract accepted encodings
        if (headers.find("ACCEPT-ENCODING") != headers.end())
            splitStringUnique(acceptedEncodings, headers["ACCEPT-ENCODING"], ',', true);
    }

    const std::string* Request::getHeader(const std::string& header) const {
        const auto result = this->headers.find(header);
        return (result != this->headers.end()) ? &result->second : nullptr;
    }

    // Returns true if the MIME type is accepted by the request OR if there aren't any present
    bool Request::isMIMEAccepted(const std::string& MIME) const {
        if (!acceptedMIMETypes.size()) return true;
        return acceptedMIMETypes.find(MIME) != acceptedMIMETypes.end();
    }

    bool Request::isEncodingAccepted(const std::string& encoding) const {
        return acceptedEncodings.find(encoding) != acceptedEncodings.end();
    }

}