#ifndef __HTTP_REQUEST_HPP
#define __HTTP_REQUEST_HPP

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "response.hpp"
#include "exception.hpp"
#include "../util/toolbox.hpp"

// Used by Allow headers, just simplified to one macro-def here
#define ALLOWED_METHODS "GET, HEAD, OPTIONS"

namespace HTTP {

    enum METHOD {
        GET = 0,
        HEAD = 1,
        OPTIONS = 2,
        UNKNOWN = 999
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
            const std::string& getVersion() const { return httpVersionStr; };

            void loadResponse(Response&) const;

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
            std::string rawPathStr; // The path BEFORE URI decoding

            std::string httpVersionStr;

            std::string body;
    };

}

#endif