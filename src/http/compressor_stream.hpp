#ifndef __HTTP_COMPRESSOR_STREAM_HPP
#define __HTTP_COMPRESSOR_STREAM_HPP

#include <vector>

#include <brotli/encode.h>
#include <zstd.h>
#include <zlib.h>

#include "tools.hpp"

#define STREAM_SUCCESS 0
#define STREAM_FAILURE 1

namespace http {

    // Base class for stream compressors
    class ICompressor {
        public:
            virtual ~ICompressor() = default;

            // Compresses the given text to the output buffer
            virtual size_t compress(const char* src, std::vector<char>& dest, const size_t size, const int flags=0) = 0;

            // Finishes compression and returns any remaining compression info
            virtual size_t finish(std::vector<char>& dest) = 0;

            // Returns true or false if the stream failed to open/start
            inline int status() const { return _status; };
        protected:
            int _status = STREAM_SUCCESS;
    };

    class ZlibCompressor : public ICompressor {
        public:
            ZlibCompressor(const int method);
            ~ZlibCompressor();
            size_t compress(const char* src, std::vector<char>& dest, const size_t size, const int flags=Z_NO_FLUSH);
            size_t finish(std::vector<char>& dest);
        private:
            std::vector<char> internalBuffer;
            z_stream stream{};
    };

    class BrotliCompressor : public ICompressor {
        public:
            BrotliCompressor();
            ~BrotliCompressor();
            size_t compress(const char* src, std::vector<char>& dest, const size_t size, const int flags);
            size_t finish(std::vector<char>& dest);
        private:
            BrotliEncoderState* state = nullptr;
            std::vector<char> internalBuffer;
    };

    class ZstdCompressor : public ICompressor {
        public:
            ZstdCompressor();
            ~ZstdCompressor();
            size_t compress(const char* src, std::vector<char>& dest, const size_t size, const int flags=0);
            size_t finish(std::vector<char>& dest);
        private:
            ZSTD_CStream* cstream = nullptr;
    };

    // Creates and returns a pointer to an ICompressor object
    ICompressor* createCompressorStream(int method);

}

#endif