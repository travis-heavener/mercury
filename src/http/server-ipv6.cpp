#include "server-ipv6.hpp"

namespace HTTP {

    int bindSocketOpts(ServerV6& server, int& sock, const bool logErrors) {
        if (sock < 0) {
            if (logErrors)
                ERROR_LOG << "Failed to open socket (" << server << ") on port " << server.getPort() << std::endl;
            return SOCKET_FAILURE;
        }

        // Init socket opts
        const int optFlag = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optFlag, sizeof(int)) < 0) {
            if (logErrors)
                ERROR_LOG << "Failed to set IPv6 socket opt SO_REUSEADDR (" << server << ")." << std::endl;
            return SOCKET_FAILURE;
        }

        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&optFlag, sizeof(int)) < 0) {
            if (logErrors)
                ERROR_LOG << "Failed to set IPv6 socket opt TCP_NODELAY (" << server << ")." << std::endl;
            return SOCKET_FAILURE;
        }

        if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&optFlag, sizeof(int)) < 0) {
            if (logErrors)
                ERROR_LOG << "Failed to set IPv6 socket opt IPV6_V6ONLY (" << server << ")." << std::endl;
            return SOCKET_FAILURE;
        }

        #if __linux__
            if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&optFlag, sizeof(int)) < 0) {
                if (logErrors)
                    ERROR_LOG << "Failed to set IPv6 socket opt SO_REUSEPORT (" << server << ")." << std::endl;
                return SOCKET_FAILURE;
            }
        #endif

        // Base case, success
        return 0;
    }

    int ServerV6::bindSocket() {
        // Retry a few times to bind if failed
        int bindAttempts = 0;
        int lastErrno = -1;
        while (++bindAttempts <= MAX_IPV6_BIND_ATTEMPTS) {
            // Open the socket
            this->sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

            // Bind socket options
            const bool isLastAttempt = bindAttempts == MAX_IPV6_BIND_ATTEMPTS;
            const int optsStatus = bindSocketOpts(*this, this->sock, isLastAttempt);

            if (optsStatus == 0) {
                // Bind the host address
                struct sockaddr_in6 addr = {};
                addr.sin6_family = AF_INET6;
                addr.sin6_port = htons(this->port);
                addr.sin6_addr = in6addr_any;

                // If bound properly, exit early
                if (bind(this->sock, (const struct sockaddr*)&addr, sizeof(addr)) >= 0)
                    return 0;

                // Otherwise, set the errno for failure to bind
                #ifdef __linux__
                    lastErrno = errno;
                #else
                    lastErrno = WSAGetLastError();
                #endif
            } else if (isLastAttempt) {
                return optsStatus;
            }

            // Otherwise, close the socket
            this->closeSocket(this->sock);
            this->sock = SOCKET_UNSET;
        }

        // Only reaches here if bind failure
        if (lastErrno == 13) { // Improve error handling for errno 13 (need sudo to listen to port)
            ERROR_LOG << "Failed to bind socket (" << *this << "), errno: " << lastErrno << ", do you have sudo perms?" << std::endl;
        } else {
            ERROR_LOG << "Failed to bind socket (" << *this << "), errno: " << lastErrno << std::endl;
        }
        return BIND_FAILURE;
    }

}