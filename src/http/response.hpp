#ifndef __HTTP_RESPONSE_HPP
#define __HTTP_RESPONSE_HPP

#include <algorithm>
#include <string>
#include <unordered_map>

#include "../util/file.hpp"
#include "../util/toolbox.hpp"

namespace HTTP {

    class Response {
        public:
            Response(const std::string& httpVersion);

            void setStatus(const uint16_t statusCode);
            void setHeader(std::string name, const std::string& value);

            void loadBodyFromErrorDoc(const uint16_t statusCode);
            int loadBodyFromFile(File& file);
            int compressBody(const int compressionType);

            void setContentType(const std::string&);
            const std::string getContentType() const;

            void loadToBuffer(std::string& buffer);
        private:
            std::string httpVersion;
            uint16_t statusCode;
            std::string body;

            std::unordered_map<std::string, std::string> headers;
    };

};

#endif