#include "response.hpp"

#include "../streams/compressor_stream.hpp"
#include "../../conf/conf.hpp"
#include "../../logs/logger.hpp"
#include "../../io/file_tools.hpp"
#include "../../util/tools/toolbox.hpp"

#define CRLF "\r\n"
#define tostr std::to_string

namespace {
    // Thread-local response read buffers eliminate per-request buffer allocation churn
    thread_local std::vector<char> threadLocalStreamBuffer;
    thread_local std::string threadLocalHeadersBuffer;

    inline char* getStreamBuffer(const size_t requiredSize) {
        if (threadLocalStreamBuffer.size() < requiredSize)
            threadLocalStreamBuffer.resize(requiredSize);
        return threadLocalStreamBuffer.data();
    }

    inline std::string& getHeadersBuffer() {
        return threadLocalHeadersBuffer;
    }
}

namespace http {

    Response::Response(const std::string& httpVersion) {
        this->httpVersion = httpVersion;
        this->pBodyStream = std::unique_ptr<IBodyStream>( new MemoryStream("") );
    }

    void Response::setHeader(std::string name, const std::string& value) {
        if (!isValidHeaderName(name) || !isValidHeaderValue(value))
            throw std::invalid_argument("Invalid response header");

        // Update header casing (ex. cOntENt-LeNGTh --> Content-Length)
        formatHeaderCasing(name);

        if (name == "Content-Encoding")
            this->hasCustomContentEncoding = true;

        if (name == "Set-Cookie") {
            setCookies.clear();
            setCookies.push_back(value);
            return;
        }

        // Check if header already exists
        auto itrMatch = headers.find(name);
        if (itrMatch != headers.end())
            itrMatch->second = value;
        else // Otherwise, append
            this->headers.insert({name, value});
    }

    void Response::addHeader(std::string name, const std::string& value) {
        if (!isValidHeaderName(name) || !isValidHeaderValue(value))
            throw std::invalid_argument("Invalid response header");

        formatHeaderCasing(name);

        if (name == "Content-Encoding")
            this->hasCustomContentEncoding = true;

        if (name == "Set-Cookie") {
            setCookies.push_back(value);
            return;
        }

        if (name == "Vary") {
            addVary(value);
            return;
        }

        auto itrMatch = headers.find(name);
        if (itrMatch != headers.end())
            itrMatch->second = value;
        else
            this->headers.insert({name, value});
    }

    void Response::clearHeader(std::string name) {
        // Update header casing (ex. cOntENt-LeNGTh --> Content-Length)
        formatHeaderCasing(name);

        if (name == "Content-Encoding")
            this->hasCustomContentEncoding = false;

        if (name == "Set-Cookie") {
            setCookies.clear();
            return;
        }

        // Check if header already exists
        auto itrMatch = headers.find(name);
        if (itrMatch != headers.end())
            headers.erase(itrMatch);
    }

    void Response::addVary(std::string_view value) {
        auto itrMatch = headers.find("Vary");
        if (itrMatch == headers.end()) {
            headers.insert({"Vary", std::string(value)});
            return;
        }

        if (itrMatch->second == "*") return;

        // Parse incoming tokens
        size_t start = 0;
        while (start < value.size()) {
            const size_t comma = value.find(',', start);
            std::string_view token = (comma == std::string_view::npos)
                ? value.substr(start)
                : value.substr(start, comma - start);
            start = (comma == std::string_view::npos) ? value.size() : comma + 1;

            const size_t first = token.find_first_not_of(" \t");
            if (first == std::string_view::npos) continue;
            const size_t last = token.find_last_not_of(" \t");
            token = token.substr(first, last - first + 1);
            if (token.empty()) continue;

            if (token == "*") { itrMatch->second = "*"; return; }

            // Check if token already exists in current Vary header (case-insensitive)
            bool found = false;
            size_t curStart = 0;
            while (curStart < itrMatch->second.size()) {
                const size_t curComma = itrMatch->second.find(',', curStart);
                std::string_view curToken = (curComma == std::string_view::npos)
                    ? std::string_view(itrMatch->second).substr(curStart)
                    : std::string_view(itrMatch->second).substr(curStart, curComma - curStart);
                curStart = (curComma == std::string_view::npos) ? itrMatch->second.size() : curComma + 1;

                const size_t cFirst = curToken.find_first_not_of(" \t");
                if (cFirst == std::string_view::npos) continue;
                const size_t cLast = curToken.find_last_not_of(" \t");
                curToken = curToken.substr(cFirst, cLast - cFirst + 1);

                if (curToken.size() == token.size() &&
                    std::equal(curToken.begin(), curToken.end(), token.begin(), [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                    })) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                if (!itrMatch->second.empty())
                    itrMatch->second += ", ";
                itrMatch->second.append(token);
            }
        }
    }

