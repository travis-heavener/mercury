#include "server-ipv6.hpp"

namespace HTTP {

    int ServerV6::bindSocket() {
        // Open the socket
        this->sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

        if (this->sock < 0) {
            ERROR_LOG << "Failed to open socket (" << this->getDetailsStr() << ") on port " << this->port << std::endl;
            return SOCKET_FAILURE;
        }

        // Init socket opts
        const int optFlag = 1;
        if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set IPv6 socket opt SO_REUSEADDR (" << this->getDetailsStr() << ")." << std::endl;
            return SOCKET_FAILURE;
        }

        if (setsockopt(this->sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set IPv6 socket opt TCP_NODELAY (" << this->getDetailsStr() << ")." << std::endl;
            return SOCKET_FAILURE;
        }

        if (setsockopt(this->sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set IPv6 socket opt IPV6_V6ONLY (" << this->getDetailsStr() << ")." << std::endl;
            return SOCKET_FAILURE;
        }

        #if __linux__
            if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&optFlag, sizeof(int)) < 0) {
                ERROR_LOG << "Failed to set IPv6 socket opt SO_REUSEPORT (" << this->getDetailsStr() << ")." << std::endl;
                return SOCKET_FAILURE;
            }
        #endif

        // Bind the host address
        struct sockaddr_in6 addr;
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(this->port);
        addr.sin6_addr = in6addr_any;

        if (bind(this->sock, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
            const int err = 
            #ifdef __linux__
                errno;
            #else
                WSAGetLastError();
            #endif

            if (err == 13) { // Improve error handling for errno 13 (need sudo to listen to port)
                ERROR_LOG << "Failed to bind socket (" << this->getDetailsStr() << "), errno: " << err << ", do you have sudo perms?" << std::endl;
            } else {
                ERROR_LOG << "Failed to bind socket (" << this->getDetailsStr() << "), errno: " << err << std::endl;
            }
            return BIND_FAILURE;
        }

        return 0;
    }

}