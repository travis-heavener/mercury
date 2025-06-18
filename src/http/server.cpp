#include "server.hpp"

namespace HTTP {

    void Server::kill() {
        // Close sockets
        for (const int c_sock : this->clientSocks) {
            if (c_sock != -1 && !this->closeSocket(c_sock)) {
                ACCESS_LOG << "Client socket closed." << std::endl;
            }
        }

        if (this->sock != -1 && !this->closeSocket(this->sock)) {
            ACCESS_LOG << "Socket closed." << std::endl;
            this->sock = -1;
        }

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
            ERROR_LOG << "Failed to listen to socket (errno: " << errno << ')' << std::endl;
            return LISTEN_FAILURE;
        }

        // Init TLS
        if (this->useTLS) {
            if ((this->pSSL_CTX = initTLSContext()) == nullptr) {
                ERROR_LOG << "Failed to init an SSL context." << std::endl;
                return BIND_FAILURE;
            }
        }

        ACCESS_LOG << "Listening on port " << this->port << '.' << std::endl;
        return 0;
    }

    ssize_t Server::readClientSock(char* readBuffer, const int client, SSL* pSSL) {
        if (this->useTLS)
            return SSL_read(pSSL, readBuffer, this->maxBufferSize);
        else
            return recv(client, readBuffer, this->maxBufferSize, 0);
    }

    ssize_t Server::writeClientSock(const int client, SSL* pSSL, std::string& resBuffer) {
        if (this->useTLS) {
            return SSL_write(pSSL, resBuffer.c_str(), resBuffer.size());
        } else {
            return send(client, resBuffer.c_str(), resBuffer.size(), 0);
        }
    }

    int Server::closeSocket(const int sock) {
        #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            shutdown(sock, SD_BOTH);
            return closesocket(sock);
        #else
            return close(sock);
        #endif
    }

    int Server::closeClientSocket(const int sock, SSL* pSSL) {
        const int status = this->closeSocket(sock);
        this->untrackClient(sock);

        // Cleanup TLS
        if (this->useTLS)
            SSL_free(pSSL);

        return status;
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

    ssize_t Server::waitForClientData(struct pollfd& pfd, const int timeoutMS) {
        pfd.events = POLLIN;
        pfd.revents = 0;
        return poll(&pfd, 1, timeoutMS);
    }

    void Server::trackClient(const int client) {
        this->clientSocks.insert(client);
    }

    void Server::untrackClient(const int client) {
        this->clientSocks.erase(client);
    }

    int Server::acceptConnection(struct sockaddr_storage& clientAddr, socklen_t& clientLen) {
        return accept(this->sock, (struct sockaddr*)&clientAddr, &clientLen);
    }

    void Server::acceptLoop() {
        while (true) {
            struct sockaddr_storage clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            char clientIPStr[INET6_ADDRSTRLEN];

            // Accept connections
            const int client = this->acceptConnection(clientAddr, clientLen);
            if (client < 0) continue;

            // Otherwise, handle the client request
            this->trackClient(client);
            this->extractClientIP(clientAddr, clientIPStr); // Read client IP

            // Detach new thread
            std::thread(&Server::handleReqs, this, client, std::string(clientIPStr)).detach();
        }
    }

    // Accept requests from clients
    void Server::handleReqs(const int client, const std::string clientIPStr) {
        // Create SSL context
        SSL* pSSL = nullptr;
        if (this->useTLS) {
            pSSL = SSL_new(this->pSSL_CTX);
            SSL_set_fd(pSSL, client);

            if (SSL_accept(pSSL) <= 0) {
                this->closeClientSocket(client, pSSL);
                return;
            }
        }

        // Create read buffer
        char* readBuffer = new char[this->maxBufferSize];
        this->clearBuffer(readBuffer);

        // Track keep-alive requests for a given connection
        int keepAliveReqsLeft = KEEP_ALIVE_MAX_REQ;
        while (keepAliveReqsLeft > 0) {
            // Poll for data
            struct pollfd pfd;
            pfd.fd = client;
            const ssize_t pollStatus = this->waitForClientData(pfd, KEEP_ALIVE_TIMEOUT_MS);
            if (pollStatus < 0) {
                if (errno != 0)
                    ERROR_LOG << "poll() error: " << strerror(errno) << std::endl;
                break;
            } else if (pollStatus == 0 || (pfd.revents & (POLLHUP | POLLERR))) {
                break; // Time out, hang up, or fatal error
            }

            // Check for POLLIN event
            if (!(pfd.revents & POLLIN)) continue;

            // Read buffer
            this->clearBuffer(readBuffer); // Clear read buffer
            const ssize_t bytesReceived = this->readClientSock(readBuffer, client, pSSL);
            if (bytesReceived == 0) // Connection closed by client
                break;

            try {
                // Parse request
                Request request(readBuffer, clientIPStr.c_str());
                ACCESS_LOG << request.getMethodStr() << ' '  << request.getIPStr() << ' '
                        << request.getPathStr() << std::endl; // Flush w/ endl vs newline

                // Generate response
                Response response;
                this->genResponse(request, response);

                // Handle keep-alive requests
                const std::string* pConnHeader = request.getHeader("Connection");
                std::string connHeader = (pConnHeader != nullptr) ? *pConnHeader : ""; // Copy string
                strToLower(connHeader); // Format copied string
                if (connHeader == "keep-alive" || (connHeader == "" && request.getVersion() == "HTTP/1.1")) {
                    // HTTP/1.1 defaults to keep-alive
                    response.setHeader("Connection", "keep-alive");
                    response.setHeader("Keep-Alive",
                                "timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT_MS/1000) +
                                ", max=" + std::to_string(KEEP_ALIVE_MAX_REQ));
                    --keepAliveReqsLeft;
                } else { // Close connection
                    response.setHeader("Connection", "close");
                    keepAliveReqsLeft = 0;
                }

                // Load response to buffer
                const bool omitBody = request.getMethod() == HTTP::METHOD::HEAD;
                std::string resBuffer;
                response.loadToBuffer(resBuffer, omitBody);

                // Send response & close connection
                ssize_t bytesSent = this->writeClientSock(client, pSSL, resBuffer);
                if (bytesSent < 0) {
                    ERROR_LOG << "writeClientSock error: " << strerror(errno) << std::endl;
                    break;
                }
            } catch (http::Exception& e) {
                break; // Handles invalid requests syntax (ie. non-CRLF)
            }
        }

        // Close client socket & cleanup TLS
        this->closeClientSocket(client, pSSL);

        // Free read buffer
        delete[] readBuffer;
    }

    void Server::genResponse(Request& request, Response& response) {
        // Verify request is valid
        if (
            request.getHeader("HOST") == nullptr || // Verify Host header is present (RFC 2616)
            request.isURIBad() // Verify URI isn't malformed
        ) {
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

    void Server::clearBuffer(char* readBuffer) {
        for (u_int i = 0; i < this->maxBufferSize; ++i)
            readBuffer[i] = 0;
    }

}