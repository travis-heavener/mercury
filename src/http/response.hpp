#ifndef __HTTP_RESPONSE_HPP
#define __HTTP_RESPONSE_HPP

#include <algorithm>
#include <functional>
#include <memory>

#include "../pch/common.hpp"
#include "../io/file.hpp"
#include "../util/toolbox.hpp"
#include "response_stream.hpp"

namespace http {

    class Response {
        public:
            Response(const std::string&);

            void setStatus(const uint16_t statusCode);
            inline uint16_t getStatus() const { return httpVersion == "HTTP/0.9" ? 0 : statusCode; };
            void setHeader(std::string name, const std::string& value);
            void clearHeader(std::string);

            int loadBodyFromErrorDoc(const uint16_t statusCode);
            int loadBodyFromFile(File& file);
            int compressBody(const int compressionType);
            inline void setBodyStream(std::unique_ptr<IResponseStream> p) { pBodyStream = std::move(p); };

            inline void setContentType(const std::string& type) {
                this->setHeader("Content-Type", type);
            }
            const std::string getContentType() const;
            int getContentLength() const;

            ssize_t loadToBuffer(const bool omitBody, std::function<ssize_t(const char*, const size_t)>&);
        private:
            std::string httpVersion;
            uint16_t statusCode;
            std::unique_ptr<IResponseStream> pBodyStream;

            std::unordered_map<std::string, std::string> headers;
    };

};

#endif