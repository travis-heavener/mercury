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
        #if __linux__
            if (this->useTLS)
                SSL_CTX_free(this->pSSL_CTX);
        #endif
    }

    // Initialize the socket
    int Server::init() {
        // Open the socket
        this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (this->sock < 0) {
            ERROR_LOG << "Failed to open socket on port " << this->port << '\n';
            return SOCKET_FAILURE;
        }

        // Init socket opts
        const int optFlag = 1;
        if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set socket opt SO_REUSEADDR." << '\n';
            return SOCKET_FAILURE;
        }

        if (setsockopt(this->sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set socket opt TCP_NODELAY." << '\n';
            return SOCKET_FAILURE;
        }

        #if __linux__
            if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&optFlag, sizeof(int)) < 0) {
                ERROR_LOG << "Failed to set socket opt SO_REUSEPORT." << '\n';
                return SOCKET_FAILURE;
            }
        #endif

        // Bind the host address
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(this->port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(this->sock, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
            if (errno == 13) { // Improve error handling for errno 13 (need sudo to listen to port)
                ERROR_LOG << "Failed to bind socket (errno: " << errno << ", do you have sudo perms?)\n";
            } else {
                ERROR_LOG << "Failed to bind socket (errno: " << errno << ')' << '\n';
            }
            return BIND_FAILURE;
        }

        // Listen to socket
        if (listen(this->sock, this->maxBacklog) < 0) {
            ERROR_LOG << "Failed to listen to socket (errno: " << errno << ')' << '\n';
            return LISTEN_FAILURE;
        }

        // Init TLS
        #if __linux__
            if (this->useTLS) {
                this->pSSL_CTX = initTLSContext();

                if (this->pSSL_CTX == nullptr) {
                    ERROR_LOG << "Failed to init an SSL context.\n";
                    return BIND_FAILURE;
                }
            }
        #endif

        ACCESS_LOG << "Listening to traffic on port " << this->port << '.' << '\n';
        return 0;
    }

    size_t Server::readClientSock() {
        #if __linux__
            if (this->useTLS)
                return SSL_read(this->pSSL, this->readBuffer, this->maxBufferSize);
        #endif
        return recv(this->c_sock, this->readBuffer, this->maxBufferSize, 0);
    }

    void Server::writeClientSock(const char* pBuffer, const size_t bufferSize) {
        if (this->useTLS) {
            #if __linux__
                SSL_write(this->pSSL, pBuffer, bufferSize);
            #endif
        } else {
            send(this->c_sock, pBuffer, bufferSize, 0);
        }
    }

    int Server::closeSocket(const int sock) {
        #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            return closesocket(sock);
        #else
            return close(sock);
        #endif
    }

    void Server::handleReqs() {
        // Accept requests from clients
        struct sockaddr_in clientAddr;
        struct in_addr clientIP;
        socklen_t clientLen = sizeof(clientAddr);
        char clientIPStr[INET_ADDRSTRLEN];

        while ( (this->c_sock = accept(this->sock, (struct sockaddr*)&clientAddr, &clientLen)) >= 0 ) {
            // Clear buffer
            this->clearBuffer();

            #if __linux__
                if (this->useTLS) {
                    this->pSSL = SSL_new(this->pSSL_CTX);
                    SSL_set_fd(this->pSSL, this->c_sock);

                    if (SSL_accept(this->pSSL) <= 0) {
                        SSL_free(this->pSSL);
                        this->closeSocket(this->c_sock);
                        continue;
                    }
                }
            #endif

            // Read buffer
            size_t bytesReceived = this->readClientSock();
            clientIP = ((struct sockaddr_in*)&clientAddr)->sin_addr;

            #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
                strcpy(clientIPStr, inet_ntoa( clientIP ));
            #else
                inet_ntop( AF_INET, &clientIP, clientIPStr, INET_ADDRSTRLEN );
            #endif

            // Skip empty packets
            if (bytesReceived == 0) continue;

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
            this->closeSocket(this->c_sock); // Close client socket

            // Cleanup TLS
            #if __linux__
                if (this->useTLS)
                    SSL_free(this->pSSL);
            #endif
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