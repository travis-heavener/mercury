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

#include <atomic>
#include <memory>
#include <shared_mutex>

#include "request.hpp"
#include "response.hpp"
#include "tls.hpp"
#include "../util/thread_pool.hpp"

#define SOCKET_UNSET -1

#define SOCKET_FAILURE 1
#define BIND_FAILURE 2
#define LISTEN_FAILURE 3

typedef unsigned short port_t;

// Helper function for binding socket options
#define bindSocketOpt(pServer, sock, family, option, value, logErrors) \
    if (setsockopt(sock, family, option, (const char*)&value, sizeof(int)) < 0) { \
        if (logErrors) \
            ERROR_LOG << "Failed to set socket opt " #option "(" << *pServer << ")." << std::endl; \
        return SOCKET_FAILURE; \
    }

namespace http {

    class Server : public std::enable_shared_from_this<Server> {
        public:
            Server(const port_t port, const bool useTLS);
            virtual ~Server() = default;

            // Overridden by IPv6 servers
            virtual int bindSocket();
            inline virtual bool isIPv4() const { return true; };
            inline bool usesTLS() const { return useTLS; };
            inline port_t getPort() const { return port; };

            int init();
            void acceptLoop();
            void handleReqs(const int, const std::string);
            void kill();
            std::unique_ptr<Response> genResponse(Request&);
            void getUsageInfo(size_t& usedThreads, size_t& totalThreads, size_t& pendingConnections);
        protected:
            // Socket methods
            void clearBuffer(std::vector<char>&);
            ssize_t readClientSock(char*, const int, SSL*);
            ssize_t writeClientSock(const int, SSL*, const char*, const size_t);
            int closeSocket(const int);
            int closeClientSocket(const int, SSL*);
            void drainClientSocket(const int, SSL*, size_t);

            // Request loop helper methods
            void extractClientIP(struct sockaddr_storage&, char*) const;
            ssize_t waitForClientData(struct pollfd&, const int);
            int acceptConnection(struct sockaddr_storage&, socklen_t&);

            // Client socket tracking methods
            void trackClient(const int);
            void untrackClient(const int);

            // Protected fields
            const port_t port;
            int sock = SOCKET_UNSET;
            std::unordered_set<int> clientSocks;

            // For multithreading
            std::shared_mutex clientsMutex;
            ThreadPool threadPool;

            // OpenSSL
            bool useTLS;
            SSL_CTX* pSSL_CTX = nullptr;

            // Used to gracefully close acceptLoop threads
            std::atomic<bool> isExiting{false};
    };

    // For logs
    std::ostream& operator<<(std::ostream& os, const Server& server);

}

#endif