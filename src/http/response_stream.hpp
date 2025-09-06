#ifndef __HTTP_RESPONSE_STREAM_HPP
#define __HTTP_RESPONSE_STREAM_HPP

#include <cstring>
#include <fstream>
#include <string>

#define FILESTREAM_SUCCESS 0
#define FILESTREAM_FAILURE 1

namespace http {

    class IResponseStream {
        public:
            virtual ~IResponseStream() = default;

            // Reads up to maxBytes bytes to buffer
            virtual size_t read(char* buffer, size_t maxBytes) = 0;

            // Returns the size of the entire output buffer
            virtual size_t size() const = 0;

            // Returns true or false if the stream failed to open/start
            int status() const { return _status; };
        protected:
            int _status = FILESTREAM_SUCCESS;
    };

    class FileStream : public IResponseStream {
        public:
            explicit FileStream(const std::string&);
            ~FileStream() { handle.close(); };
            size_t read(char* buffer, size_t maxBytes);
            size_t size() const { return _size; };
        private:
            std::ifstream handle;
            size_t _size;
    };

    class MemoryStream : public IResponseStream {
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