    void Response::setCompressMethod(const int compressMethod) {
        // Retain backend-provided content encoding without recompression
        if (this->hasCustomContentEncoding) return;

        // Determine compression method
        switch ( this->compressMethod = compressMethod ) {
            case COMPRESS_ZSTD:    headers["Content-Encoding"] = "zstd";    break;
            case COMPRESS_BROTLI:  headers["Content-Encoding"] = "br";      break;
            case COMPRESS_GZIP:    headers["Content-Encoding"] = "gzip";    break;
            case COMPRESS_DEFLATE: headers["Content-Encoding"] = "deflate"; break;
            default:               clearHeader("Content-Encoding");          break;
        }
    }

    bool Response::shouldCloseConnection() const {
        auto connection = headers.find("Connection");
        if (connection == headers.end()) return false;
        std::string value = connection->second;
        strToUpper(value);
        std::vector<std::string> tokens;
        splitString(tokens, value, ',', true);
        for (auto token : tokens) {
            trimString(token);
            if (token == "CLOSE") return true;
        }
        return false;
    }

    void Response::clearByteRanges() {
        originalByteRanges.clear();
        originalBodySize = totalByteRangeSize = 0;
        clearHeader("Content-Range");
    }

    int Response::loadBodyFromErrorDoc(const uint16_t statusCode) {
        this->setStatus(statusCode);
        this->setContentType("text/html; charset=UTF-8");

        // A replacement document must not inherit the original representation's ranges.
        // Start empty so a failed load cannot disclose the previous response body.
        clearByteRanges();
        setBodyStream(std::make_unique<MemoryStream>(""));
        const int status = loadErrorDoc(statusCode, pBodyStream);
        if (status != IO_SUCCESS) setBodyStream(std::make_unique<MemoryStream>(""));
        this->setHeader("Content-Length", tostr(this->pBodyStream->size()));
        return status;
    }

    int Response::loadBodyFromFile(File& file) {
        const int bodyStatus = file.loadToBuffer(pBodyStream);

        // Do not retain an old body or inspect an unusable stream after a failed load
        if (bodyStatus != IO_SUCCESS) {
            clearByteRanges();
            setBodyStream(std::make_unique<MemoryStream>(""));
        }

        // Get last modified GMT string and advertise range support for static files
        if (bodyStatus == IO_SUCCESS && !file.isDirectory) {
            this->setHeader("Last-Modified", file.getLastModifiedGMT());
            this->setHeader("Accept-Ranges", "bytes");
        }

        this->setHeader("Content-Length", tostr(this->pBodyStream->size()));
        return bodyStatus;
    }

    const std::string Response::getContentType() const {
        auto type = this->headers.find("Content-Type");
        if (type == this->headers.end()) return "";
        return type->second;
    }

    size_t Response::getContentLength() const {
        auto type = this->headers.find("Content-Length");
        if (type == this->headers.end()) return pBodyStream->size();
        return std::stoull(type->second);
    }

