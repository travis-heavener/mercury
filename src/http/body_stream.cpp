#include "body_stream.hpp"

#include <cstring>

#include "compressor_stream.hpp"
#include "../conf/conf.hpp"
#include "../logs/logger.hpp"
#include "../util/string_tools.hpp"
#include "../io/file_tools.hpp"

namespace http {

    // Adds a new byte range
    void IBodyStream::addByteRange(byte_range_t byteRange) {
        this->byteRanges.emplace_back( std::move(byteRange) );
    }

    size_t MemoryStream::read(char* buffer, size_t maxBytes) {
        // Handle byte ranges
        if (byteRangeIndex < byteRanges.size() && !byteRanges.empty() && offset > byteRanges[byteRangeIndex].second) {
            ++byteRangeIndex;
            return 0;
        }

        size_t remaining;
        if (!byteRanges.empty()) {
            if (byteRangeIndex + 1 == byteRanges.size()) return 0;

            byte_range_t& front = byteRanges[byteRangeIndex];
            if (offset < front.first) offset = front.first; // Reset the offset if needed

            size_t totalRangeSize = front.second - front.first + 1;
            remaining = totalRangeSize - (offset - front.first);
        } else {
            remaining = data.size() - offset;
        }

        size_t toRead = (std::min)(remaining, maxBytes);
        if (toRead == 0) return 0; // Skip no-op memcpy

        memcpy(buffer, data.c_str() + offset, toRead);
        offset += toRead;
        return toRead;
    }

    FileStream::FileStream(const std::string& path, const bool isTempFile)
        : isTempFile(isTempFile), path(path) {
        this->handle = std::ifstream(path, std::ios::binary | std::ios::ate );

        // Check if successful
        if (!this->handle.is_open()) {
            this->_status = STREAM_FAILURE;
            return;
        }

        originalSize = handle.tellg(); // Grab size of file
        handle.seekg(0, std::ios::beg); // Revert to start
    }

    FileStream::~FileStream() {
        handle.close();

        // Remove if needed (for temp files)
        if (isTempFile) removeTempFile(path);
    }

    size_t FileStream::size() const {
        if (this->byteRanges.empty())
            return originalSize;

        // Base case, byte ranges
        size_t s = 0;
        for (const http::byte_range_t& range : this->byteRanges)
            s += range.second - range.first + 1;
        return s;
    }

    size_t FileStream::read(char* buffer, size_t maxBytes) {
        // Handle byte ranges
        while (byteRangeIndex < byteRanges.size() && !byteRanges.empty()) {
            byte_range_t& front = byteRanges[byteRangeIndex];

            // Pop file pointer if past range
            if (handle.tellg() != -1 && static_cast<size_t>(handle.tellg()) > front.second) {
                ++byteRangeIndex;
                return 0;
            }

            // Align file pointer to range start, if needed
            if (handle.tellg() != -1 && static_cast<size_t>(handle.tellg()) < front.first) {
                handle.clear(); // Clear any EOFs
                handle.seekg(static_cast<std::streamoff>(front.first), std::ios::beg);
            }

            break;
        }

        // Verify byte ranges exist if needed
        if (byteRangeIndex == byteRanges.size() && !byteRanges.empty()) return 0;

        // Determine remaining bytes to read
        size_t remaining;
        if (byteRangeIndex < byteRanges.size() && !byteRanges.empty()) {
            byte_range_t& front = byteRanges[byteRangeIndex];
            std::streamoff currentPos = handle.tellg();
            if (currentPos == -1) return 0; // Internal error
            remaining = front.second - static_cast<size_t>(currentPos) + 1;
        } else {
            remaining = originalSize - static_cast<size_t>(handle.tellg());
        }

        size_t toRead = (std::min)(remaining, maxBytes);
        if (toRead == 0) return 0;

        handle.read(buffer, toRead);
        return static_cast<size_t>(handle.gcount());
    }

}