#include "response.hpp"

#include <format>

#include "compressor_stream.hpp"
#include "../conf/conf.hpp"
#include "../logs/logger.hpp"
#include "../io/file_tools.hpp"
#include "../util/toolbox.hpp"

#define CRLF "\r\n"
#define str std::to_string

namespace http {

    Response::Response(const std::string& httpVersion) {
        this->httpVersion = httpVersion;
        this->pBodyStream = std::unique_ptr<IBodyStream>( new MemoryStream("") );
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
        switch ( this->compressMethod = compressMethod ) {
            case COMPRESS_ZSTD:    setHeader("Content-Encoding", "zstd");    break;
            case COMPRESS_BROTLI:  setHeader("Content-Encoding", "br");      break;
            case COMPRESS_GZIP:    setHeader("Content-Encoding", "gzip");    break;
            case COMPRESS_DEFLATE: setHeader("Content-Encoding", "deflate"); break;
        }
    }

    int Response::loadBodyFromErrorDoc(const uint16_t statusCode) {
        this->setStatus(statusCode);
        this->setContentType("text/html; charset=UTF-8");

        const int status = loadErrorDoc(statusCode, pBodyStream);
        this->setHeader("Content-Length", str(this->pBodyStream->size()));
        return status;
    }

    int Response::loadBodyFromFile(File& file) {
        const int bodyStatus = file.loadToBuffer(pBodyStream);

        // Get last modified GMT string
        if (bodyStatus == IO_SUCCESS && !file.isDirectory)
            this->setHeader("Last-Modified", file.getLastModifiedGMT());

        this->setHeader("Content-Length", str(this->pBodyStream->size()));
        return bodyStatus;
    }

    const std::string Response::getContentType() const {
        auto type = this->headers.find("Content-Type");
        if (type == this->headers.end()) return "";
        return type->second;
    }

    size_t Response::getContentLength() const {
        auto type = this->headers.find("Content-Length");
        if (type == this->headers.end()) return -1;
        return std::stoull(type->second);
    }

    bool Response::extendByteRanges(const std::vector<byte_range_t>& byteRanges) {
        // Validate each byte range
        const size_t streamSize = pBodyStream->size();
        const size_t npos = std::string::npos;
        for (const byte_range_t& byteRange : byteRanges) {
            const size_t first = byteRange.first, second = byteRange.second;
            if ((first == npos && (second > streamSize || second == 0)) || // Ex. bytes=-500
                (second == npos && (first >= streamSize)) || // Ex. bytes=0-
                (first != npos && second != npos && (first > second || first >= streamSize || second >= streamSize)) // Ex. bytes=200-300
            ) {
                return false;
            }
        }

        // Sort and coalesce byte ranges
        std::vector<byte_range_t> sortedByteRanges;
        intervalMergeByteRanges(byteRanges, sortedByteRanges, streamSize);

        // Verify only one byte range is present
        if (sortedByteRanges.size() > 1) return false;

        // Append each byte range
        this->originalBodySize = pBodyStream->size();
        for (const byte_range_t& byteRange : sortedByteRanges) {
            this->pBodyStream->addByteRange(byteRange);
            totalByteRangeSize += byteRange.second - byteRange.first + 1;
        }

        // Internally store the original size (for byte ranges)
        this->originalByteRanges.assign(sortedByteRanges.cbegin(), sortedByteRanges.cend());

        // Base case, success
        return true;
    }

    bool Response::precompressBody() {
        if (this->compressMethod == NO_COMPRESS && this->originalByteRanges.empty()) return true;

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
        this->setCompressMethod(this->compressMethod); // Update header

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
                        removeTempFile(tmpPath);
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
                    removeTempFile(tmpPath);
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
        setBodyStream( std::unique_ptr<IBodyStream>(new FileStream(tmpPath, true)) );

        // Base case, successful
        return true;
    }

