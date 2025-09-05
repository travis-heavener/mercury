#include "server.hpp"

namespace http {

    Server::Server(const port_t port, const bool useTLS) : port(port),
    maxBacklog(conf::MAX_REQUEST_BACKLOG), maxBufferSize(conf::MAX_REQUEST_BUFFER),
        threadPool(conf::THREADS_PER_CHILD), useTLS(useTLS) {};

    void Server::kill() {
        // Close sockets
        for (const int c_sock : this->clientSocks) {
            if (c_sock != SOCKET_UNSET) {
                this->closeSocket(c_sock);
            }
        }

        if (this->sock != SOCKET_UNSET && !this->closeSocket(this->sock)) {
            ACCESS_LOG << "Server socket closed (" << *this << ")." << std::endl;
            this->sock = SOCKET_UNSET;
        }

        // Free SSL ptrs
        if (this->useTLS)
            SSL_CTX_free(this->pSSL_CTX);
    }

    int Server::bindSocket() {
        // Open the socket
        this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (this->sock < 0) {
            ERROR_LOG << "Failed to open socket (" << *this << ") on port " << this->port << std::endl;
            return SOCKET_FAILURE;
        }

        // Init socket opts
        const int optFlag = 1;
        if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set socket opt SO_REUSEADDR (" << *this << ")." << std::endl;
            return SOCKET_FAILURE;
        }

