#ifndef __HTTP_REQUEST_HPP
#define __HTTP_REQUEST_HPP

#include <chrono>

#include "../pch/common.hpp"

#include "response.hpp"
#include "exception.hpp"
#include "../util/toolbox.hpp"

namespace http {

    enum METHOD {
        GET = 0,
        HEAD = 1,
        OPTIONS = 2,
        POST = 3,
        PUT = 4,
        DEL = 5,
        PATCH = 6,
        UNKNOWN = 999
    };

    class Request {
        public:
            Request(const char*, std::string, const bool);

            const std::string* getHeader(std::string) const;
            inline const std::string getIPStr() const { return ipStr; };
            inline METHOD getMethod() const { return method; };
            inline const std::string& getMethodStr() const { return methodStr; };
            inline const std::string& getRawPathStr() const { return rawPathStr; };
            inline const std::string& getPathStr() const { return pathStr; };
            inline const std::string& getBody() const { return body; };
            inline const std::string& getVersion() const { return httpVersionStr; };

            inline const std::unordered_map<std::string, std::string>& getHeaders() const { return headers; };

            bool isMIMEAccepted(const std::string&) const;
            bool isEncodingAccepted(const std::string&) const;
            inline bool usesHTTPS() const { return isHTTPS; };
            inline bool isURIBad() const { return hasBadURI; };
            inline bool getHasExplicitlyDefinedHTTPVersion0_9() const { return hasExplicitlyDefinedHTTPVersion0_9; };

            bool isFileValid(Response& response, const File& file) const;
            bool isInDocumentRoot(Response&, const std::string&) const;
        private:
            void setStatusMaybeErrorDoc(Response& response, const int status) const;

            std::unordered_map<std::string, std::string> headers;

            std::unordered_set<std::string> acceptedMIMETypes;
            std::unordered_set<std::string> acceptedEncodings;

            std::string ipStr;
            bool isHTTPS;

            METHOD method;
            std::string methodStr;

            std::string pathStr;
            std::string rawPathStr; // The path BEFORE URI decoding
            bool hasBadURI = false; // Set to true if the decodeURI method fails, handled by Response object
            bool hasExplicitlyDefinedHTTPVersion0_9; // Set to true if the status line has HTTP/0.9 explicitly in it (not allowed)

            std::string httpVersionStr;

            std::string body;
    };

}

#endif