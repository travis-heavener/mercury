#include "server.hpp"

namespace HTTP {

    Server::Server(const port_t port, const u_short maxBacklog, const u_int maxBufferSize, const bool useTLS)
                : port(port), maxBacklog(maxBacklog), maxBufferSize(maxBufferSize), useTLS(useTLS) {
        // Create request buffer
        this->readBuffer = new char[this->maxBufferSize];
    };

    void Server::kill() {
        // Close sockets
        if (this->c_sock != -1 && !this->closeSocket(this->c_sock)) {
            ACCESS_LOG << "Client socket closed." << '\n';
            this->c_sock = -1;
        }

        if (this->sock != -1 && !this->closeSocket(this->sock)) {
            ACCESS_LOG << "Socket closed." << '\n';
            this->sock = -1;
        }

        // Free read buffer
        delete[] this->readBuffer;

        // Free SSL ptrs
        if (this->useTLS)
            SSL_CTX_free(this->pSSL_CTX);
    }

    int Server::bindSocket() {
        // Open the socket
        this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (this->sock < 0) {
            ERROR_LOG << "Failed to open socket on port " << this->port << std::endl;
            return SOCKET_FAILURE;
        }

        // Init socket opts
        const int optFlag = 1;
        if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set socket opt SO_REUSEADDR." << std::endl;
            return SOCKET_FAILURE;
        }

