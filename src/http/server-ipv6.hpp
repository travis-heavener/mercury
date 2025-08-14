#ifndef __HTTP_SERVER_IPV6_HPP
#define __HTTP_SERVER_IPV6_HPP

#include "server.hpp"

#define MAX_IPV6_BIND_ATTEMPTS 5 

namespace http {

    class ServerV6 : public Server {
        public:
            ServerV6(const port_t port, const bool useTLS)
                : Server(port, useTLS) {};

            // Overridden by IPv6 servers
            int bindSocket();
            bool isIPv4() const { return false; };
        private:
    };

}

#endif