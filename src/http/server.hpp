#ifndef __HTTP_SERVER_HPP
#define __HTTP_SERVER_HPP

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    // Fix inet_ntop unavailable
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #elif _WIN32_WINNT < 0x0600
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif

    #include <winsock2.h>
    #include <ws2tcpip.h>

    #define poll WSAPoll
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <unistd.h>
    #include <poll.h>
#endif

#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

#include "request.hpp"
#include "response.hpp"
#include "tls.hpp"
#include "../util/toolbox.hpp"
#include "../util/file.hpp"

#include <zlib.h>

#define SOCKET_FAILURE 1
#define BIND_FAILURE 2
#define LISTEN_FAILURE 3

typedef unsigned short u_short;
typedef unsigned int u_int;

namespace HTTP {

    class Server {
        public:
            Server(const port_t port, const u_short maxBacklog, const u_int maxBufferSize, const bool useTLS);
            virtual ~Server() { this->kill(); };

            // Overridden by IPv6 servers
            virtual int bindSocket();

            int init();
            void handleReqs();
            void kill();
            void genResponse(Request&, Response&);
        protected:
            void clearBuffer();
            size_t readClientSock();
            void writeClientSock(const char*, const size_t);
            int closeSocket(const int);

            void extractClientIP(struct sockaddr_storage&, char*) const;
            bool waitForClientData(const int);
            int acceptConnection(struct sockaddr_storage&, socklen_t&);

            const port_t port;
            int sock = -1;
            int c_sock = -1;

            const u_short maxBacklog;
            const u_int maxBufferSize;
            char* readBuffer;

            // OpenSSL
            bool useTLS;
            SSL* pSSL = nullptr;
            SSL_CTX* pSSL_CTX = nullptr;
    };

}

#endif