    bool Response::extendByteRanges(const std::vector<byte_range_t>& byteRanges) {
        if (byteRanges.empty()) return true;
        const size_t streamSize = pBodyStream->size();
        if (streamSize == 0) return false;

        // Filter satisfiable byte ranges (RFC 9110 Section 14.1.2)
        const size_t npos = std::string::npos;
        std::vector<byte_range_t> satisfiableRanges;
        for (const byte_range_t& byteRange : byteRanges) {
            const size_t first = byteRange.first, second = byteRange.second;
            if (first == npos) {
                // Suffix range: bytes=-N (N must be > 0)
                if (second == 0) return false;
                satisfiableRanges.push_back(byteRange);
            } else if (second == npos) {
                // Open range: bytes=N- (N must be < streamSize)
                if (first < streamSize) satisfiableRanges.push_back(byteRange);
            } else {
                // Explicit range: bytes=N-M (first must be <= second)
                if (first > second) return false;
                if (first < streamSize) satisfiableRanges.push_back(byteRange);
            }
        }
        if (satisfiableRanges.empty()) return false;

        // Sort and coalesce byte ranges
        std::vector<byte_range_t> sortedByteRanges;
        intervalMergeByteRanges(satisfiableRanges, sortedByteRanges, streamSize);

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
        if (pBodyStream == nullptr) return false;

        // Create compressor
        std::unique_ptr<ICompressor> pCompressor = std::unique_ptr<ICompressor>( createCompressorStream(this->compressMethod) );
        if (this->compressMethod != NO_COMPRESS && (pCompressor == nullptr || pCompressor->status() != STREAM_SUCCESS)) {
            ERROR_LOG << "Precompression error: failed to initialize compressor." << std::endl;
            return false;
        }
        this->setCompressMethod(this->compressMethod); // Update header

        // Compress entirely in-memory if body fits within response buffer
        if (pBodyStream->size() <= conf::RESPONSE_BUFFER_SIZE) {
            std::string inMemoryBuffer;
            inMemoryBuffer.reserve(pBodyStream->size());
            char* pReadChunk = getStreamBuffer(conf::RESPONSE_BUFFER_SIZE);
            std::vector<char> compressChunk;
            while (true) {
                size_t bytesRead = pBodyStream->read(pReadChunk, conf::RESPONSE_BUFFER_SIZE);
                if (bytesRead == 0) {
                    if (pCompressor != nullptr) {
                        compressChunk.clear();
                        bytesRead = pCompressor->finish(compressChunk);
                        if (pCompressor->status() != STREAM_SUCCESS) {
                            ERROR_LOG << "Precompression error (end flush)." << std::endl;
                            return false;
                        }
                        inMemoryBuffer.append(compressChunk.data(), bytesRead);
                    }
                    break;
                }

                if (pCompressor != nullptr) {
                    compressChunk.clear();
                    bytesRead = pCompressor->compress(pReadChunk, compressChunk, bytesRead);
                    if (pCompressor->status() != STREAM_SUCCESS) {
                        ERROR_LOG << "Precompression error." << std::endl;
                        return false;
                    }
                    inMemoryBuffer.append(compressChunk.data(), bytesRead);
                } else {
                    inMemoryBuffer.append(pReadChunk, bytesRead);
                }
            }

            setBodyStream(std::make_unique<MemoryStream>(std::move(inMemoryBuffer)));
            return true;
        }

        // Buffer large bodies to temp file on disk
        std::string tmpPath;
        if (!createTempFile(tmpPath)) return false;

        // Buffer the output continuously to the temp file
        std::ofstream handle(tmpPath, std::ios::binary);
        if (!handle.is_open()) {
            removeTempFile(tmpPath);
            return false;
        }

        // Write in chunks
        char* pReadChunk = getStreamBuffer(conf::RESPONSE_BUFFER_SIZE);
        std::vector<char> compressChunk;
        while (true) {
            // Read
            size_t bytesRead = pBodyStream->read(pReadChunk, conf::RESPONSE_BUFFER_SIZE);
            if (bytesRead == 0) {
                // Send any remaining compression data
                if (pCompressor != nullptr) {
                    // Compress
                    compressChunk.clear();
                    bytesRead = pCompressor->finish(compressChunk);
                    if (pCompressor->status() != STREAM_SUCCESS) {
                        ERROR_LOG << "Precompression error (end flush)." << std::endl;
                        handle.close();
                        removeTempFile(tmpPath);
                        return false;
                    }

                    // Write
                    handle.write(compressChunk.data(), bytesRead);
                }
                break;
            }

            // Compress
            if (pCompressor != nullptr) {
                compressChunk.clear();
                bytesRead = pCompressor->compress(pReadChunk, compressChunk, bytesRead);
                if (pCompressor->status() != STREAM_SUCCESS) {
                    ERROR_LOG << "Precompression error." << std::endl;
                    handle.close();
                    removeTempFile(tmpPath);
                    return false;
                }

                // Write
                handle.write(compressChunk.data(), bytesRead);
            } else {
                // Write w/o compression
                handle.write(pReadChunk, bytesRead);
            }
        }

        // Close the file
        handle.close();
        if (handle.fail()) {
            removeTempFile(tmpPath);
            return false;
        }

        // Create FileStream in place of body stream to buffer the response
        auto compressed = std::make_unique<FileStream>(tmpPath, true);
        if (compressed->status() != STREAM_SUCCESS) return false;
        setBodyStream(std::move(compressed));

        // Base case, successful
        return true;
    }

