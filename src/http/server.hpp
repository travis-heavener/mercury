#ifndef __HTTP_SERVER_HPP
#define __HTTP_SERVER_HPP

#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

#include "request.hpp"
#include "../util/toolbox.hpp"
#include "../util/file.hpp"

#include <zlib.h>

typedef unsigned short u_short;
typedef unsigned int u_int;

namespace HTTP {

    class Server {
        public:
            Server(const std::string& host, const port_t port, const u_short maxBacklog, const u_int maxBufferSize);
            ~Server() { this->kill(); };

            int init();
            void handleReqs();
            void kill();
            void genResponse(std::string&, const Request&);
        private:
            void clearBuffer();

            const std::string host;
            const port_t port;
            int sock = -1;
            int c_sock = -1;
            int node_sock = -1;

            const u_short maxBacklog;
            const u_int maxBufferSize;
            char* readBuffer;
    };

}

#endif