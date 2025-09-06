#include "response.hpp"

#define CRLF "\r\n"

namespace http {

    Response::Response(const std::string& httpVersion) {
        this->httpVersion = httpVersion;
        this->pBodyStream = std::unique_ptr<IResponseStream>( new MemoryStream("") );
    }

    void Response::setStatus(const uint16_t statusCode) {
        this->statusCode = statusCode;
    }

    void Response::setHeader(std::string name, const std::string& value) {
        // Update header casing (ex. cOntENt-LeNGTh --> Content-Length)
        formatHeaderCasing(name);

        // Check if header already exists
        auto itrMatch = headers.find(name);
        if (itrMatch != headers.end())
            itrMatch->second = value;
        else // Otherwise, append
            this->headers.insert({name, value});
    }

    void Response::clearHeader(std::string name) {
        // Update header casing (ex. cOntENt-LeNGTh --> Content-Length)
        formatHeaderCasing(name);

        // Check if header already exists
        auto itrMatch = headers.find(name);
        if (itrMatch != headers.end())
            headers.erase(itrMatch);
    }

    int Response::loadBodyFromErrorDoc(const uint16_t statusCode) {
        this->setStatus(statusCode);
        this->setContentType("text/html");

        const int status = loadErrorDoc(statusCode, pBodyStream);
        this->setHeader("Content-Length", std::to_string(this->pBodyStream->size()));
        return status;
    }

    int Response::loadBodyFromFile(File& file) {
        const int bodyStatus = file.loadToBuffer(pBodyStream);

        // Get last modified GMT string
        if (bodyStatus == IO_SUCCESS)
            this->setHeader("Last-Modified", file.getLastModifiedGMT());

        this->setHeader("Content-Length", std::to_string(this->pBodyStream->size()));
        return bodyStatus;
    }

    int Response::compressBody(const int compressionType) {
        // Ignore if empty body
        if (this->pBodyStream->size() == 0) return IO_SUCCESS;

        const int status = IO_SUCCESS; //compressText(this->body, compressionType);

        // if (status == IO_SUCCESS) {
        //     switch (compressionType) {
        //         case COMPRESS_BROTLI:
        //             this->setHeader("Content-Encoding", "br");
        //             break;
        //         case COMPRESS_GZIP:
        //             this->setHeader("Content-Encoding", "gzip");
        //             break;
        //         case COMPRESS_DEFLATE:
        //             this->setHeader("Content-Encoding", "deflate");
        //             break;
        //     }
        // }

        return status;
    }

    const std::string Response::getContentType() const {
        auto type = this->headers.find("Content-Type");
        if (type == this->headers.end()) return "";
        return type->second;
    }

    int Response::getContentLength() const {
        auto type = this->headers.find("Content-Length");
        if (type == this->headers.end()) return -1;
        return std::stoi(type->second);
    }

    ssize_t Response::loadToBuffer(const bool omitBody, std::function<ssize_t(const char*, const size_t)>& sendFunc) {
        std::vector<char> chunk(conf::MAX_RESPONSE_BUFFER);

        // Handle HTTP/0.9 unique format
        if (this->httpVersion == "HTTP/0.9") {
            // Send chunks
            while (true) {
                // Read
                size_t bytesRead = pBodyStream->read(chunk.data(), conf::MAX_RESPONSE_BUFFER);
                if (bytesRead == 0) break;

                // Send
                ssize_t status = sendFunc(chunk.data(), bytesRead);
                if (status < 0) return status;
            }
            return 0;
        }

        // Determine Content-Length
        const std::string contentLen = std::to_string(this->pBodyStream->size());
        this->setHeader("Content-Length", contentLen);

        // Load config headers last to overwrite any dupes that have been previously set
        this->setHeader("Server", "Mercury/" + conf::VERSION.substr(9)); // Skip "Mercury v"
        this->setHeader("Date", getCurrentGMTString());

        // Stringify headers
        std::string headers;
        for (auto& [name, value] : this->headers)
            headers += name + ':' + value + CRLF;

        // Write to buffer
        std::string headersBlock = httpVersion + ' ' + std::to_string(statusCode) + ' '  + getReasonFromStatusCode(statusCode) + CRLF + headers + CRLF;
        ssize_t status = sendFunc(headersBlock.data(), headersBlock.size());
        if (status < 0) return status;

        // Omit the body from HEAD requests
        if (omitBody) return 0;

        // Send chunks
        while (true) {
            // Read
            size_t bytesRead = pBodyStream->read(chunk.data(), conf::MAX_RESPONSE_BUFFER);
            if (bytesRead == 0) break;

            // Send
            ssize_t status = sendFunc(chunk.data(), bytesRead);
            if (status < 0) return status;
        }

        // Base case, success
        return 0;
    }

};