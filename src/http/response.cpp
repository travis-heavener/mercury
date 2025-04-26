#include "response.hpp"

namespace HTTP {

    Response::Response(const std::string& httpVersion) {
        this->httpVersion = httpVersion;
        loadConfHeaders(this->headers);
    }

    void Response::setStatus(const uint16_t statusCode) {
        this->statusCode = statusCode;
    }

    void Response::setHeader(std::string name, const std::string& value) {
        // Force string to be uppercase
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        this->headers.insert({name, value});
    }

    int Response::loadBodyFromErrorDoc(const uint16_t statusCode) {
        this->setStatus(statusCode);
        return loadErrorDoc(statusCode, this->body);
    }

    int Response::loadBodyFromFile(File& file) {
        const int bodyStatus = file.loadToBuffer(this->body);

        // Get last modified GMT string
        if (bodyStatus == IO_SUCCESS)
            this->setHeader("Last-Modified", file.getLastModifiedGMT());

        return bodyStatus;
    }

    int Response::compressBody(const int compressionType) {
        return compressText(this->body, compressionType);
    }

    void Response::setContentType(const std::string& type) {
        this->setHeader("Content-Type", type);
    }

    const std::string Response::getContentType() const {
        auto type = this->headers.find("CONTENT-TYPE");
        if (type == this->headers.end()) return "";
        return type->second;
    }

    void Response::loadToBuffer(std::string& buffer) {
        // Determine Content-Length
        const std::string contentLen = std::to_string(this->body.size());
        this->setHeader("Content-Length", contentLen);

        // Stringify headers
        std::string headers;
        for (auto [name, value] : this->headers)
            headers += name + ':' + value + '\n';

        // Write to buffer
        buffer = httpVersion + ' ' + std::to_string(statusCode) + ' '  + getReasonFromStatusCode(statusCode) + '\n' + headers + '\n' + body;
    }

};