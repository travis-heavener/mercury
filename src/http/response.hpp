#ifndef __HTTP_RESPONSE_HPP
#define __HTTP_RESPONSE_HPP

#include <functional>
#include <memory>

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include "../pch/common.hpp"
#include "../io/file.hpp"
#include "../util/string_tools.hpp"
#include "body_stream.hpp"
#include "http_tools.hpp"

// The minimum size for a body to be compressed
#define MIN_COMPRESSION_SIZE 750

namespace http {

    class Response {
        public:
            Response(const std::string& httpVersion);

            inline void setStatus(const uint16_t statusCode) { this->statusCode = statusCode; };
            inline uint16_t getStatus() const { return httpVersion == "HTTP/0.9" ? 0 : statusCode; };

            void setHeader(std::string name, const std::string& value);
            void clearHeader(std::string name);
            inline void clearHeaders() { headers.clear(); };
            void setCompressMethod(const int compressMethod);

            int loadBodyFromErrorDoc(const uint16_t statusCode);
            int loadBodyFromFile(File& file);
            inline void setBodyStream(std::unique_ptr<IBodyStream> p) { pBodyStream = std::move(p); };

            inline void setContentType(const std::string& type) {
                this->setHeader("Content-Type", type);
            }
            const std::string getContentType() const;

            size_t getContentLength() const;

            ssize_t streamBody(const bool isHTMLAccepted, const bool omitBody, std::function<ssize_t(const char*, const size_t)>&);

            // Returns true if the ranges are valid, false otherwise
            bool extendByteRanges(const std::vector<byte_range_t>& byteRanges);
        private:
            bool precompressBody();

            std::string httpVersion;
            uint16_t statusCode;
            std::unique_ptr<IBodyStream> pBodyStream;
            int compressMethod = NO_COMPRESS;

            std::unordered_map<std::string, std::string> headers;

            // For precompressed bodies
            std::vector<byte_range_t> originalByteRanges;
            size_t originalBodySize = 0;
            size_t totalByteRangeSize = 0; // The total size of all the byte range data
    };

};

#endif