#ifndef __HTTP_BODY_STREAM_HPP
#define __HTTP_BODY_STREAM_HPP

#include <fstream>
#include <queue>
#include <string>
#include <vector>

#include "http_tools.hpp"

#define STREAM_SUCCESS 0
#define STREAM_FAILURE 1

namespace http {

    // Base class for ResponseStreams
    class IBodyStream {
        public:
            virtual ~IBodyStream() = default;

            // Reads up to maxBytes bytes to buffer
            virtual size_t read(char* buffer, size_t maxBytes) = 0;

            // Returns the size of the entire output buffer
            inline virtual size_t size() const = 0;

            // Returns true or false if the stream failed to open/start
            inline int status() const { return _status; };

            // Returns true if the stream is already compressed
            inline virtual bool isPrecompressed() const { return false; };

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
            size_t size() const;
            inline bool isPrecompressed() const { return isTempFile; };
        private:
            bool isTempFile = false;
            std::ifstream handle;
            size_t originalSize;
            const std::string path;
    };

    class MemoryStream : public IBodyStream {
        public:
            explicit MemoryStream(const std::string& s) : data(std::move(s)), offset(0) {};
            size_t read(char* buffer, size_t maxBytes);
            inline size_t size() const { return data.size(); };
        private:
            std::string data;
            size_t offset;
    };

}

#endif