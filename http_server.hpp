#ifndef __HTTP_SERVER_HPP
#define __HTTP_SERVER_HPP

#include <iostream>
#include <string>

#include "http_request.hpp"
#include "toolbox.hpp"
#include "file.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_BACKLOG 25
#define MAX_READ_BUFFER 1024 * 16

#define SOCKET_FAILURE 1
#define BIND_FAILURE 2
#define LISTEN_FAILURE 3

typedef unsigned short port_t;

class HTTPServer {
    public:
        HTTPServer(const std::string& host, const port_t port) : host(host), port(port) {};
        ~HTTPServer() { this->kill(); };

        int init();
        void handleReqs();
        void kill();
        void genResponse(std::string&, const HTTPRequest&);
    private:
        const std::string host;
        const port_t port;
        int sock;
        int c_sock;

        char readBuffer[MAX_READ_BUFFER];
};

#endif