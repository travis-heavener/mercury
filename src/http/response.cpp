#include "response.hpp"

#define CRLF "\r\n"

namespace HTTP {

    Response::Response(const std::string& httpVersion) {
        this->httpVersion = httpVersion;
    }

    void Response::setStatus(const uint16_t statusCode) {
        this->statusCode = statusCode;
    }

    void Response::setHeader(std::string name, const std::string& value) {
        // Update header casing (ex. cOntENt-LeNGTh --> Content-Length)
        formatHeaderCasing(name);
        this->headers.insert({name, value});
    }

    int Response::loadBodyFromErrorDoc(const uint16_t statusCode) {
        this->setStatus(statusCode);
        this->setContentType("text/html");
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
        // Ignore if empty body
        if (this->body.size() == 0) return IO_SUCCESS;

        const int status = compressText(this->body, compressionType);

        if (status == IO_SUCCESS) {
            switch (compressionType) {
                case COMPRESS_BROTLI:
                    this->setHeader("Content-Encoding", "br");
                    break;
                case COMPRESS_GZIP:
                    this->setHeader("Content-Encoding", "gzip");
                    break;
                case COMPRESS_DEFLATE:
                    this->setHeader("Content-Encoding", "deflate");
                    break;
            }
        }

        return status;
    }

    void Response::setContentType(const std::string& type) {
        this->setHeader("Content-Type", type);
    }

    const std::string Response::getContentType() const {
        auto type = this->headers.find("Content-Type");
        if (type == this->headers.end()) return "";
        return type->second;
    }

    void Response::loadToBuffer(std::string& buffer, const bool omitBody) {
        // Determine Content-Length
        const std::string contentLen = std::to_string(this->body.size());
        this->setHeader("Content-Length", contentLen);

        // Load config headers last to overwrite any dupes that have been previously set
        this->setHeader("Server", conf::VERSION);
        this->setHeader("Date", getCurrentGMTString());

        // Stringify headers
        std::string headers;
        for (auto& [name, value] : this->headers)
            headers += name + ':' + value + CRLF;

        // Write to buffer
        buffer = httpVersion + ' ' + std::to_string(statusCode) + ' '  + getReasonFromStatusCode(statusCode) + CRLF + headers + CRLF;

        // Omit the body from HEAD requests
        if (!omitBody) buffer += body;
    }

};