#ifndef __HTTP_SERVER_HPP
#define __HTTP_SERVER_HPP

#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

#include "request.hpp"
#include "../toolbox.hpp"
#include "../file.hpp"

#include <zlib.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <unistd.h>
#endif

#define MAX_BACKLOG 25
#define MAX_READ_BUFFER 1024 * 16

#define SOCKET_FAILURE 1
#define BIND_FAILURE 2
#define LISTEN_FAILURE 3


namespace HTTP {

    class Server {
        public:
            Server(const std::string& host, const port_t port) : host(host), port(port) {};
            ~Server() { this->kill(); };

            int init();
            void handleReqs();
            void kill();
            void genResponse(std::string&, const Request&);
        private:
            const std::string host;
            const port_t port;
            int sock;
            int c_sock;

            char readBuffer[MAX_READ_BUFFER];
    };

}

#endif