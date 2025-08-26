#ifndef __HTTP_RESPONSE_HPP
#define __HTTP_RESPONSE_HPP

#include <algorithm>

#include "../pch/common.hpp"
#include "../io/file.hpp"
#include "../util/toolbox.hpp"

namespace http {

    class Response {
        public:
            Response(const std::string&);

            void setStatus(const uint16_t statusCode);
            inline uint16_t getStatus() const { return statusCode; };
            void setHeader(std::string name, const std::string& value);

            int loadBodyFromErrorDoc(const uint16_t statusCode);
            int loadBodyFromFile(File& file);
            int compressBody(const int compressionType);
            inline void setBody(const std::string& body) { this->body = body; }

            inline void setContentType(const std::string& type) {
                this->setHeader("Content-Type", type);
            }
            const std::string getContentType() const;

            void loadToBuffer(std::string& buffer, const bool omitBody);
        private:
            std::string httpVersion;
            uint16_t statusCode;
            std::string body;

            std::unordered_map<std::string, std::string> headers;
    };

};

#endif