#include "server.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "../conf/conf.hpp"
#include "../io/file.hpp"
#include "../logs/logger.hpp"
#include "../util/ratelimit.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

#include "tools.hpp"
#include "telemetry/stats.hpp"
#include "version/handler_1_1.hpp"
#include "version/handler_1_0.hpp"
#include "version/handler_0_9.hpp"


#ifdef _WIN32
    #define close closesocket
#endif

namespace http {

    Server::Server(const port_t port, const bool useTLS) : port(port),
        threadPool(), useTLS(useTLS) {};

    Server::~Server() {
        if (pSSL_CTX != nullptr) {
            SSL_CTX_free(pSSL_CTX);
            pSSL_CTX = nullptr;
        }
    }

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
        if (useTLS && pSSL_CTX != nullptr) {
            SSL_CTX_free(pSSL_CTX);
            pSSL_CTX = nullptr;
        }
    }

    int Server::bindSocket() {
        // Open the socket
        #ifdef SOCK_CLOEXEC
            this->sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
        #else
            this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        #endif

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
                << (lastErrno == 13 ? ", do you have elevated permissions (e.g. sudo, doas)?" : "") // Improve log for errno 13 (needs elevated permissions)
                << std::endl;

            // Improve log for errno 13 (needs elevated permissions)
            if (lastErrno == 13)
                std::cerr << "Failed to bind socket (" << *this << "), errno: " << lastErrno << ", do you have elevated permissions (e.g. sudo, doas)?" << std::endl;
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

        if (!conf::DISABLE_LOGS)
            ACCESS_LOG << "Listening on port " << this->port << " (" << *this << ")." << std::endl;
        return 0;
    }

    ssize_t Server::readClientSock(char* readBuffer, const int client, SSL* pSSL) {
        ssize_t n;
        if (this->useTLS)
            n = SSL_read(pSSL, readBuffer, conf::REQUEST_BUFFER_SIZE);
        else
            n = recv(client, readBuffer, conf::REQUEST_BUFFER_SIZE, 0);

        if (n > 0)
            ServerStats::getInstance().recordBytesRead(static_cast<size_t>(n));
        return n;
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
        if (status > 0)
            ServerStats::getInstance().recordBytesSent(static_cast<size_t>(status));
        return (status <= 0) ? -1 : status;
    }

    int Server::closeSocket(const int sock) {
        #ifdef _WIN32
            shutdown(sock, SD_BOTH);
        #endif
        return close(sock);
    }

    int Server::closeClientSocket(const int sock, SSL* pSSL) {
        ServerStats::flush();
        this->untrackClient(sock);
        if (this->useTLS && pSSL != nullptr) SSL_free(pSSL); // Cleanup TLS
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

    // Toggle socket blocking state across platforms
    static bool setSocketBlocking(const int sock, const bool blocking) {
        #ifdef _WIN32
            u_long mode = blocking ? 0 : 1;
            return ioctlsocket(sock, FIONBIO, &mode) == 0;
        #else
            const int flags = fcntl(sock, F_GETFL, 0);
            if (flags < 0) return false;
            return fcntl(sock, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) >= 0;
        #endif
    }

    // Configure socket read and write timeouts
    static void setSocketTimeout(const int sock, const int timeoutSec) {
        #ifdef _WIN32
            const DWORD timeoutMs = static_cast<DWORD>(timeoutSec) * 1000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        #else
            struct timeval tv{};
            tv.tv_sec = timeoutSec;
            tv.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
        #endif
    }

    // Stream sanitized paths without heap allocations or temporary string copies
    struct SanitizedLogPath {
        std::string_view path;

        friend std::ostream& operator<<(std::ostream& os, const SanitizedLogPath& sp) {
            constexpr char HEX[] = "0123456789ABCDEF";
            size_t start = 0;
            for (size_t i = 0; i < sp.path.size(); ++i) {
                const unsigned char c = static_cast<unsigned char>(sp.path[i]);
                if (c < 32 || c >= 127) {
                    if (i > start) os.write(sp.path.data() + start, i - start);
                    os << "\\x" << HEX[c >> 4] << HEX[c & 0x0F];
                    start = i + 1;
                }
            }
            if (start < sp.path.size()) {
                os.write(sp.path.data() + start, sp.path.size() - start);
            }
            return os;
        }
    };

    // Complete TLS handshake w/ deadline to prevent stalling worker threads
    bool Server::acceptTLS(SSL* pSSL, const int client, const int timeoutMS) {
        if (!setSocketBlocking(client, false)) return false;

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMS);
        bool success = false;

        while (!this->isExiting) {
            const int rv = SSL_accept(pSSL);
            if (rv > 0) {
                success = true;
                break;
            }

            const int err = SSL_get_error(pSSL, rv);
            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
                break;

            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;

            const int remMS = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            struct pollfd pfd{};
            pfd.fd = client;
            pfd.events = (err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT);
            pfd.revents = 0;

            const int polled = poll(&pfd, 1, std::min(remMS, 1000));
            if (polled < 0) break;
        }

        setSocketBlocking(client, true);
        return success;
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
        #if defined(__linux__) && defined(SOCK_CLOEXEC)
            const int client = accept4(listener, (struct sockaddr*)&clientAddr, &clientLen, SOCK_CLOEXEC);
            if (client < 0 && (errno == ENOSYS || errno == EINVAL))
                return accept(listener, (struct sockaddr*)&clientAddr, &clientLen);
            return client;
        #else
            return accept(listener, (struct sockaddr*)&clientAddr, &clientLen);
        #endif
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

            // Apply read and write deadlines to prevent slow clients from stalling worker threads
            setSocketTimeout(client, conf::KEEP_ALIVE_TIMEOUT);

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

            // Apply read and write deadlines to prevent slow clients from stalling worker threads
            setSocketTimeout(client, conf::KEEP_ALIVE_TIMEOUT);

            // Complete the TLS handshake w/ timeout before reading HTTP data
            if (useTLS) {
                pSSL = SSL_new(pSSL_CTX);
                if (pSSL == nullptr) throw http::Exception();
                SSL_set_fd(pSSL, client);
                if (!this->acceptTLS(pSSL, client, conf::KEEP_ALIVE_TIMEOUT * 1000))
                    throw http::Exception();
            }

            // Keep unread data across requests; one read may contain several requests
            std::vector<char> readBuffer(conf::REQUEST_BUFFER_SIZE);
            std::string pending;
            unsigned int requestsLeft = conf::MAX_KEEP_ALIVE_REQUESTS;
            headers_map_t reqHeaders;

            // Pass socket writes to Response, which retries partial writes
            std::function<ssize_t(const char*, const size_t)> sendFunc = [&](const char* data, size_t size) {
                return writeClientSock(client, pSSL, data, size);
            };

            // Append the next available block without discarding buffered request data
            auto readMore = [&](const int timeoutMs = conf::KEEP_ALIVE_TIMEOUT * 1000) -> bool {
                if (isExiting || timeoutMs <= 0) return false;

                // Decrypted TLS bytes can already be waiting without socket activity
                if (!useTLS || SSL_pending(pSSL) == 0) {
                    struct pollfd pfd{};
                    pfd.fd = client;
                    const ssize_t ready = waitForClientData(pfd, timeoutMs);
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
                ServerStats::getInstance().recordRequest();
                ServerStats::getInstance().recordStatusCode(status);
            };

            // Process requests until the connection limit, shutdown, or an error
            while (requestsLeft > 0 && !isExiting) {
                size_t headerEnd;
                bool lineTooLong = false, isSimpleRequest = false;
                constexpr size_t maxHeaderBytes = 64 * 1024;

                // Overall deadline for receiving complete request headers
                const auto headerDeadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(conf::KEEP_ALIVE_TIMEOUT);

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

                    const auto now = std::chrono::steady_clock::now();
                    if (now >= headerDeadline) throw http::Exception();
                    const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(headerDeadline - now).count();
                    if (!readMore(static_cast<int>(remainingMs))) throw http::Exception();
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
                reqHeaders.clear();
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
                const std::string_view raw(pending.data(), requestSize);

                ServerStats::getInstance().recordRequest();

                // Generate the response using the validated request and owned client IP
                RequestFlags flags;
                Request request(reqHeaders, raw, clientIPStr, useTLS, flags);

                if (pending.size() == requestSize)
                    pending.clear();
                else
                    pending.erase(0, requestSize);

                // Enforce rate limiting if enabled
                std::unique_ptr<Response> response = nullptr;
                double retryAfter = 0.0;
                const bool isRateLimited = conf::IS_RATE_LIMIT_ENABLED &&
                    !util::RateLimiter::getInstance().checkAndConsume(clientIPStr, request.isDNT(), retryAfter);

                if (isRateLimited) {
                    response = std::make_unique<Response>(request.getVersion().empty() ? "HTTP/1.1" : request.getVersion());
                    response->setStatus(429);
                    response->setHeader("Retry-After", std::to_string(static_cast<int>(retryAfter)));
                    response->setHeader("Connection", "close");
                    if (request.isMIMEAccepted("text/html"))
                        response->loadBodyFromErrorDoc(429);
                } else {
                    response = genResponse(request);
                }

                // Read Connection as a case-insensitive list without allocating temporary collections
                bool hasClose = isRateLimited, hasKeepAlive = false;
                const auto itConn = reqHeaders.find("CONNECTION");
                if (itConn != reqHeaders.end()) {
                    std::string_view connView = itConn->second;
                    size_t start = 0;
                    while (start < connView.size()) {
                        const size_t comma = connView.find(',', start);
                        std::string_view token = (comma == std::string_view::npos)
                            ? connView.substr(start)
                            : connView.substr(start, comma - start);
                        start = (comma == std::string_view::npos) ? connView.size() : comma + 1;

                        const size_t first = token.find_first_not_of(" \t");
                        if (first != std::string_view::npos) {
                            const size_t last = token.find_last_not_of(" \t");
                            token = token.substr(first, last - first + 1);
                            if (equalsCi(token, "close")) hasClose = true;
                            else if (equalsCi(token, "keep-alive")) hasKeepAlive = true;
                        }
                    }
                }

                // A close option wins over keep-alive, including across repeated headers
                // HTTP/1.1 persists by default; HTTP/1.0 requires explicit keep-alive
                if (requestsLeft > 0) --requestsLeft;
                if (isRateLimited) requestsLeft = 0;
                const bool keepAlive = requestsLeft > 0 && conf::IS_KEEP_ALIVE_ENABLED &&
                    !hasClose &&
                    (request.getVersion() == "HTTP/1.1" ||
                        (request.getVersion() == "HTTP/1.0" && hasKeepAlive));

                // Advertise the remaining request allowance, closing on the final one
                response->setHeader("Connection", keepAlive ? "keep-alive" : "close");
                if (keepAlive)
                    response->setHeader("Keep-Alive", "timeout=" + std::to_string(conf::KEEP_ALIVE_TIMEOUT) +
                        ", max=" + std::to_string(requestsLeft));

                // Send headers and body (HEAD sends headers only)
                const ssize_t status = response->streamBody(request.isMIMEAccepted("text/html"), request.getMethod() == METHOD::HEAD, sendFunc);

                ServerStats::getInstance().recordStatusCode(response->getStatus());

                // Log the final status, including errors detected while preparing the body
                if (!conf::DISABLE_LOGS) {
                    ACCESS_LOG << request.getMethodStr() << ' '
                        << formatClientIP(request.getIPStr(), request.isDNT()) << ' '
                        << SanitizedLogPath{request.getPaths().rawPathFromRequest} << " -- (" << response->getStatus()
                        << ") [" << request.getVersion() << ']' << std::endl;
                }

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

    size_t Server::getActiveClientCount() {
        std::unique_lock lock(clientsMutex);
        return clientSocks.size();
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