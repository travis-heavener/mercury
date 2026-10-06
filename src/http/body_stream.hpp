#ifndef __HTTP_BODY_STREAM_HPP
#define __HTTP_BODY_STREAM_HPP

#include <fstream>
#include <queue>
#include <string>
#include <vector>

#include "tools.hpp"

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

            // Adds a new byte range
            void addByteRange(byte_range_t byteRange);

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
        private:
            bool isTempFile = false;
            std::ifstream handle;
            size_t originalSize = 0;
            const std::string path;
    };

    class MemoryStream : public IBodyStream {
        public:
            explicit MemoryStream(const std::string& s) : data(std::move(s)), offset(0) {};
            size_t read(char* buffer, size_t maxBytes);
            size_t size() const;
        private:
            std::string data;
            size_t offset;
    };

}

#endif