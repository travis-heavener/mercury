#ifndef __HTTP_SERVER_IPV6_HPP
#define __HTTP_SERVER_IPV6_HPP

#include "server.hpp"

namespace HTTP {

    class ServerV6 : public Server {
        public:
            ServerV6(const port_t port, const u_short maxBacklog, const u_int maxBufferSize, const bool useTLS)
                : Server(port, maxBacklog, maxBufferSize, useTLS) {};

            // Overridden by IPv6 servers
            int bindSocket();
        private:
    };

}

#endif