        if (setsockopt(this->sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set socket opt TCP_NODELAY (" << *this << ")." << std::endl;
            return SOCKET_FAILURE;
        }

        #if __linux__
            if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&optFlag, sizeof(int)) < 0) {
                ERROR_LOG << "Failed to set socket opt SO_REUSEPORT (" << *this << ")." << std::endl;
                return SOCKET_FAILURE;
            }
        #endif

        // Bind the host address
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(this->port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(this->sock, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
            #ifdef __linux__
                const int err = errno;
            #else
                const int err = WSAGetLastError();
            #endif

            if (err == 13) { // Improve error handling for errno 13 (need sudo to listen to port)
                ERROR_LOG << "Failed to bind socket (" << *this << "), errno: " << err << ", do you have sudo perms?" << std::endl;
            } else {
                ERROR_LOG << "Failed to bind socket (" << *this << "), errno: " << err << std::endl;
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
            ERROR_LOG << "Failed to listen to socket (" << *this << "), errno: " << errno << std::endl;
            return LISTEN_FAILURE;
        }

        // Init TLS
        if (this->useTLS) {
            if ((this->pSSL_CTX = initTLSContext()) == nullptr) {
                ERROR_LOG << "Failed to init an SSL context (" << *this << ")." << std::endl;
                return BIND_FAILURE;
            }
        }

        ACCESS_LOG << "Listening on port " << this->port << " (" << *this << ")." << std::endl;
        return 0;
    }

    ssize_t Server::readClientSock(char* readBuffer, const int client, SSL* pSSL) {
        if (this->useTLS)
            return SSL_read(pSSL, readBuffer, this->maxBufferSize);
        else
            return recv(client, readBuffer, this->maxBufferSize, 0);
    }

    ssize_t Server::writeClientSock(const int client, SSL* pSSL, std::string& resBuffer) {
        if (this->useTLS)
            return SSL_write(pSSL, resBuffer.c_str(), resBuffer.size());
        else
            return send(client, resBuffer.c_str(), resBuffer.size(), 0);
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

        #ifdef _WIN32
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
        std::unique_lock lock(clientsMutex);
        this->clientSocks.insert(client);
    }

    void Server::untrackClient(const int client) {
        std::unique_lock lock(clientsMutex);
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
            auto self = shared_from_this();  // must inherit from enable_shared_from_this
            threadPool.enqueue([self, client, ip = std::move(clientIPStr)]() mutable {
                self->handleReqs(client, std::move(ip));
            });
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
            // Read buffer until headers have loaded
            std::string requestStr;
            requestStr.reserve(this->maxBufferSize);

            // Poll for data
            bool isForceClosed = false, isDataReady = true;
            while (requestStr.find("\r\n\r\n") == std::string::npos &&
                !isForceClosed && isDataReady) {
                struct pollfd pfd; pfd.fd = client;
                const ssize_t pollStatus = this->waitForClientData(pfd, KEEP_ALIVE_TIMEOUT_MS);
                if (pollStatus <= 0 || (pfd.revents & (POLLHUP | POLLERR))) {
                    isForceClosed = true; break; // Fatal error or timeout
                }

                // Check for POLLIN event
                if (!(pfd.revents & POLLIN)) { isDataReady = false; break; }
                
                // Read buffer (regardless of TLS or not, keep looping if TLS)
                do {
                    this->clearBuffer(readBuffer); // Clear read buffer
                    const ssize_t bytesReceived = this->readClientSock(readBuffer, client, pSSL);
                    if (bytesReceived <= 0) { isForceClosed = true; break; } // Connection closed by client
                    requestStr.append(readBuffer, bytesReceived); // Concat string
                } while (this->useTLS && SSL_pending(pSSL) > 0);
            }

            if (!isDataReady) continue; // Handle POLLIN not set
            if (isForceClosed) break; // Handle connection closed by client

            // Process headers and early info
            headers_map_t reqHeaders;
            loadEarlyHeaders(reqHeaders, requestStr);
            size_t contentLength;
            try {
                contentLength = reqHeaders.find("CONTENT-LENGTH") != reqHeaders.end() ?
                    std::stoull(reqHeaders["CONTENT-LENGTH"]) : 0;
            } catch (std::invalid_argument&) {
                break; // Bad Content-Length header, close connection
            }

            // Account for already read body
            if (contentLength > 0)
                contentLength -= requestStr.length() - (requestStr.find("\r\n\r\n") + 4);

            // Read remaining body
            while (contentLength > 0 && !isForceClosed && isDataReady) {
                struct pollfd pfd; pfd.fd = client;
                const ssize_t pollStatus = this->waitForClientData(pfd, KEEP_ALIVE_TIMEOUT_MS);
                if (pollStatus <= 0 || (pfd.revents & (POLLHUP | POLLERR))) {
                    isForceClosed = true; break; // Fatal error or timeout
                }

                // Check for POLLIN event
                if (!(pfd.revents & POLLIN)) { isDataReady = false; break; }

                // Read buffer (regardless of TLS or not, keep looping if TLS)
                do {
                    this->clearBuffer(readBuffer);
                    const ssize_t bytesReceived = this->readClientSock(readBuffer, client, pSSL);
                    if (bytesReceived <= 0) { isForceClosed = true; break; } // Connection closed by client
                    requestStr.append(readBuffer, bytesReceived); // Concat string

                    // Update remaining content length
                    contentLength = (contentLength > static_cast<size_t>(bytesReceived)) ?
                        (contentLength - bytesReceived) : 0;
                } while (this->useTLS && SSL_pending(pSSL) > 0);
            }

            if (!isDataReady) continue; // Handle POLLIN not set
            if (isForceClosed) break; // Handle connection closed by client

            // Parse request
            Response* pResponse = nullptr;
            try {
                Request request(reqHeaders, requestStr, clientIPStr, useTLS);

                // Generate response
                pResponse = genResponse(request);

                ACCESS_LOG << request.getMethodStr() << ' '
                        << request.getIPStr() << ' '
                        << request.getPathStr()
                        << " -- (" << pResponse->getStatus() << ") ["
                        << request.getVersion() << ']'
                        << std::endl; // Flush w/ endl vs newline

                // Handle keep-alive requests
                const std::string* pConnHeader = request.getHeader("Connection");
                std::string connHeader = (pConnHeader != nullptr) ? *pConnHeader : ""; // Copy string
                strToUpper(connHeader); // Format copied string
                if (connHeader == "KEEP-ALIVE" || (connHeader == "" && request.getVersion() == "HTTP/1.1")) {
                    // HTTP/1.1 defaults to keep-alive
                    pResponse->setHeader("Connection", "keep-alive");
                    pResponse->setHeader("Keep-Alive",
                                "timeout=" + std::to_string(KEEP_ALIVE_TIMEOUT_MS/1000) +
                                ", max=" + std::to_string(KEEP_ALIVE_MAX_REQ));
                    --keepAliveReqsLeft;
                } else { // Close connection
                    pResponse->setHeader("Connection", "close");
                    keepAliveReqsLeft = 0;
                }

                // Load response to buffer
                const bool omitBody = request.getMethod() == http::METHOD::HEAD;
                std::string resBuffer;
                pResponse->loadToBuffer(resBuffer, omitBody);

                // Send response & close connection
                ssize_t bytesSent = this->writeClientSock(client, pSSL, resBuffer);
                if (bytesSent < 0) break;
            } catch (http::Exception& e) {
                if (pResponse != nullptr) delete pResponse;
                break; // Handles invalid requests syntax (ie. non-CRLF)
            }

            // Free Response
            delete pResponse;
        }

        // Close client socket & cleanup TLS
        this->closeClientSocket(client, pSSL);

        // Free read buffer
        delete[] readBuffer;
    }

    Response* Server::genResponse(Request& request) {
        // Handle different HTTP versions
        Response* pResponse;

        if (request.getVersion() == "HTTP/1.1") {
            pResponse = version::handler_1_1::genResponse(request);
        } else if (request.getVersion() == "HTTP/1.0" && conf::ENABLE_LEGACY_HTTP) {
            pResponse = version::handler_1_0::genResponse(request);
        } else if (request.getVersion() == "HTTP/0.9" && conf::ENABLE_LEGACY_HTTP
            && !request.getHasExplicitlyDefinedHTTPVersion0_9()) {
            pResponse = version::handler_0_9::genResponse(request);
        } else {
            pResponse = new Response("HTTP/1.1"); // Default version

            // Handle with HTML if possible
            pResponse->setStatus(505);
            if (request.isMIMEAccepted("text/html"))
                pResponse->loadBodyFromErrorDoc(505);
        }

        // Handle compression
        const std::string contentType = pResponse->getContentType();

        // Determine compression method
        if (request.getMethod() == http::METHOD::OPTIONS) return pResponse;

        if (this->useTLS && request.isEncodingAccepted("br"))
            pResponse->compressBody(COMPRESS_BROTLI);
        else if (request.isEncodingAccepted("gzip"))
            pResponse->compressBody(COMPRESS_GZIP);
        else if (request.isEncodingAccepted("deflate"))
            pResponse->compressBody(COMPRESS_DEFLATE);

        return pResponse;
    }

    void Server::clearBuffer(char* readBuffer) {
        for (unsigned int i = 0; i < this->maxBufferSize; ++i)
            readBuffer[i] = 0;
    }

    std::ostream& operator<<(std::ostream& os, const Server& server) {
        os << "IPv" << (server.isIPv4() ? "4" : "6");
        if (server.usesTLS()) os << " w/ TLS";
        return os;
    }

}