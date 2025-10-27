#include "compressor_stream.hpp"

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

    ZstdCompressor::ZstdCompressor() {
        // Create compressor stream
        cstream = ZSTD_createCStream();
        if (!cstream) {
            _status = STREAM_FAILURE;
            return;
        }

        // Initialize compressor stream
        size_t initResult = ZSTD_initCStream(cstream, 3);
        if (ZSTD_isError(initResult)) {
            _status = STREAM_FAILURE;
            ZSTD_freeCStream(cstream);
            cstream = nullptr;
        }
    }

    ZstdCompressor::~ZstdCompressor() {
        if (cstream != nullptr)
            ZSTD_freeCStream(cstream);
    }

    size_t ZstdCompressor::compress(const char* src, std::vector<char>& dest, const size_t size, const int) {
        if (!cstream) return 0;

        ZSTD_inBuffer in = { src, size, 0 };
        size_t totalWritten = 0;

        while (in.pos < in.size) {
            // Reserve sufficient space
            size_t chunkSize = ZSTD_CStreamOutSize();
            size_t oldSize = dest.size();
            dest.resize(oldSize + chunkSize);

            // Compress
            ZSTD_outBuffer out = { dest.data() + oldSize, chunkSize, 0 };
            size_t ret = ZSTD_compressStream(cstream, &out, &in);
            if (ZSTD_isError(ret)) {
                _status = STREAM_FAILURE;
                break;
            }

            // Resize according to space used
            dest.resize(oldSize + out.pos);
            totalWritten += out.pos;
        }

        return totalWritten;
    }

    size_t ZstdCompressor::finish(std::vector<char>& dest) {
        if (!cstream) return 0;

        size_t totalWritten = 0;
        size_t remaining = 0;

        do {
            // Reserve sufficient space
            size_t chunkSize = ZSTD_CStreamOutSize();
            size_t oldSize = dest.size();
            dest.resize(oldSize + chunkSize);

            // Compress
            ZSTD_outBuffer out = { dest.data() + oldSize, chunkSize, 0 };
            remaining = ZSTD_endStream(cstream, &out);
            if (ZSTD_isError(remaining)) {
                _status = STREAM_FAILURE;
                break;
            }

            // Resize according to space used
            dest.resize(oldSize + out.pos);
            totalWritten += out.pos;

        } while (remaining != 0);

        return totalWritten;
    }

    // Creates and returns a pointer to an ICompressor object
    ICompressor* createCompressorStream(int method) {
        switch (method) {
            case COMPRESS_ZSTD: return new ZstdCompressor();
            case COMPRESS_BROTLI: return new BrotliCompressor();
            case COMPRESS_GZIP:
            case COMPRESS_DEFLATE: return new ZlibCompressor(method);
        }
        // Base case, no compression
        return nullptr;
    }

}