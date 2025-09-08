#include "body_stream.hpp"

#include <cstring>

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"
#include "../util/string_tools.hpp"
#include "../io/file_tools.hpp"

namespace http {

    ZlibCompressor::ZlibCompressor(const int method) {
        // Init zlib
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;

        // Handle gzip vs deflate
        const int windowBits = (method == COMPRESS_GZIP) ? (15 | 16) : 15;
        if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            this->_status = STREAM_FAILURE;
        }

        // Create fixed-size internal buffer
        internalBuffer = std::vector<char>(
            deflateBound(&stream, conf::RESPONSE_BUFFER_SIZE)
        );
    }

    ZlibCompressor::~ZlibCompressor() { deflateEnd(&stream); }

    // Huge thanks to https://www.zlib.net/zpipe.c
    size_t ZlibCompressor::compress(const char* src, std::vector<char>& dest, const size_t size, const int flushMode) {
        if (size == 0 && flushMode == Z_NO_FLUSH)
            return 0; // Skip, should call finish() instead

        // Prepare compression
        stream.avail_in = static_cast<uInt>(size);
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(src));

        // Clear destination buffer
        dest.clear();

        // Compress & check success
        do {
            stream.avail_out = internalBuffer.size();
            stream.next_out = reinterpret_cast<Bytef*>(internalBuffer.data());

            const int ret = deflate(&stream, flushMode);
            if (ret == Z_STREAM_ERROR) {
                this->_status = STREAM_FAILURE;
                return 0;
            }

            const size_t have = internalBuffer.size() - stream.avail_out;
            dest.insert(dest.end(), internalBuffer.data(), internalBuffer.data() + have);
        } while (stream.avail_out == 0); // Read entire output

        // Return size of vector
        return dest.size();
    }

    size_t ZlibCompressor::finish(std::vector<char>& dest) {
        return compress(nullptr, dest, 0, Z_FINISH);
    }

    BrotliCompressor::BrotliCompressor() : internalBuffer(conf::REQUEST_BUFFER_SIZE) {
        if (!(state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr))) {
            this->_status = STREAM_FAILURE;
            return;
        }

        // Init encoder
        BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY, BROTLI_DEFAULT_QUALITY);
        BrotliEncoderSetParameter(state, BROTLI_PARAM_LGWIN, BROTLI_DEFAULT_WINDOW);
        BrotliEncoderSetParameter(state, BROTLI_PARAM_MODE, BROTLI_MODE_GENERIC);
    }

    BrotliCompressor::~BrotliCompressor() {
        if (state) {
            BrotliEncoderDestroyInstance(state);
            state = nullptr;
        }
    }

    size_t BrotliCompressor::compress(const char* src, std::vector<char>& dest, const size_t size, const int flags) {
        // Abort if failed
        if (this->_status == STREAM_FAILURE || !state)
            return 0;

        // Prepare compression
        size_t availIn = size;
        const uint8_t* nextIn = reinterpret_cast<const uint8_t*>(src);

        BrotliEncoderOperation op = BROTLI_OPERATION_PROCESS;
        if (flags == BROTLI_OPERATION_FINISH)
            op = BROTLI_OPERATION_FINISH;

        // Clear destination buffer
        dest.clear();

        while (availIn > 0 || op == BROTLI_OPERATION_FINISH) {
            size_t avail_out = internalBuffer.size();
            uint8_t* next_out = reinterpret_cast<uint8_t*>(internalBuffer.data());

            // Compress
            if (!BrotliEncoderCompressStream(state, op, &availIn, &nextIn, &avail_out, &next_out, nullptr)) {
                this->_status = STREAM_FAILURE;
                return 0;
            }

            // Count used size
            size_t used = internalBuffer.size() - avail_out;
            if (used > 0)
                dest.insert(dest.end(), internalBuffer.data(), internalBuffer.data() + used);

            if ((op == BROTLI_OPERATION_PROCESS && availIn == 0)
                || (op == BROTLI_OPERATION_FINISH && BrotliEncoderIsFinished(state)))
                break;
        }

        return dest.size();
    }

    size_t BrotliCompressor::finish(std::vector<char>& dest) {
        if (this->_status == STREAM_FAILURE || !state)
            return 0;

        // Clear destination buffer
        dest.clear();
        while (!BrotliEncoderIsFinished(state)) {
            // Prepare compression
            size_t avail_in = 0;
            const uint8_t* next_in = nullptr;
            size_t avail_out = internalBuffer.size();
            uint8_t* next_out = reinterpret_cast<uint8_t*>(internalBuffer.data());

            // Compress
            if (!BrotliEncoderCompressStream(state, BROTLI_OPERATION_FINISH, &avail_in, &next_in, &avail_out, &next_out, nullptr)) {
                this->_status = STREAM_FAILURE;
                return 0;
            }

            // Count used size
            size_t used = internalBuffer.size() - avail_out;
            if (used > 0)
                dest.insert(dest.end(), internalBuffer.data(), internalBuffer.data() + used);
        }

        return dest.size();
    }

    // Creates and returns a pointer to an ICompressor object
    ICompressor* createCompressorStream(int method) {
        switch (method) {
            case COMPRESS_BROTLI: return new BrotliCompressor();
            case COMPRESS_GZIP:
            case COMPRESS_DEFLATE: return new ZlibCompressor(method);
        }
        // Base case, no compression
        return nullptr;
    }

    size_t MemoryStream::read(char* buffer, size_t maxBytes) {
        const size_t remaining = data.size() - offset;
        const size_t toRead = (std::min)(remaining, maxBytes);
        memcpy(buffer, data.c_str() + offset, toRead);
        offset += toRead;
        return toRead;
    }

    FileStream::FileStream(const std::string& path, const bool isCompressedTempFile)
        : isCompressedTempFile(isCompressedTempFile), path(path) {
        this->handle = std::ifstream(path, std::ios::binary | std::ios::ate );

        // Check if successful
        if (!this->handle.is_open()) {
            this->_status = STREAM_FAILURE;
            return;
        }

        this->_size = handle.tellg(); // Grab size of file
        handle.seekg(0, std::ios::beg); // Revert to start
    }

    FileStream::~FileStream() {
        handle.close();

        // Remove if needed (for temp files)
        if (isCompressedTempFile)
            removeTempFile(path);
    }

    size_t FileStream::read(char* buffer, size_t maxBytes) {
        if (handle.eof()) return 0;
        handle.read(buffer, maxBytes);
        return handle.gcount();
    }

}