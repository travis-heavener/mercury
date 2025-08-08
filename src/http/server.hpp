#ifndef __HTTP_SERVER_HPP
#define __HTTP_SERVER_HPP

#ifdef _WIN32
    #include "../winheader.hpp"
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <unistd.h>
    #include <poll.h>
#endif

#include <shared_mutex>

#include "../pch/common.hpp"

#include "request.hpp"
#include "response.hpp"
#include "tls.hpp"
#include "../io/file.hpp"
#include "../util/toolbox.hpp"
#include "../util/thread_pool.hpp"
#include "../logs/logger.hpp"

#include <zlib.h>

#define SOCKET_UNSET -1

#define SOCKET_FAILURE 1
#define BIND_FAILURE 2
#define LISTEN_FAILURE 3

#define KEEP_ALIVE_TIMEOUT_MS 3000
#define KEEP_ALIVE_MAX_REQ 100

namespace HTTP {

    class Server : public std::enable_shared_from_this<Server> {
        public:
            Server(const port_t port, const bool useTLS);
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
            inline int acceptConnection(struct sockaddr_storage&, socklen_t&);

            // Client socket tracking methods
            inline void trackClient(const int);
            inline void untrackClient(const int);

            // For logs
            std::string getDetailsStr() const;

            // Protected fields
            const port_t port;
            int sock = SOCKET_UNSET;
            std::unordered_set<int> clientSocks;

            const unsigned short maxBacklog;
            const unsigned int maxBufferSize;

            // For multithreading
            std::shared_mutex clientsMutex;
            ThreadPool threadPool;

            // OpenSSL
            bool useTLS;
            SSL_CTX* pSSL_CTX = nullptr;
    };

}

#endif