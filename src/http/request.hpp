#ifndef __HTTP_REQUEST_HPP
#define __HTTP_REQUEST_HPP

#include "../pch/common.hpp"

#include "response.hpp"

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

    // Used to extract headers and status info from partial request
    typedef std::unordered_map<std::string, std::string> headers_map_t;
    void loadEarlyHeaders(headers_map_t&, const std::string&);

    typedef std::vector<std::pair<size_t, size_t>> byte_range_t;

    class Request {
        public:
            Request(headers_map_t& headers, const std::string&, std::string, const bool, const bool);

            const std::string* getHeader(std::string) const;
            inline const std::string getIPStr() const { return ipStr; };
            inline METHOD getMethod() const { return method; };
            inline const std::string& getMethodStr() const { return methodStr; };
            inline const std::string& getRawPathStr() const { return rawPathStr; };
            inline const std::string& getPathStr() const { return pathStr; };
            inline const std::string& getBody() const { return body; };
            inline const std::string& getVersion() const { return httpVersionStr; };
            inline int getCompressMethod() const { return compressMethod; };

            inline const headers_map_t& getHeaders() const { return headers; };
            inline const std::vector<std::pair<size_t, size_t>>& getByteRanges() const { return byteRanges; };

            bool isMIMEAccepted(const std::string&) const;
            bool isEncodingAccepted(const std::string&) const;
            inline bool usesHTTPS() const { return isHTTPS; };
            inline bool isContentTooLarge() const { return _isContentTooLarge; };
            inline bool has400Error() const { return _has400Error; };
            inline bool hasExplicitHTTP0_9() const { return _hasExplicitHTTP0_9; };

            bool isFileValid(Response& response, const File& file) const;
            bool isInDocumentRoot(Response&, const std::string&) const;
        private:
            void setStatusMaybeErrorDoc(Response& response, const int status) const;

            headers_map_t& headers;

            std::unordered_set<std::string> acceptedMIMETypes;
            std::unordered_set<std::string> acceptedEncodings;
            byte_range_t byteRanges;

            std::string ipStr;
            bool isHTTPS;
            bool _isContentTooLarge; // If true, sends 413 Content Too Large

            METHOD method;
            std::string methodStr;

            std::string pathStr;
            std::string rawPathStr; // The path BEFORE URI decoding
            bool _hasExplicitHTTP0_9; // Set to true if the status line has HTTP/0.9 explicitly in it (not allowed)
            bool _has400Error = false; // If true, handle as 400 Bad Request

            std::string httpVersionStr;

            std::string body;
            int compressMethod = NO_COMPRESS;
    };

}

#endif