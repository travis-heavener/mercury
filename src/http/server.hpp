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
    #define ssize_t SSIZE_T
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <unistd.h>
    #include <poll.h>
#endif

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>

#include "request.hpp"
#include "response.hpp"
#include "tls.hpp"
#include "../util/toolbox.hpp"
#include "../util/file.hpp"
#include "../logs/logger.hpp"

#include <zlib.h>

#define SOCKET_UNSET -1

#define SOCKET_FAILURE 1
#define BIND_FAILURE 2
#define LISTEN_FAILURE 3

#define KEEP_ALIVE_TIMEOUT_MS 5000
#define KEEP_ALIVE_MAX_REQ 100

typedef unsigned short u_short;
typedef unsigned int u_int;

namespace HTTP {

    class Server {
        public:
            Server(const port_t port, const bool useTLS) :
                port(port), maxBacklog(conf::MAX_REQUEST_BACKLOG), maxBufferSize(conf::MAX_REQUEST_BUFFER),
                useTLS(useTLS) {};
            virtual ~Server() = default;

            // Overridden by IPv6 servers
            virtual int bindSocket();
            virtual bool isIPv4() const { return true; };

            int init();
            void acceptLoop();
            void handleReqs(const int, const std::string);
            void kill();
            void genResponse(Request&, Response&);
        protected:
            // Socket methods
            void clearBuffer(char*);
            ssize_t readClientSock(char*, const int, SSL*);
            ssize_t writeClientSock(const int, SSL*, std::string&);
            int closeSocket(const int);
            int closeClientSocket(const int, SSL*);

            // Request loop helper methods
            void extractClientIP(struct sockaddr_storage&, char*) const;
            ssize_t waitForClientData(struct pollfd&, const int);
            int acceptConnection(struct sockaddr_storage&, socklen_t&);

            // Client socket tracking methods
            void trackClient(const int);
            void untrackClient(const int);

            std::string getDetailsStr() const;

            // Protected fields
            const port_t port;
            int sock = SOCKET_UNSET;
            std::unordered_set<int> clientSocks;

            const u_short maxBacklog;
            const u_int maxBufferSize;

            // OpenSSL
            bool useTLS;
            SSL_CTX* pSSL_CTX = nullptr;
    };

}

#endif