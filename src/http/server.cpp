#include "server.hpp"

#include "../conf/conf.hpp"
#include "../io/file.hpp"
#include "../logs/logger.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

#include "tools.hpp"
#include "version/handler_1_1.hpp"
#include "version/handler_1_0.hpp"
#include "version/handler_0_9.hpp"

#define SOCKET_DRAIN_BUFFER_SIZE 8192

#ifdef _WIN32
    #define close closesocket
#endif

namespace http {

    Server::Server(const port_t port, const bool useTLS) : port(port),
        threadPool(), useTLS(useTLS) {};

    void Server::kill() {
        // Close sockets
        for (const int c_sock : this->clientSocks)
            if (c_sock != SOCKET_UNSET)
                this->closeSocket(c_sock);

        if (this->sock != SOCKET_UNSET && !this->closeSocket(this->sock)) {
            ACCESS_LOG << "Server socket closed (" << *this << ")." << std::endl;
            this->sock = SOCKET_UNSET;
        }

        // Free SSL ptrs
        if (this->useTLS) SSL_CTX_free(this->pSSL_CTX);

        // Signal to threads to wrap it up yo
        this->isExiting.store(true);
        this->threadPool.stop();
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
        bindSocketOpt(this, this->sock, SOL_SOCKET, SO_REUSEADDR, optFlag, true);
        bindSocketOpt(this, this->sock, IPPROTO_TCP, TCP_NODELAY, optFlag, true);

        #if __linux__
            bindSocketOpt(this, this->sock, SOL_SOCKET, SO_REUSEPORT, optFlag, true);
        #endif

        // Bind the host address
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(this->port);
        memcpy(&addr.sin_addr, conf::BIND_ADDR_IPV4->bytes, 4);

        if (bind(this->sock, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
            #ifdef _WIN32
                int lastErrno = WSAGetLastError();
            #else
                int lastErrno = errno;
            #endif

            ERROR_LOG << "Failed to bind socket (" << *this << "), errno: "
                << lastErrno
                << (lastErrno == 13 ? ", do you have sudo perms?" : "") // Improve log for errno 13 (needs sudo)
                << std::endl;
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
        if (listen(this->sock, conf::MAX_REQUEST_BACKLOG) < 0) {
            #ifdef _WIN32
                int lastErrno = WSAGetLastError();
            #else
                int lastErrno = errno;
            #endif

            ERROR_LOG << "Failed to listen to socket (" << *this << "), errno: " << lastErrno << std::endl;
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
            return SSL_read(pSSL, readBuffer, conf::REQUEST_BUFFER_SIZE);
        else
            return recv(client, readBuffer, conf::REQUEST_BUFFER_SIZE, 0);
    }

    ssize_t Server::writeClientSock(const int client, SSL* pSSL, const char* resBuffer, const size_t n) {
        ssize_t status;
        if (this->useTLS) {
            status = SSL_write(pSSL, resBuffer, n);
        } else {
            #ifndef _WIN32
                status = send(client, resBuffer, n, MSG_NOSIGNAL);
            #else
                status = send(client, resBuffer, n, 0);
            #endif
        }
        return (status <= 0) ? -1 : status;
    }

    int Server::closeSocket(const int sock) {
        #ifdef _WIN32
            shutdown(sock, SD_BOTH);
        #endif
        return close(sock);
    }

    int Server::closeClientSocket(const int sock, SSL* pSSL) {
        this->untrackClient(sock);
        if (this->useTLS) SSL_free(pSSL); // Cleanup TLS
        return this->closeSocket(sock);
    }

    void Server::drainClientSocket(const int sock, SSL* pSSL, size_t size) {
        char buffer[SOCKET_DRAIN_BUFFER_SIZE];
        while (size > 0) {
            ssize_t bytesRead = this->readClientSock(buffer, sock, pSSL);
            if (bytesRead <= 0) break; // Connection closed
            if (static_cast<size_t>(bytesRead) > size) break;
            size -= static_cast<size_t>(bytesRead);
        }
    }

    void Server::extractClientIP(struct sockaddr_storage& clientAddr, char* clientIPStr) const {
        void* addrPtr = nullptr;
        int afType = ((struct sockaddr*)&clientAddr)->sa_family;

        if (afType == AF_INET6)
            addrPtr = &(((struct sockaddr_in6*)&clientAddr)->sin6_addr);
        else
            addrPtr = &(((struct sockaddr_in*)&clientAddr)->sin_addr);

        #ifdef _WIN32
            if (afType == AF_INET)
                strcpy(clientIPStr, inet_ntoa(*(struct in_addr*)addrPtr));
            else
                inet_ntop(AF_INET6, addrPtr, clientIPStr, INET6_ADDRSTRLEN);
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
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(this->sock, &fds);

        struct timeval tv;
        tv.tv_sec = 1; // 1 second timeout
        tv.tv_usec = 0;

        int rv = select(this->sock + 1, &fds, nullptr, nullptr, &tv);
        if (rv <= 0) return -1; // Timeout or error

        return accept(this->sock, (struct sockaddr*)&clientAddr, &clientLen);
    }

    void Server::acceptLoop() {
        while (!this->isExiting) {
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
            auto self = shared_from_this(); // Must inherit from enable_shared_from_this
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

        // Create read buffer per-thread
        thread_local std::vector<char> readBuffer(conf::REQUEST_BUFFER_SIZE);
        this->clearBuffer(readBuffer);

        // Track keep-alive requests for a given connection
        RequestFlags reqFlags;
        int keepAliveReqsLeft = static_cast<int>( conf::MAX_KEEP_ALIVE_REQUESTS );
        while (keepAliveReqsLeft > 0 && !reqFlags.isContentTooLarge && !reqFlags.isURITooLong) {
            // Declare container for parsed request headers
            headers_map_t reqHeaders;

            // Read buffer until headers have loaded
            thread_local std::string requestStr;
            requestStr.clear(); // Clear previous content from thread_local
            requestStr.reserve(conf::REQUEST_BUFFER_SIZE);

            // Poll for data
            bool isForceClosed = false, isDataReady = true;
            while (requestStr.find("\r\n\r\n") == std::string::npos && !isForceClosed && isDataReady) {
                if (isExiting) { // Program closed
                    isForceClosed = true; break;
                }

                struct pollfd pfd; pfd.fd = client;
                const ssize_t pollStatus = this->waitForClientData(pfd, conf::KEEP_ALIVE_TIMEOUT * 1000);
                if (pollStatus <= 0 || (pfd.revents & (POLLHUP | POLLERR))) {
                    isForceClosed = true; break; // Fatal error or timeout
                }

                // Check for POLLIN event
                if (!(pfd.revents & POLLIN)) { isDataReady = false; break; }
                
                // Read buffer (regardless of TLS or not, keep looping if TLS)
                do {
                    this->clearBuffer(readBuffer); // Clear read buffer
                    const ssize_t bytesReceived = this->readClientSock(readBuffer.data(), client, pSSL);
                    if (bytesReceived <= 0) { isForceClosed = true; break; } // Connection closed by client
                    requestStr.append(readBuffer.data(), bytesReceived); // Concat string
                } while (this->useTLS && SSL_pending(pSSL) > 0);

                // Check for status line
                const size_t requestLineEnd = requestStr.find("\r\n");
                if (requestStr.size() > conf::MAX_REQUEST_LINE_LENGTH &&
                    (requestLineEnd == std::string::npos || requestLineEnd > conf::MAX_REQUEST_LINE_LENGTH)) {
                    reqFlags.isURITooLong = true; // URI and/or request line is too long
                    break; // No need to drain client socket since the connection is about to be closed
                }
            }

            if (!isDataReady) continue; // Handle POLLIN not set
            if (isForceClosed) break; // Handle connection closed by client

            // Process headers and early info
            if (!reqFlags.isURITooLong) {
                loadEarlyHeaders(reqHeaders, requestStr);
                size_t contentLength;
                try {
                    contentLength = reqHeaders.find("CONTENT-LENGTH") != reqHeaders.end() ?
                        std::stoull(reqHeaders["CONTENT-LENGTH"]) : 0;
                } catch (std::invalid_argument&) {
                    break; // Bad Content-Length header, close connection
                }

                // Reject oversized bodies
                if (contentLength > conf::MAX_REQUEST_BODY) {
                    contentLength = 0; // Passthru
                    this->drainClientSocket(client, pSSL, contentLength); // Drain socket
                    reqFlags.isContentTooLarge = true;
                }

                // Account for already read body
                if (contentLength > 0)
                    contentLength -= requestStr.length() - (requestStr.find("\r\n\r\n") + 4);

                // Read remaining body
                while (contentLength > 0 && !isForceClosed && isDataReady) {
                    struct pollfd pfd; pfd.fd = client;
                    const ssize_t pollStatus = this->waitForClientData(pfd, conf::KEEP_ALIVE_TIMEOUT * 1000);
                    if (pollStatus <= 0 || (pfd.revents & (POLLHUP | POLLERR))) {
                        isForceClosed = true; break; // Fatal error or timeout
                    }

                    // Check for POLLIN event
                    if (!(pfd.revents & POLLIN)) { isDataReady = false; break; }

                    // Read buffer (regardless of TLS or not, keep looping if TLS)
                    do {
                        this->clearBuffer(readBuffer);
                        const ssize_t bytesReceived = this->readClientSock(readBuffer.data(), client, pSSL);
                        if (bytesReceived <= 0) { isForceClosed = true; break; } // Connection closed by client
                        requestStr.append(readBuffer.data(), bytesReceived); // Concat string

                        // Update remaining content length
                        contentLength = (contentLength > static_cast<size_t>(bytesReceived)) ?
                            (contentLength - bytesReceived) : 0;
                    } while (this->useTLS && SSL_pending(pSSL) > 0);
                }

                if (!isDataReady) continue; // Handle POLLIN not set
                if (isForceClosed) break; // Handle connection closed by client
            }

            // Parse request
            std::unique_ptr<Response> pResponse = nullptr;
            try {
                Request request(reqHeaders, requestStr, clientIPStr, useTLS, reqFlags);

                // Generate response
                pResponse = genResponse(request);

                // Handle keep-alive requests
                const std::optional<std::string> connHeader = request.getHeader("Connection");
                std::string connValue = connHeader.has_value() ? *connHeader : ""; // Copy string
                strToUpper(connValue); // Format copied string
                if (conf::IS_KEEP_ALIVE_ENABLED &&
                    !reqFlags.isContentTooLarge && !reqFlags.isURITooLong &&
                    (connValue == "KEEP-ALIVE" || (connValue == "" && request.getVersion() == "HTTP/1.1"))) {
                    // HTTP/1.1 defaults to keep-alive
                    pResponse->setHeader("Connection", "keep-alive");
                    pResponse->setHeader("Keep-Alive",
                                "timeout=" + std::to_string(conf::KEEP_ALIVE_TIMEOUT) +
                                ", max=" + std::to_string(conf::MAX_KEEP_ALIVE_REQUESTS));
                    --keepAliveReqsLeft;
                } else { // Close connection
                    pResponse->setHeader("Connection", "close");
                    keepAliveReqsLeft = 0;
                }

                // Load response to buffer
                const bool omitBody = request.getMethod() == http::METHOD::HEAD;
                std::function<ssize_t(const char*, const size_t)> sendFunc = [this, client, &pSSL](const char* resBuffer, const size_t n) -> ssize_t {
                    return this->writeClientSock(client, pSSL, resBuffer, n);
                };

                // Handle write failure
                const ssize_t sendStatus = pResponse->streamBody(request.isMIMEAccepted("text/html"), omitBody, sendFunc);

                // Log request
                ACCESS_LOG << request.getMethodStr() << ' '
                        << (conf::REDACT_LOG_IPS ? "<Anonymous IP>" : request.getIPStr()) << ' '
                        << request.getPaths().rawPathFromRequest
                        << " -- (" << pResponse->getStatus() << ") ["
                        << request.getVersion() << ']'
                        << std::endl; // Flush w/ endl vs newline

                // Handle connection closure OR Content Too Large
                if (sendStatus < 0 || pResponse->getStatus() == 413)
                    break;
            } catch (http::Exception& e) {
                break; // Handles invalid requests syntax (ie. non-CRLF)
            }
        }

        // Close client socket & cleanup TLS
        this->closeClientSocket(client, pSSL);
    }

    std::unique_ptr<Response> Server::genResponse(Request& request) {
        // Handle different HTTP versions
        std::unique_ptr<Response> pResponse = nullptr;

        if (request.getVersion() == "HTTP/1.1") {
            pResponse = version::handler_1_1::genResponse(request);
        } else if (request.getVersion() == "HTTP/1.0" && conf::ENABLE_LEGACY_HTTP) {
            pResponse = version::handler_1_0::genResponse(request);
        } else if (request.getVersion() == "HTTP/0.9" && conf::ENABLE_LEGACY_HTTP
            && !request.hasExplicitHTTP0_9()) {
            pResponse = version::handler_0_9::genResponse(request);
        } else {
            pResponse = std::unique_ptr<Response>(new Response("HTTP/1.1")); // Default version

            // Handle with HTML if possible
            pResponse->setStatus(505);
            if (request.isMIMEAccepted("text/html"))
                pResponse->loadBodyFromErrorDoc(505);
        }

        // Pass the compression method
        pResponse->setCompressMethod(request.getCompressMethod(pResponse->getContentType()));

        return pResponse;
    }

    void Server::getUsageInfo(size_t& usedThreads, size_t& totalThreads, size_t& pendingConnections) {
        threadPool.getUsageInfo(usedThreads, totalThreads, pendingConnections);
    }

    void Server::clearBuffer(std::vector<char>& readBuffer) {
        std::fill(readBuffer.begin(), readBuffer.end(), 0);
    }

    std::ostream& operator<<(std::ostream& os, const Server& server) {
        os << "IPv" << (server.isIPv4() ? "4" : "6");
        if (server.usesTLS()) os << " w/ TLS";
        return os;
    }

}

#ifdef _WIN32
    #undef close
#endif