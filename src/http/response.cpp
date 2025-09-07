#include "response.hpp"

#define CRLF "\r\n"

namespace http {

    Response::Response(const std::string& httpVersion) {
        this->httpVersion = httpVersion;
        this->pBodyStream = std::unique_ptr<IBodyStream>( new MemoryStream("") );
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

    void Response::setCompressMethod(const int compressMethod) {
        // Determine compression method
        this->compressMethod = compressMethod;
        switch (this->compressMethod) {
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

    int Response::loadBodyFromErrorDoc(const uint16_t statusCode) {
        this->setStatus(statusCode);
        this->setContentType("text/html; charset=UTF-8");

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

    bool Response::precompressBody() {
        // Get path to a temp file
        std::string tmpPath;
        if (!createTempFile(tmpPath)) return false;

        // Buffer the output continuously to the temp file
        std::ofstream handle(tmpPath, std::ios::binary);
        if (!handle.is_open()) {
            removeTempFile(tmpPath);
            return false;
        }

        // Create compressor
        std::unique_ptr<ICompressor> pCompressor = std::unique_ptr<ICompressor>( createCompressorStream(this->compressMethod) );

        // Write in chunks
        std::vector<char> readChunk(conf::RESPONSE_BUFFER_SIZE), compressChunk;
        size_t contentLen = 0;
        while (true) {
            // Read
            size_t bytesRead = pBodyStream->read(readChunk.data(), conf::RESPONSE_BUFFER_SIZE);
            if (bytesRead == 0) {
                // Send any remaining compression data
                if (pCompressor != nullptr) {
                    // Compress
                    compressChunk.clear();
                    bytesRead = pCompressor->finish(compressChunk);
                    if (pCompressor->status() != STREAM_SUCCESS) {
                        ERROR_LOG << "Precompression error (end flush)." << std::endl;
                        return false;
                    }

                    // Write
                    contentLen += bytesRead;
                    handle.write(compressChunk.data(), bytesRead);
                }
                break;
            }

            // Compress
            if (pCompressor != nullptr) {
                compressChunk.clear();
                bytesRead = pCompressor->compress(readChunk.data(), compressChunk, bytesRead);
                if (pCompressor->status() != STREAM_SUCCESS) {
                    ERROR_LOG << "Precompression error." << std::endl;
                    return false;
                }

                // Write
                contentLen += bytesRead;
                handle.write(compressChunk.data(), bytesRead);
            } else {
                // Write w/o compression
                contentLen += bytesRead;
                handle.write(readChunk.data(), bytesRead);
            }
        }

        // Close the file
        handle.close();

        // Create FileStream in place of body stream to buffer the response
        this->pBodyStream = std::unique_ptr<IBodyStream>(new FileStream(tmpPath, true));

        // Base case, successful
        return true;
    }

    ssize_t Response::beginStreamingBody(const bool isHTMLAccepted, const bool omitBody, std::function<ssize_t(const char*, const size_t)>& sendFunc) {
        std::vector<char> readChunk(conf::RESPONSE_BUFFER_SIZE), compressChunk;

        // Handle HTTP/0.9 unique format
        if (this->httpVersion == "HTTP/0.9") {
            // Send chunks
            while (true) {
                // Read
                size_t bytesRead = pBodyStream->read(readChunk.data(), conf::RESPONSE_BUFFER_SIZE);
                if (bytesRead == 0) break;

                // Send
                ssize_t status = sendFunc(readChunk.data(), bytesRead);
                if (status < 0) return status;
            }
            return 0;
        }

        // Check if the body needs to be pre-compressed (HTTP/1.0 ONLY)
        if (this->httpVersion == "HTTP/1.0" && this->pBodyStream->size() > 0) {
            if (!this->precompressBody()) {
                this->setStatus(500);
                if (isHTMLAccepted)
                    this->loadBodyFromErrorDoc(500);
            }
        }

        // Create a streamable, buffered compressor
        std::unique_ptr<ICompressor> pCompressor(
            (httpVersion == "HTTP/1.0") ? nullptr : createCompressorStream(this->compressMethod)
        );

        // Update transfer encoding
        bool isTransferEncoding = this->httpVersion != "HTTP/1.0";
        if (!isTransferEncoding) { // Content-Length is known (no compress)
            this->setHeader("Content-Length", std::to_string(this->pBodyStream->size()));
        } else { // Use chunked transfer encoding
            this->setHeader("Transfer-Encoding", "chunked");
        }

        // Load config headers last to overwrite any dupes that have been previously set
        this->setHeader("Server", "Mercury/" + conf::VERSION.substr(9)); // Skip "Mercury v"
        this->setHeader("Date", getCurrentGMTString());

        // Stringify headers
        std::string headers;
        for (auto& [name, value] : this->headers)
            headers += name + ": " + value + CRLF;

        // Write to buffer
        std::string headersBlock = httpVersion + ' ' + std::to_string(statusCode) + ' '  + getReasonFromStatusCode(statusCode) + CRLF + headers + CRLF;
        ssize_t status = sendFunc(headersBlock.data(), headersBlock.size());
        if (status < 0) return status;

        // Omit the body from HEAD requests
        if (omitBody || pBodyStream->size() == 0) return 0;

        // Send chunks
        auto sendWrapper = [&](const std::vector<char>& chunk, const size_t bytesRead) -> int {
            if (bytesRead == 0) return 0;
            if (isTransferEncoding) {
                const std::string header = std::format("{:x}", bytesRead) + "\r\n";
                if (sendFunc(header.data(), header.size()) < 0) return -1;
                if (sendFunc(chunk.data(), bytesRead) < 0) return -1;
                if (sendFunc("\r\n", 2) < 0) return -1;
            } else {
                if (sendFunc(chunk.data(), bytesRead) < 0) return -1;
            }
            return 0;
        };

        while (true) {
            // Read
            size_t bytesRead = pBodyStream->read(readChunk.data(), conf::RESPONSE_BUFFER_SIZE);
            if (bytesRead == 0) {
                // Send any remaining compression data
                if (pCompressor != nullptr) {
                    // Compress
                    compressChunk.clear();
                    bytesRead = pCompressor->finish(compressChunk);
                    if (pCompressor->status() != STREAM_SUCCESS) {
                        ERROR_LOG << "Compression error (end flush)." << std::endl;
                        return -1;
                    }

                    // Send
                    if (sendWrapper(compressChunk, bytesRead) < 0) return -1;
                }
                break;
            }

            // Compress
            if (pCompressor != nullptr) {
                compressChunk.clear();
                bytesRead = pCompressor->compress(readChunk.data(), compressChunk, bytesRead);
                if (pCompressor->status() != STREAM_SUCCESS) {
                    ERROR_LOG << "Compression error." << std::endl;
                    return -1;
                }

                // Send
                if (sendWrapper(compressChunk, bytesRead) < 0)
                    return -1;
            } else if (sendWrapper(readChunk, bytesRead) < 0) { // Send w/o compression
                return -1;
            }
        }

        // End chunked transfer
        if (isTransferEncoding)
            sendFunc("0\r\n\r\n", 5);

        // Base case, success
        return 0;
    }

};