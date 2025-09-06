#include "response_stream.hpp"
#include <iostream>

namespace http {

    size_t MemoryStream::read(char* buffer, size_t maxBytes) {
        const size_t remaining = data.size() - offset;
        const size_t toRead = (std::min)(remaining, maxBytes);
        memcpy(buffer, data.c_str() + offset, toRead);
        offset += toRead;
        return toRead;
    }

    FileStream::FileStream(const std::string& path) {
        this->handle = std::ifstream(path, std::ios::binary | std::ios::ate );

        // Check if successful
        if (!this->handle.is_open()) {
            this->_status = FILESTREAM_FAILURE;
            return;
        }

        this->_size = handle.tellg(); // Grab size of file
        handle.seekg(0, std::ios::beg); // Revert to start
    }

    size_t FileStream::read(char* buffer, size_t maxBytes) {
        if (handle.eof()) return 0;
        handle.read(buffer, maxBytes);
        return handle.gcount();
    }

}