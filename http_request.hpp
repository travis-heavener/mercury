#ifndef __HTTP_REQUEST_HPP
#define __HTTP_REQUEST_HPP

#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>

enum HTTP_METHOD {
    GET = 0
};

class HTTPRequest {
    public:
        HTTPRequest(const char*, const char*);

        const std::string* getHeader(const std::string&) const;
        const std::string getIPStr() const { return ipStr; };
        HTTP_METHOD getMethod() const { return method; };
        const std::string& getMethodStr() const { return methodStr; };
        const std::string& getPathStr() const { return pathStr; };
        const std::string& getBody() const { return body; };
    private:
        std::unordered_map<std::string, std::string> headers;

        std::string ipStr;

        HTTP_METHOD method;
        std::string methodStr;

        std::string pathStr;

        std::string httpVersionStr;

        std::string body;
};

#endif