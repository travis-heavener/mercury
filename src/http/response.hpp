#ifndef __HTTP_RESPONSE_HPP
#define __HTTP_RESPONSE_HPP

#include <algorithm>
#include <functional>
#include <memory>

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include "../pch/common.hpp"
#include "../io/file.hpp"
#include "../util/string_tools.hpp"
#include "body_stream.hpp"

// The minimum size for a body to be compressed
#define MIN_COMPRESSION_SIZE 750

namespace http {

    class Response {
        public:
            Response(const std::string&);

            void setStatus(const uint16_t statusCode);
            inline uint16_t getStatus() const { return httpVersion == "HTTP/0.9" ? 0 : statusCode; };
            void setHeader(std::string name, const std::string& value);
            void clearHeader(std::string);
            void setCompressMethod(const int compressMethod);

            int loadBodyFromErrorDoc(const uint16_t statusCode);
            int loadBodyFromFile(File& file);
            inline void setBodyStream(std::unique_ptr<IBodyStream> p) { pBodyStream = std::move(p); };

            inline void setContentType(const std::string& type) {
                this->setHeader("Content-Type", type);
            }
            const std::string getContentType() const;
            int getContentLength() const;

            ssize_t beginStreamingBody(const bool isHTMLAccepted, const bool omitBody, std::function<ssize_t(const char*, const size_t)>&);
        private:
            bool precompressBody();

            std::string httpVersion;
            uint16_t statusCode;
            std::unique_ptr<IBodyStream> pBodyStream;
            int compressMethod = NO_COMPRESS;

            std::unordered_map<std::string, std::string> headers;
    };

};

#endif