        if (setsockopt(this->sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set socket opt TCP_NODELAY." << std::endl;
            return SOCKET_FAILURE;
        }

        #if __linux__
            if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&optFlag, sizeof(int)) < 0) {
                ERROR_LOG << "Failed to set socket opt SO_REUSEPORT." << std::endl;
                return SOCKET_FAILURE;
            }
        #endif

        // Bind the host address
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(this->port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(this->sock, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
            const int err = 
            #ifdef __linux__
                errno;
            #else
                WSAGetLastError();
            #endif

            if (err == 13) { // Improve error handling for errno 13 (need sudo to listen to port)
                ERROR_LOG << "Failed to bind socket (errno: " << err << ", do you have sudo perms?)" << std::endl;
            } else {
                ERROR_LOG << "Failed to bind socket (errno: " << err << ')' << std::endl;
            }
            return BIND_FAILURE;
        }

        return 0;
    }

    // Initialize the socket
    int Server::init() {
        // Bind socket
        const int bindStatus = this->bindSocket();
        if (bindStatus != 0) return bindStatus;

        // Listen to socket
        if (listen(this->sock, this->maxBacklog) < 0) {
            ERROR_LOG << "Failed to listen to socket (errno: " << errno << ')' << '\n';
            return LISTEN_FAILURE;
        }

        // Init TLS
        if (this->useTLS) {
            if ((this->pSSL_CTX = initTLSContext()) == nullptr) {
                ERROR_LOG << "Failed to init an SSL context.\n";
                return BIND_FAILURE;
            }
        }

        ACCESS_LOG << "Listening to traffic on port " << this->port << '.' << '\n';
        return 0;
    }

    size_t Server::readClientSock() {
        if (this->useTLS)
            return SSL_read(this->pSSL, this->readBuffer, this->maxBufferSize);
        else
            return recv(this->c_sock, this->readBuffer, this->maxBufferSize, 0);
    }

    void Server::writeClientSock(const char* pBuffer, const size_t bufferSize) {
        if (this->useTLS) {
            SSL_write(this->pSSL, pBuffer, bufferSize);
        } else {
            send(this->c_sock, pBuffer, bufferSize, 0);
        }
    }

    int Server::closeSocket(const int sock) {
        // Cleanup TLS
        if (this->useTLS && this->pSSL != nullptr) {
            SSL_free(this->pSSL);
            this->pSSL = nullptr;
        }

        #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            shutdown(sock, SD_BOTH);
            return closesocket(sock);
        #else
            return close(sock);
        #endif
    }

    void Server::extractClientIP(struct sockaddr_storage& clientAddr, char* clientIPStr) const {
        void* addrPtr = nullptr;
        int afType = ((struct sockaddr*)&clientAddr)->sa_family;

        if (afType == AF_INET6) {
            addrPtr = &(((struct sockaddr_in6*)&clientAddr)->sin6_addr);
        } else {
            addrPtr = &(((struct sockaddr_in*)&clientAddr)->sin_addr);
        }

        #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            if (afType == AF_INET) {
                strcpy(clientIPStr, inet_ntoa(*(struct in_addr*)addrPtr));
            } else {
                inet_ntop(AF_INET6, addrPtr, clientIPStr, sizeof(clientIPStr));
            }
        #else
            inet_ntop(afType, addrPtr, clientIPStr, afType == AF_INET6 ? INET6_ADDRSTRLEN : INET_ADDRSTRLEN);
        #endif
    }

    bool Server::waitForClientData(const int timeoutMS) {
        struct pollfd pfd;
        pfd.fd = this->c_sock;
        pfd.events = POLLIN;

        const int status = poll(&pfd, 1, timeoutMS);
        return (status > 0 && (pfd.revents & POLLIN));
    }

    int Server::acceptConnection(struct sockaddr_storage& clientAddr, socklen_t& clientLen) {
        return this->c_sock = accept(this->sock, (struct sockaddr*)&clientAddr, &clientLen);
    }

    void Server::handleReqs() {
        // Accept requests from clients
        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        char clientIPStr[INET6_ADDRSTRLEN];

        while ( this->acceptConnection(clientAddr, clientLen) >= 0 ) {
            // Wait for data
            if (!this->waitForClientData(75)) {
                // Close client socket & cleanup TLS
                this->closeSocket(this->c_sock);
                continue;
            }

            // Clear buffer
            this->clearBuffer();

            if (this->useTLS) {
                this->pSSL = SSL_new(this->pSSL_CTX);
                SSL_set_fd(this->pSSL, this->c_sock);

                if (SSL_accept(this->pSSL) <= 0) {
                    this->closeSocket(this->c_sock);
                    continue;
                }
            }

            // Read buffer
            size_t bytesReceived = this->readClientSock();
            this->extractClientIP(clientAddr, clientIPStr);

            // Skip empty packets
            if (bytesReceived > 0) {
                // Parse request
                Request request( this->readBuffer, clientIPStr );
                ACCESS_LOG << request.getMethodStr() << ' ' << request.getIPStr() << ' ' << request.getPathStr() << std::endl; // Flush w/ endl vs newline

                // Generate response
                Response response(request.getVersion());
                this->genResponse(request, response);

                // Load response to buffer
                const bool omitBody = request.getMethod() == HTTP::METHOD::HEAD;
                std::string resBuffer;
                response.loadToBuffer(resBuffer, omitBody);

                // Send response & close connection
                this->writeClientSock(resBuffer.c_str(), resBuffer.size());
            }

            // Close client socket & cleanup TLS
            this->closeSocket(this->c_sock);
        }
    }

    void Server::genResponse(const Request& request, Response& response) {
        // Verify Host header is present (RFC2616)
        if (request.getHeader("HOST") == nullptr) {
            // If the request allows HTML, return an HTML display
            if (request.isMIMEAccepted("text/html"))
                response.loadBodyFromErrorDoc(400);
        } else {
            // Load the response as normal, switching on the verb used
            request.loadResponse(response);
        }

        // Handle compression
        const std::string contentType = response.getContentType();

        // Determine compression method
        if (request.getMethod() == HTTP::METHOD::OPTIONS) return;

        if (this->useTLS && request.isEncodingAccepted("br"))
            response.compressBody(COMPRESS_BROTLI);
        else if (request.isEncodingAccepted("gzip"))
            response.compressBody(COMPRESS_GZIP);
        else if (request.isEncodingAccepted("deflate"))
            response.compressBody(COMPRESS_DEFLATE);
    }

    void Server::clearBuffer() {
        for (u_int i = 0; i < this->maxBufferSize; ++i)
            this->readBuffer[i] = 0;
    }

}