    ssize_t Response::streamBody(const bool isHTMLAccepted, const bool omitBody, std::function<ssize_t(const char*, const size_t)>& writeFunc) {
        // A successful socket write may consume only part of a header or body chunk.
        auto sendFunc = [&](const char* data, const size_t size) -> ssize_t {
            size_t sent = 0;
            while (sent < size) {
                const ssize_t n = writeFunc(data + sent, size - sent);
                if (n <= 0 || static_cast<size_t>(n) > size - sent) return -1;
                sent += static_cast<size_t>(n);
            }
            return static_cast<ssize_t>(sent);
        };

        // Lambda to reset Content-Length, close the connection, and wipe the stream
        auto clearBodyAndResetStream = [&]() {
            clearByteRanges();
            compressMethod = NO_COMPRESS;
            clearHeader("Content-Encoding");
            this->clearHeader("Content-Type");
            this->clearHeader("Keep-Alive");
            this->clearHeader("Accept-Ranges");
            this->clearHeader("Last-Modified");
            this->clearHeader("ETag");
            this->setHeader("Content-Length", "0");
            this->setHeader("Connection", "close");
            setBodyStream( std::unique_ptr<IBodyStream>( new MemoryStream("") ) );
        };

        // Reject responses exceeding configured body limit
        if (!omitBody && pBodyStream != nullptr &&
            ((!originalByteRanges.empty() && totalByteRangeSize > conf::MAX_RESPONSE_BODY) ||
            (originalByteRanges.empty() && pBodyStream->size() > conf::MAX_RESPONSE_BODY)))
        {
            this->setStatus(413);
            this->clearHeader("Keep-Alive");
            this->clearHeader("Accept-Ranges");
            this->clearHeader("Content-Encoding");
            this->clearHeader("Last-Modified");
            this->clearHeader("ETag");
            this->setHeader("Connection", "close");
            compressMethod = NO_COMPRESS;
            if (isHTMLAccepted) { // Replace body
                this->loadBodyFromErrorDoc(413);

                // Verify the error doc ALSO isn't too large
                if (pBodyStream->size() > conf::MAX_RESPONSE_BODY)
                    clearBodyAndResetStream();
            } else { // Remove body
                clearBodyAndResetStream();
            }
        }

        // Handle HTTP/0.9 unique format
        if (this->httpVersion == "HTTP/0.9") {
            // Verify status is unset (only set by errors)
            if (statusCode != RESPONSE_DEFAULT_STATUS_HTTP_0_9) {
                // Signal the server to close the connection
                // By returning < 0
                return -1;
            }

            char* pReadChunk = getStreamBuffer(conf::RESPONSE_BUFFER_SIZE);
            // Send chunks
            while (true) {
                size_t bytesRead = pBodyStream->read(pReadChunk, conf::RESPONSE_BUFFER_SIZE);
                if (bytesRead == 0) break;
                ssize_t status = sendFunc(pReadChunk, bytesRead);
                if (status < 0) return status;
            }
            return 0;
        }

        // Advertise byte ranges for FileStreams or range-capable static bodies
        if (dynamic_cast<FileStream*>(pBodyStream.get()) != nullptr ||
            dynamic_cast<CachedFileStream*>(pBodyStream.get()) != nullptr ||
            this->headers.contains("Accept-Ranges"))
            this->setHeader("Accept-Ranges", "bytes");

        // Check if the body needs to be pre-compressed (HTTP/1.0 or HTTP/1.1+ w/ small bodies)
        bool wasPrecompressed = false;
        if (!hasCustomContentEncoding && pBodyStream->size() > 0 && originalByteRanges.size() <= 1 &&
            (
                (httpVersion == "HTTP/1.0" && (pBodyStream->size() > conf::MIN_COMPRESSION_SIZE || (!identityAccepted && compressMethod != NO_COMPRESS))) ||
                (pBodyStream->size() <= conf::RESPONSE_BUFFER_SIZE && (pBodyStream->size() > conf::MIN_COMPRESSION_SIZE || (!identityAccepted && compressMethod != NO_COMPRESS)))
            )
        ) {
            if (!this->precompressBody()) { // Failed to compress body
                // The error page is a new, uncompressed representation.
                clearByteRanges();
                compressMethod = NO_COMPRESS;
                clearHeaders();
                setHeader("Connection", "close");

                ERROR_LOG << "Body precompression failure (could be from failure to create temp file)" << std::endl;
                setStatus(500);

                if (isHTMLAccepted) { // Replace body
                    loadBodyFromErrorDoc(500);

                    // Verify the error doc ALSO isn't too large
                    if (pBodyStream->size() > conf::MAX_RESPONSE_BODY)
                        clearBodyAndResetStream();
                } else { // Remove body
                    clearBodyAndResetStream();
                }
            } else {
                wasPrecompressed = true;
            }
        } else if (originalByteRanges.size() > 1) {
            // Remove Content-Encoding for multipart byte ranges
            // UNIMPLEMENTED
            ERROR_LOG << "Unimplemented - multipart byte ranges" << std::endl;
            clearHeader("Content-Encoding");
        }

        // Create a streamable, buffered compressor
        std::unique_ptr<ICompressor> pCompressor(
            (hasCustomContentEncoding || wasPrecompressed || pBodyStream->size() == 0 || (identityAccepted && pBodyStream->size() <= conf::MIN_COMPRESSION_SIZE)) ? nullptr : createCompressorStream(this->compressMethod)
        );

        // Skip compression for small bodies
        if (!hasCustomContentEncoding && !wasPrecompressed && (pBodyStream->size() == 0 || (identityAccepted && pBodyStream->size() <= conf::MIN_COMPRESSION_SIZE)))
            clearHeader("Content-Encoding");

        // Update transfer encoding
        const size_t bodySize = pBodyStream->size();

        const bool isTransEncSupported = this->httpVersion != "HTTP/1.0";
        const bool usingTransEnc = isTransEncSupported && bodySize > conf::RESPONSE_BUFFER_SIZE;
        if (!usingTransEnc && bodySize > 0) { // Content-Length is known (no compress)
            // Check for byte ranges
            if (!originalByteRanges.empty()) { // Single byte range
                setStatus(206); // Partial Content
                setHeader("Accept-Ranges", "bytes");

                const size_t startIndex = originalByteRanges[0].first;
                const size_t endIndex = originalByteRanges[0].second;

                setHeader("Content-Length", tostr(bodySize));
                setHeader("Content-Range", "bytes " + tostr(startIndex) + '-' + tostr(endIndex) + "/" + tostr(originalBodySize));
            } else {
                this->setHeader("Content-Length", tostr(bodySize));
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

                setHeader("Content-Range", "bytes " + tostr(startIndex) + '-' + tostr(endIndex) + "/" + tostr(originalBodySize));
            }
        }

        // Frame empty redirects/errors on persistent connections as well.
        if (bodySize == 0 && statusCode != 204 && statusCode != 304)
            setHeader("Content-Length", "0");

        // Ensure representation selection includes Vary: Accept-Encoding (RFC 9110 Section 12.5.5)
        if (headers.contains("Content-Encoding") || statusCode == 200 || statusCode == 206 || statusCode == 304 || statusCode == 406)
            addVary("Accept-Encoding");

        // Load config headers last to overwrite any dupes that have been previously set
        static const std::string serverHeader = "Mercury/" + (conf::VERSION.size() > 9 ? conf::VERSION.substr(9) : conf::VERSION);
        setHeader("Server", serverHeader);
        setHeader("Date", getCurrentGMTString());

        // Pre-allocate header block to eliminate reallocations
        const std::string_view reason = getReasonFromStatus(statusCode);
        const std::string statusCodeStr = tostr(statusCode);
        size_t estimatedSize = httpVersion.size() + 1 + statusCodeStr.size() + 1 + reason.size() + 2 + 2;
        for (const auto& [name, value] : this->headers)
            estimatedSize += name.size() + 2 + value.size() + 2;
        for (const auto& cookie : this->setCookies)
            estimatedSize += 12 + cookie.size() + 2;

        const bool combineBody = !omitBody && bodySize > 0 && bodySize <= conf::RESPONSE_BUFFER_SIZE;
        const size_t totalBufferSize = estimatedSize + (combineBody ? bodySize : 0);

        std::string& headersBlock = getHeadersBuffer();
        headersBlock.clear();
        if (headersBlock.capacity() < totalBufferSize)
            headersBlock.reserve(totalBufferSize);

        // Format status line
        headersBlock.append(httpVersion);
        headersBlock.push_back(' ');
        headersBlock.append(statusCodeStr);
        headersBlock.push_back(' ');
        headersBlock.append(reason);
        headersBlock.append(CRLF);

        // Append response headers
        for (const auto& [name, value] : this->headers) {
            headersBlock.append(name);
            headersBlock.append(": ");
            headersBlock.append(value);
            headersBlock.append(CRLF);
        }
        for (const auto& cookie : this->setCookies) {
            headersBlock.append("Set-Cookie: ");
            headersBlock.append(cookie);
            headersBlock.append(CRLF);
        }
        headersBlock.append(CRLF);

        // Coalesce headers and small body payload into a single socket write
        if (combineBody) {
            const size_t headerLen = headersBlock.size();
            headersBlock.resize(headerLen + bodySize);
            const size_t bytesRead = pBodyStream->read(headersBlock.data() + headerLen, bodySize);
            if (bytesRead < bodySize)
                headersBlock.resize(headerLen + bytesRead);
            const ssize_t writeStatus = sendFunc(headersBlock.data(), headersBlock.size());
            if (headersBlock.capacity() > conf::RESPONSE_BUFFER_SIZE + 4096)
                headersBlock.shrink_to_fit();
            return (writeStatus < 0) ? writeStatus : 0;
        }

        // Write to buffer
        ssize_t status = sendFunc(headersBlock.data(), headersBlock.size());
        if (headersBlock.capacity() > conf::RESPONSE_BUFFER_SIZE + 4096)
            headersBlock.shrink_to_fit();
        if (status < 0) return status;

        // Omit the body from HEAD requests OR if the body doesn't exist
        if (omitBody || bodySize == 0) return 0;

        // Send chunks
        auto sendWrapper = [&](const char* pChunk, const size_t bytesRead) -> int {
            if (bytesRead == 0) return 0;
            if (usingTransEnc) {
                // Convert to hex
                char hexBuf[32];
                const int hexLen = snprintf(hexBuf, sizeof(hexBuf), "%zx\r\n", bytesRead);
                if (hexLen > 0 && sendFunc(hexBuf, static_cast<size_t>(hexLen)) < 0) return -1;
                if (sendFunc(pChunk, bytesRead) < 0) return -1;
                if (sendFunc("\r\n", 2) < 0) return -1;
            } else {
                if (sendFunc(pChunk, bytesRead) < 0) return -1;
            }
            return 0;
        };

        char* pReadChunk = getStreamBuffer(conf::RESPONSE_BUFFER_SIZE);
        std::vector<char> compressChunk;

        // Set to true if sending a new range
        while (true) {
            // Read
            size_t bytesRead = pBodyStream->read(pReadChunk, conf::RESPONSE_BUFFER_SIZE);
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
                    if (sendWrapper(compressChunk.data(), bytesRead) < 0) return -1;
                }
                break;
            }

            // Compress
            if (pCompressor != nullptr) {
                compressChunk.clear();
                bytesRead = pCompressor->compress(pReadChunk, compressChunk, bytesRead);
                if (pCompressor->status() != STREAM_SUCCESS) {
                    ERROR_LOG << "Compression error." << std::endl;
                    return -1;
                }

                // Send
                if (sendWrapper(compressChunk.data(), bytesRead) < 0)
                    return -1;
            } else { // Send w/o compression
                if (sendWrapper(pReadChunk, bytesRead) < 0) return -1;
            }
        }

        // End chunked transfer
        if (usingTransEnc && sendFunc("0\r\n\r\n", 5) < 0)
            return -1;

        // Base case, success
        return 0;
    }

}

#undef CRLF
#undef toStr
