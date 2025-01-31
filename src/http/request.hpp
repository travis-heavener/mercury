#ifndef __HTTP_REQUEST_HPP
#define __HTTP_REQUEST_HPP

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../util/toolbox.hpp"

#define ALLOWED_METHODS "GET"

namespace HTTP {

    enum METHOD {
        GET = 0
    };

    class Request {
        public:
            Request(const char*, const char*);

            const std::string* getHeader(const std::string&) const;
            const std::string getIPStr() const { return ipStr; };
            METHOD getMethod() const { return method; };
            const std::string& getMethodStr() const { return methodStr; };
            const std::string& getPathStr() const { return pathStr; };
            const std::string& getBody() const { return body; };

            bool isMIMEAccepted(const std::string&) const;
            bool isEncodingAccepted(const std::string&) const;
        private:
            std::unordered_map<std::string, std::string> headers;

            std::unordered_set<std::string> acceptedMIMETypes;
            std::unordered_set<std::string> acceptedEncodings;

            std::string ipStr;

            METHOD method;
            std::string methodStr;

            std::string pathStr;

            std::string httpVersionStr;

            std::string body;
    };

}

#endif