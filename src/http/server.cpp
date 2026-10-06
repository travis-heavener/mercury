#include "server.hpp"

#include <iostream>

#include "../conf/conf.hpp"
#include "../io/file.hpp"
#include "../logs/logger.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

#include "tools.hpp"
#include "version/handler_1_1.hpp"
#include "version/handler_1_0.hpp"
#include "version/handler_0_9.hpp"


#ifdef _WIN32
    #define close closesocket
#endif

namespace http {

    Server::Server(const port_t port, const bool useTLS) : port(port),
        threadPool(), useTLS(useTLS) {};

    // Stop accepting clients, drain workers, then release shared TLS state
    void Server::kill() {
        // Make repeated shutdown requests harmless
        if (isExiting.exchange(true)) return;

        // Remove the listener from service before closing its descriptor
        const int listener = sock.exchange(SOCKET_UNSET);
        if (listener != SOCKET_UNSET) {
            #ifdef _WIN32
                shutdown(listener, SD_BOTH);
            #else
                shutdown(listener, SHUT_RDWR);
            #endif
            closeSocket(listener);
        }

        // Wake blocked readers without closing descriptors still owned by workers.
        {
            std::unique_lock lock(clientsMutex);
            for (const int client : clientSocks) {
                #ifdef _WIN32
                    shutdown(client, SD_BOTH);
                #else
                    shutdown(client, SHUT_RDWR);
                #endif
            }
        }
        // Workers may still use the TLS context until their requests finish
        threadPool.stop();
        if (useTLS) {
            SSL_CTX_free(pSSL_CTX);
            pSSL_CTX = nullptr;
        }
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

        if (::bind(this->sock.load(), (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
            #ifdef _WIN32
                int lastErrno = WSAGetLastError();
            #else
                int lastErrno = errno;
            #endif

            ERROR_LOG << "Failed to bind socket (" << *this << "), errno: "
                << lastErrno
                << (lastErrno == 13 ? ", do you have sudo perms?" : "") // Improve log for errno 13 (needs sudo)
                << std::endl;

            // Improve log for errno 13 (needs sudo)
            if (lastErrno == 13)
                std::cerr << "Failed to bind socket (" << *this << "), errno: " << lastErrno << ", do you have sudo perms?" << std::endl;
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

    // Register clients under the same lock used by shutdown
    bool Server::trackClient(const int client) {
        std::unique_lock lock(clientsMutex);
        if (isExiting) return false;
        this->clientSocks.insert(client);
        return true;
    }

    // Remove a client before its worker closes the socket
    void Server::untrackClient(const int client) {
        std::unique_lock lock(clientsMutex);
        this->clientSocks.erase(client);
    }

    int Server::acceptConnection(struct sockaddr_storage& clientAddr, socklen_t& clientLen) {
        // Snapshot the listener once so a concurrent shutdown cannot change it mid-call
        const int listener = sock.load();
        if (listener == SOCKET_UNSET) return -1;

        // Poll acceptance periodically so shutdown can end the loop
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listener, &fds);

        struct timeval tv;
        tv.tv_sec = 1; // 1 second timeout
        tv.tv_usec = 0;

        int rv = select(listener + 1, &fds, nullptr, nullptr, &tv);
        if (rv <= 0) return -1; // Timeout or error

        // Accept the ready client and return its peer address
        return accept(listener, (struct sockaddr*)&clientAddr, &clientLen);
    }

    void Server::acceptLoop() {
        while (!this->isExiting) {
            struct sockaddr_storage clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            char clientIPStr[INET6_ADDRSTRLEN];

            // Accept connections
            const int client = this->acceptConnection(clientAddr, clientLen);
            if (client < 0) continue;

            // Track the socket before queueing it so shutdown can wake pending clients
            if (!this->trackClient(client)) {
                closeSocket(client);
                continue;
            }
            this->extractClientIP(clientAddr, clientIPStr); // Read client IP

            // Queue the handler with its own IP string and shared server lifetime
            auto self = shared_from_this(); // Must inherit from enable_shared_from_this
            try {
                if (threadPool.enqueue([self, client, ip = std::string(clientIPStr)]() {
                    self->handleReqs(client, ip);
                })) continue;
            } catch (const std::exception& e) {
                ERROR_LOG << "Failed to queue client: " << e.what() << std::endl;
            }
            // Queueing failed; ownership never reached a worker, so clean up here
            untrackClient(client);
            closeSocket(client);
        }
    }

    // Handle all requests on one connection, retaining any pipelined bytes
    void Server::handleReqs(const int client, const std::string clientIPStr) {
        SSL* pSSL = nullptr;
        try {
            // Skip clients that were queued before shutdown began
            if (isExiting) throw http::Exception();

            // Complete the TLS handshake before reading HTTP data
            if (useTLS) {
                pSSL = SSL_new(pSSL_CTX);
                if (pSSL == nullptr) throw http::Exception();
                SSL_set_fd(pSSL, client);
                if (SSL_accept(pSSL) <= 0) throw http::Exception();
            }

            // Keep unread data across requests; one read may contain several requests
            std::vector<char> readBuffer(conf::REQUEST_BUFFER_SIZE);
            std::string pending;
            unsigned int requestsLeft = conf::MAX_KEEP_ALIVE_REQUESTS;

            // Pass socket writes to Response, which retries partial writes
            std::function<ssize_t(const char*, const size_t)> sendFunc = [&](const char* data, size_t size) {
                return writeClientSock(client, pSSL, data, size);
            };

            // Append the next available block without discarding buffered request data
            auto readMore = [&]() -> bool {
                if (isExiting) return false;

                // Decrypted TLS bytes can already be waiting without socket activity
                if (!useTLS || SSL_pending(pSSL) == 0) {
                    struct pollfd pfd{};
                    pfd.fd = client;
                    const ssize_t ready = waitForClientData(pfd, conf::KEEP_ALIVE_TIMEOUT * 1000);
                    if (ready <= 0 || !(pfd.revents & POLLIN)) return false;
                }

                // Stop on EOF or a read error; only append bytes actually received
                const ssize_t n = readClientSock(readBuffer.data(), client, pSSL);
                if (n <= 0) return false;
                pending.append(readBuffer.data(), static_cast<size_t>(n));
                return true;
            };

            // Reject framing errors before constructing a Request or reading its body
            auto reject = [&](const int status) {
                Response response(pending.find("HTTP/1.0") < pending.find("\r\n") ? "HTTP/1.0" : "HTTP/1.1");
                response.setStatus(status);
                response.setHeader("Connection", "close");
                response.setHeader("Content-Length", "0");
                response.streamBody(false, true, sendFunc);
            };

            // Process requests until the connection limit, shutdown, or an error
            while (requestsLeft > 0 && !isExiting) {
                size_t headerEnd;
                bool lineTooLong = false, isSimpleRequest = false;
                constexpr size_t maxHeaderBytes = 64 * 1024;

                // Read the complete header block, checking limits after every append
                while (true) {
                    const size_t lineEnd = pending.find("\r\n");
                    lineTooLong = (lineEnd == std::string::npos ? pending.size() : lineEnd) > conf::MAX_REQUEST_LINE_LENGTH;
                    headerEnd = pending.find("\r\n\r\n");

                    // HTTP/0.9 consists of one line with no protocol token or headers
                    // Recognize it even when disabled so genResponse can return 505
                    if (lineEnd != std::string::npos) {
                        const size_t firstSpace = pending.find(' ');
                        isSimpleRequest = firstSpace < lineEnd && pending.find(' ', firstSpace + 1) > lineEnd;
                        if (isSimpleRequest) headerEnd = lineEnd;
                    }

                    // Stop buffering once a complete request header or a limit is found
                    if (lineTooLong || headerEnd != std::string::npos || pending.size() > maxHeaderBytes) break;
                    if (!readMore()) throw http::Exception();
                }

                // Report oversized request lines separately from oversized headers
                if (lineTooLong) {
                    reject(414);
                    break;
                }
                if (headerEnd == std::string::npos || headerEnd + (isSimpleRequest ? 2 : 4) > maxHeaderBytes) {
                    reject(431);
                    break;
                }

                // Parse HTTP/1.x headers; simple HTTP/0.9 requests have no header block
                headers_map_t reqHeaders;
                if (!isSimpleRequest) {
                    try {
                        loadEarlyHeaders(reqHeaders, pending);
                    } catch (http::Exception&) {
                        reject(400);
                        break;
                    }
                }

                // Reject unsupported transfer coding and malformed Content-Length
                // Otherwise chunk data could be interpreted as a subsequent request
                size_t contentLength = 0;
                if (reqHeaders.contains("TRANSFER-ENCODING") ||
                    (reqHeaders.contains("CONTENT-LENGTH") && !parseUnsignedDecimal(reqHeaders["CONTENT-LENGTH"], contentLength))) {
                    reject(400);
                    break;
                }

                // Enforce the body limit before waiting for the declared payload
                if (contentLength > conf::MAX_REQUEST_BODY) {
                    reject(413);
                    break;
                }

                // Wait for this body only; extra bytes belong to the next request
                const size_t headerSize = headerEnd + (isSimpleRequest ? 2 : 4);
                while (pending.size() - headerSize < contentLength)
                    if (!readMore()) throw http::Exception();

                // Extract exactly one request and retain any pipelined surplus
                const size_t requestSize = headerSize + contentLength;
                const std::string raw = pending.substr(0, requestSize);
                pending.erase(0, requestSize);

                // Generate the response using the validated request and owned client IP
                RequestFlags flags;
                Request request(reqHeaders, raw, clientIPStr, useTLS, flags);
                std::unique_ptr<Response> response = genResponse(request);

                // Read Connection as a case-insensitive list, not one literal value
                std::string connValue = request.getHeader("Connection").value_or("");
                strToUpper(connValue);
                std::unordered_set<std::string> connOptions;
                splitStringUnique(connOptions, connValue, ',', true);

                // A close option wins over keep-alive, including across repeated headers
                // HTTP/1.1 persists by default; HTTP/1.0 requires explicit keep-alive
                --requestsLeft;
                const bool keepAlive = requestsLeft > 0 && conf::IS_KEEP_ALIVE_ENABLED &&
                    !connOptions.contains("CLOSE") &&
                    (request.getVersion() == "HTTP/1.1" ||
                        (request.getVersion() == "HTTP/1.0" && connOptions.contains("KEEP-ALIVE")));

                // Advertise the remaining request allowance, closing on the final one
                response->setHeader("Connection", keepAlive ? "keep-alive" : "close");
                if (keepAlive)
                    response->setHeader("Keep-Alive", "timeout=" + std::to_string(conf::KEEP_ALIVE_TIMEOUT) +
                        ", max=" + std::to_string(requestsLeft));

                // Send headers and body (HEAD sends headers only)
                const ssize_t status = response->streamBody(request.isMIMEAccepted("text/html"), request.getMethod() == METHOD::HEAD, sendFunc);

                // Log the final status, including errors detected while preparing the body
                ACCESS_LOG << request.getMethodStr() << ' '
                    << formatClientIP(request.getIPStr(), request.isDNT()) << ' '
                    << request.getPaths().rawPathFromRequest << " -- (" << response->getStatus()
                    << ") [" << request.getVersion() << ']' << std::endl;

                // Do not process queued requests after a failed write or forced closure
                if (!keepAlive || status < 0 || response->shouldCloseConnection()) break;
            }
        } catch (http::Exception&) {
            // Invalid/incomplete request, socket timeout, or shutdown: close this client
        } catch (const std::exception& e) {
            ERROR_LOG << "Request failed: " << e.what() << std::endl;
        } catch (...) {
            ERROR_LOG << "Unknown request failure" << std::endl;
        }

        // Release TLS state and the tracked socket on every exit path
        closeClientSocket(client, pSSL);
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

    std::ostream& operator<<(std::ostream& os, const Server& server) {
        os << "IPv" << (server.isIPv4() ? "4" : "6");
        if (server.usesTLS()) os << " w/ TLS";
        return os;
    }

}

#ifdef _WIN32
    #undef close
#endif