#ifndef __HTTP_FASTCGI_CLIENT_HPP
#define __HTTP_FASTCGI_CLIENT_HPP

#include <cstdint>
#include <cstring>
#include <iostream>

#ifdef _WIN32
    #include "../../winheader.hpp"
#else
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

#include "../request.hpp"
#include "../response.hpp"

#include "../../io/file.hpp"
#include "../../pch/common.hpp"

#define FASTCGI_HOST "127.0.0.1"

namespace http {

    namespace fcgi {

        void handlePHPRequest(const File& file, const Request& req, Response& res);

        // FCGI Header struct
        typedef struct {
            uint8_t  version;
            uint8_t  type;
            uint16_t requestId;
            uint16_t contentLength;
            uint8_t  paddingLength;
            uint8_t  reserved;
        } Header;

        // FCGI beginning of request body struct
        typedef struct {
            uint16_t role;
            uint8_t  flags;
            uint8_t  reserved[5];
        } BeginRequestBody;

        class Client {
            public:
                Client(const std::string& host, int port)
                    : sock(-1), host(host), port(port) {};
                ~Client() { close(); }

                bool connectServer();
                void close();

                bool beginRequest(uint16_t requestId=1);
                bool sendParam(const std::string& key, const std::string& value, uint16_t requestId=1);
                bool endParams(uint16_t requestId=1);
                bool sendStdin(const std::string& body, uint16_t requestId=1);
                bool endStdin(uint16_t requestId=1);

                std::string readResponse();
            private:
                bool sendRecord(uint8_t type, uint16_t requestId, const std::vector<uint8_t>& content);
                std::vector<uint8_t> makeNameValuePair(const std::string& name, const std::string& value);

                int sock;
                std::string host;
                int port;

                Header fcgiHeader;
                BeginRequestBody fgciBeginRequestBody;
        };

    }

}

#endif