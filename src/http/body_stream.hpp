#ifndef __HTTP_BODY_STREAM_HPP
#define __HTTP_BODY_STREAM_HPP

#include <fstream>
#include <queue>
#include <string>
#include <vector>

#include <brotli/encode.h>
#include <zlib.h>

#include "byte_range.hpp"

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
            int status() const { return _status; };
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

    // Creates and returns a pointer to an ICompressor object
    ICompressor* createCompressorStream(int method);

    // Base class for ResponseStreams
    class IBodyStream {
        public:
            virtual ~IBodyStream() = default;

            // Reads up to maxBytes bytes to buffer
            virtual size_t read(char* buffer, size_t maxBytes) = 0;

            // Returns the size of the entire output buffer
            virtual size_t size() const = 0;

            // Returns true or false if the stream failed to open/start
            int status() const { return _status; };

            // Returns true if the stream is already compressed
            virtual bool isPrecompressed() const { return false; };

            // Adds a new byte range
            void addByteRange(byte_range_t byteRange);

            // Returns true if there are more byte ranges left
            inline size_t getTotalNumByteRanges() const { return byteRanges.size(); };

            // Returns the byte range at index i, but does NOT check if one exists (will fail if empty)
            inline const byte_range_t& getByteRange(size_t i) const { return byteRanges[i]; };
        protected:
            int _status = STREAM_SUCCESS;
            std::vector<byte_range_t> byteRanges;
            size_t byteRangeIndex = 0;
    };

    class FileStream : public IBodyStream {
        public:
            explicit FileStream(const std::string&, const bool=false);
            ~FileStream();
            size_t read(char* buffer, size_t maxBytes);
            size_t size() const { return _size; };
            bool isPrecompressed() const { return isCompressedTempFile; };
        private:
            bool isCompressedTempFile = false;
            std::ifstream handle;
            size_t _size;
            const std::string path;
    };

    class MemoryStream : public IBodyStream {
        public:
            explicit MemoryStream(const std::string& s) : data(std::move(s)), offset(0) {};
            size_t read(char* buffer, size_t maxBytes);
            size_t size() const { return data.size(); };
        private:
            std::string data;
            size_t offset;
    };

}

#endif