    ssize_t Response::streamBody(const bool isHTMLAccepted, const bool omitBody, std::function<ssize_t(const char*, const size_t)>& sendFunc) {
        std::vector<char> readChunk(conf::RESPONSE_BUFFER_SIZE), compressChunk;

        // Handle HTTP/0.9 unique format
        if (this->httpVersion == "HTTP/0.9") {
            // Send chunks
            while (true) {
                size_t bytesRead = pBodyStream->read(readChunk.data(), conf::RESPONSE_BUFFER_SIZE);
                if (bytesRead == 0) break;
                ssize_t status = sendFunc(readChunk.data(), bytesRead);
                if (status < 0) return status;
            }
            return 0;
        }

        // Lambda to reset Content-Length, close the connection, and wipe the stream
        auto clearBodyAndResetStream = [&]() {
            this->clearHeader("Content-Type");
            this->clearHeader("Keep-Alive");
            this->setHeader("Content-Length", "0");
            this->setHeader("Connection", "close");
            setBodyStream( std::unique_ptr<IBodyStream>( new MemoryStream("") ) );
        };

        // Advertise byte ranges for FileStreams
        if (dynamic_cast<FileStream*>(pBodyStream.get()) != nullptr)
            this->setHeader("Accept-Ranges", "bytes");

        // Verify the content isn't too large as a MemoryStream
        if (dynamic_cast<MemoryStream*>(pBodyStream.get()) != nullptr &&
            ((!originalByteRanges.empty() && totalByteRangeSize > conf::MAX_RESPONSE_BODY) ||
            (originalByteRanges.empty() && pBodyStream->size() > conf::MAX_RESPONSE_BODY)))
        {
            this->setStatus(413);
            this->clearHeader("Keep-Alive");
            this->setHeader("Connection", "close");
            if (isHTMLAccepted) { // Replace body
                this->loadBodyFromErrorDoc(413);

                // Verify the error doc ALSO isn't too large
                if (pBodyStream->size() > conf::MAX_RESPONSE_BODY)
                    clearBodyAndResetStream();
            } else { // Remove body
                clearBodyAndResetStream();
            }
        }

        // Check if the body needs to be pre-compressed (HTTP/1.0 or HTTP/1.1+ w/ small bodies)
        bool wasPrecompressed = false;
        if (originalByteRanges.size() <= 1 &&
            (
                (httpVersion == "HTTP/1.0" && pBodyStream->size() > conf::MIN_COMPRESSION_SIZE) ||
                (pBodyStream->size() <= conf::RESPONSE_BUFFER_SIZE && pBodyStream->size() > conf::MIN_COMPRESSION_SIZE)
            )
        ) {
            if (!this->precompressBody()) { // Failed to compress body
                // Send uncompresesed error page
                this->originalByteRanges.clear();
                this->originalBodySize = 0;
                clearHeaders();

                ERROR_LOG << "Body precompression failure (could be from failure to create temp file)" << std::endl;
                setStatus(500);

                if (isHTMLAccepted) { // Replace body
                    loadBodyFromErrorDoc(500);

                    // Verify the error doc ALSO isn't too large
                    if (pBodyStream->size() > conf::MAX_RESPONSE_BODY)
                        clearBodyAndResetStream();
                } else { // Remove body
                    clearHeaders();
                    setHeader("Content-Length", "0");
                    setBodyStream( std::unique_ptr<IBodyStream>( new MemoryStream("") ) );
                }
            } else {
                wasPrecompressed = true;
            }
        } else if (originalByteRanges.size() > 1) {
            // Remove Content-Encoding for multipart byte ranges
            // UNIMPLEMENTED
            clearHeader("Content-Encoding");
        }

        // Create a streamable, buffered compressor
        std::unique_ptr<ICompressor> pCompressor(
            (wasPrecompressed || pBodyStream->size() <= conf::MIN_COMPRESSION_SIZE) ? nullptr : createCompressorStream(this->compressMethod)
        );

        // Skip compression for small bodies
        if (!wasPrecompressed && pBodyStream->size() <= conf::MIN_COMPRESSION_SIZE)
            clearHeader("Content-Encoding");

        // Update transfer encoding
        const size_t bodySize = pBodyStream->size();
        const std::string originalContentType = this->getContentType();

        const bool isTransEncSupported = this->httpVersion != "HTTP/1.0";
        const bool usingTransEnc = isTransEncSupported && bodySize > conf::RESPONSE_BUFFER_SIZE;
        if (!usingTransEnc && bodySize > 0) { // Content-Length is known (no compress)
            // Check for byte ranges
            if (!originalByteRanges.empty()) { // Single byte range
                setStatus(206); // Partial Content
                setHeader("Accept-Ranges", "bytes");

                const size_t startIndex = originalByteRanges[0].first;
                const size_t endIndex = originalByteRanges[0].second;

                setHeader("Content-Length", str(bodySize));
                setHeader("Content-Range", "bytes " + str(startIndex) + '-' + str(endIndex) + "/" + str(originalBodySize));
            } else {
                this->setHeader("Content-Length", str(bodySize));
            }
        } else if (usingTransEnc) { // Use chunked transfer encoding
            setHeader("Transfer-Encoding", "chunked");
            clearHeader("Content-Length");

            // Check for byte ranges
            if (!originalByteRanges.empty()) { // Single byte range
                setStatus(206); // Partial Content
                setHeader("Accept-Ranges", "bytes");
                const size_t startIndex = originalByteRanges[0].first;
                const size_t endIndex = originalByteRanges[0].second;

                setHeader("Content-Range", "bytes " + str(startIndex) + '-' + str(endIndex) + "/" + str(originalBodySize));
            }
        }

        // Load config headers last to overwrite any dupes that have been previously set
        setHeader("Server", "Mercury/" + conf::VERSION.substr(9)); // Skip "Mercury v"
        setHeader("Date", getCurrentGMTString());

        // Stringify headers
        std::string headers;
        for (auto& [name, value] : this->headers)
            headers += name + ": " + value + CRLF;

        // Write to buffer
        std::string headersBlock = httpVersion + ' ' + str(statusCode) + ' '  + getReasonFromStatus(statusCode) + CRLF + headers + CRLF;
        ssize_t status = sendFunc(headersBlock.data(), headersBlock.size());
        if (status < 0) return status;

        // Omit the body from HEAD requests OR if the body doesn't exist
        if (omitBody || bodySize == 0) return 0;

        // Send chunks
        auto sendWrapper = [&](const std::vector<char>& chunk, const size_t bytesRead) -> int {
            if (bytesRead == 0) return 0;
            if (usingTransEnc) {
                const std::string header = std::format("{:x}", bytesRead) + "\r\n";
                if (sendFunc(header.data(), header.size()) < 0) return -1;
                if (sendFunc(chunk.data(), bytesRead) < 0) return -1;
                if (sendFunc("\r\n", 2) < 0) return -1;
            } else {
                if (sendFunc(chunk.data(), bytesRead) < 0) return -1;
            }
            return 0;
        };

        // Set to true if sending a new range
        while (true) {
            // Read
            size_t bytesRead = pBodyStream->read(readChunk.data(), conf::RESPONSE_BUFFER_SIZE);
            if (bytesRead == 0 && originalByteRanges.size() <= 1) {
                // If sending multipart byte ranges, this is skipped
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
            } else { // Send w/o compression
                if (sendWrapper(readChunk, bytesRead) < 0) return -1;
            }
        }

        // End chunked transfer
        if (usingTransEnc)
            sendFunc("0\r\n\r\n", 5);

        // Base case, success
        return 0;
    }

}

#undef CRLF
#undef toStr