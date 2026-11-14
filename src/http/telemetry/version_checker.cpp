#include "version_checker.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
    #include "../../winheader.hpp"
    #include <wincrypt.h>
    #ifdef X509_NAME
        #undef X509_NAME
    #endif
    #ifdef X509_EXTENSIONS
        #undef X509_EXTENSIONS
    #endif
#else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <poll.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "../../conf/conf.hpp"

namespace {

    struct ResolverState {
        std::mutex mutex;
        std::condition_variable condition;
        bool done = false;
        std::shared_ptr<addrinfo> result;
    };

    // Bound outstanding lookups even if system resolver never returns
    static std::shared_ptr<addrinfo> resolveWithDeadline(const std::string& host, const std::string& port,
            const addrinfo& hints, const std::chrono::steady_clock::time_point& deadline) {
        static auto pActive = std::make_shared<std::atomic<bool>>(false);
        auto active = pActive;
        bool expected = false;
        if (!active->compare_exchange_strong(expected, true)) return {};
        std::shared_ptr<ResolverState> state;
        try { state = std::make_shared<ResolverState>(); }
        catch (...) { active->store(false); return {}; }
        std::unique_lock<std::mutex> lock(state->mutex);
        try {
            std::thread([state, active, host, port, hints]() {
                addrinfo* raw = nullptr;
                std::shared_ptr<addrinfo> result;
                try {
                    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &raw) == 0 && raw) {
                        std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owned(raw, freeaddrinfo);
                        raw = nullptr;
                        result = std::shared_ptr<addrinfo>(std::move(owned));
                    }
                } catch (...) { }
                if (raw) freeaddrinfo(raw);
                {
                    std::lock_guard<std::mutex> guard(state->mutex);
                    state->result = std::move(result);
                    state->done = true;
                }
                active->store(false);
                state->condition.notify_all();
            }).detach();
        } catch (...) {
            active->store(false);
            return {};
        }
        if (!state->condition.wait_until(lock, deadline, [&] { return state->done; })) return {};
        return state->result;
    }

    // Toggle socket non-blocking mode across platforms
    static bool setSocketBlocking(int sock, bool blocking) {
        #ifdef _WIN32
            u_long mode = blocking ? 0 : 1;
            return ioctlsocket(sock, FIONBIO, &mode) == 0;
        #else
            const int flags = fcntl(sock, F_GETFL, 0);
            if (flags < 0) return false;
            return fcntl(sock, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) >= 0;
        #endif
    }

    // Await socket readability or writability with strict deadline
    static bool waitForSocket(int sock, bool forRead, const std::chrono::steady_clock::time_point& deadline) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;
        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (remainingMs <= 0) return false;

        #ifndef _WIN32
            struct pollfd pfd{};
            pfd.fd = sock;
            pfd.events = forRead ? POLLIN : POLLOUT;
            return poll(&pfd, 1, static_cast<int>(remainingMs)) > 0 && (pfd.revents & pfd.events);
        #else
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);

        struct timeval tv;
        tv.tv_sec = static_cast<long>(remainingMs / 1000);
        tv.tv_usec = static_cast<long>((remainingMs % 1000) * 1000);

        int rc = 0;
        if (forRead) {
            rc = select(sock + 1, &fds, nullptr, nullptr, &tv);
        } else {
            rc = select(sock + 1, nullptr, &fds, nullptr, &tv);
        }
        return rc > 0;
        #endif
    }

    // Connect to endpoint within deadline using non-blocking socket
    static bool connectWithDeadline(int sock, const struct sockaddr* pAddr, socklen_t addrLen,
                                   const std::chrono::steady_clock::time_point& deadline) {
        if (!setSocketBlocking(sock, false)) return false;

        int rc = connect(sock, pAddr, addrLen);
        if (rc == 0) return true;

        #ifdef _WIN32
            if (WSAGetLastError() != WSAEWOULDBLOCK) return false;
        #else
            if (errno != EINPROGRESS) return false;
        #endif

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;
        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (remainingMs <= 0) return false;

        #ifndef _WIN32
            struct pollfd pfd{};
            pfd.fd = sock;
            pfd.events = POLLOUT;
            rc = poll(&pfd, 1, static_cast<int>(remainingMs));
            if (rc <= 0) return false;
            int error = 0;
            socklen_t len = sizeof(error);
            return getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0;
        #else
        fd_set writefds, exceptfds;
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
        FD_SET(sock, &writefds);
        FD_SET(sock, &exceptfds);

        struct timeval tv;
        tv.tv_sec = static_cast<long>(remainingMs / 1000);
        tv.tv_usec = static_cast<long>((remainingMs % 1000) * 1000);

        rc = select(sock + 1, nullptr, &writefds, &exceptfds, &tv);
        if (rc <= 0) return false;

        if (FD_ISSET(sock, &exceptfds)) return false;

        if (FD_ISSET(sock, &writefds)) {
            int error = 0;
            socklen_t len = sizeof(error);
            #ifdef _WIN32
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) < 0 || error != 0) {
                    return false;
                }
            #else
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                    return false;
                }
            #endif
            return true;
        }
        return false;
        #endif
    }

    #ifdef _WIN32
        // Load Windows system root certificates into OpenSSL store
        static void loadWindowsTrustRoots(SSL_CTX* pCtx) {
            for (const wchar_t* pStoreName : {L"ROOT", L"CA"}) {
                HCERTSTORE hStore = CertOpenSystemStoreW(0, pStoreName);
                if (hStore) {
                    X509_STORE* pStore = SSL_CTX_get_cert_store(pCtx);
                    PCCERT_CONTEXT pContext = nullptr;
                    while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != nullptr) {
                        const unsigned char* pData = pContext->pbCertEncoded;
                        X509* pCert = d2i_X509(nullptr, &pData, pContext->cbCertEncoded);
                        if (pCert) {
                            X509_STORE_add_cert(pStore, pCert);
                            X509_free(pCert);
                        }
                    }
                    CertCloseStore(hStore, 0);
                }
            }
        }
    #endif

    // RAII container to ensure socket and TLS state cleanup on all exit paths
    struct NetworkResources {
        int sock = -1;
        std::shared_ptr<addrinfo> pAddrInfo;
        SSL_CTX* pCtx = nullptr;
        SSL* pSSL = nullptr;

        ~NetworkResources() {
            if (pSSL) {
                SSL_shutdown(pSSL);
                SSL_free(pSSL);
            }
            if (pCtx) {
                SSL_CTX_free(pCtx);
            }
            if (sock >= 0) {
                #ifdef _WIN32
                    closesocket(sock);
                #else
                    close(sock);
                #endif
            }
        }
    };

} // namespace

// Fetch latest version from remote endpoint under strict verification and deadline
std::string fetchLatestVersion(const std::string& host, const std::string& path, uint16_t port) {
    constexpr auto TIMEOUT = std::chrono::seconds(2);
    const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    constexpr size_t MAX_RESPONSE_SIZE = 8192;

    NetworkResources res;

    // 1. Resolve host
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    const std::string portStr = std::to_string(port);
    res.pAddrInfo = resolveWithDeadline(host, portStr, hints, deadline);
    if (!res.pAddrInfo) {
        return "";
    }

    // 2. Create non-blocking socket and connect under deadline
    #ifdef SOCK_CLOEXEC
        res.sock = socket(res.pAddrInfo->ai_family, res.pAddrInfo->ai_socktype | SOCK_CLOEXEC, res.pAddrInfo->ai_protocol);
    #else
        res.sock = socket(res.pAddrInfo->ai_family, res.pAddrInfo->ai_socktype, res.pAddrInfo->ai_protocol);
    #endif
    if (res.sock < 0) {
        return "";
    }

    if (!connectWithDeadline(res.sock, res.pAddrInfo->ai_addr, res.pAddrInfo->ai_addrlen, deadline)) {
        return "";
    }

    // 3. Initialize OpenSSL client context with peer verification
    const SSL_METHOD* pMethod = TLS_client_method();
    res.pCtx = SSL_CTX_new(pMethod);
    if (!res.pCtx) {
        return "";
    }

    // Enforce TLS 1.2 minimum version
    SSL_CTX_set_min_proto_version(res.pCtx, TLS1_2_VERSION);

    // Load system root certificates and enforce peer verification
    SSL_CTX_set_default_verify_paths(res.pCtx);
    #ifdef _WIN32
        loadWindowsTrustRoots(res.pCtx);
    #else
        // Locate distro trust bundle when bundled OpenSSL defaults point into build directory
        if (std::getenv("SSL_CERT_FILE") == nullptr && std::getenv("SSL_CERT_DIR") == nullptr) {
            constexpr const char* TRUST_BUNDLES[] = {
                "/etc/ssl/cert.pem",
                "/etc/ssl/certs/ca-certificates.crt",
                "/etc/pki/tls/certs/ca-bundle.crt",
                "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"
            };
            for (const char* pBundle : TRUST_BUNDLES) {
                if (SSL_CTX_load_verify_locations(res.pCtx, pBundle, nullptr) == 1) break;
                ERR_clear_error();
            }
        }
    #endif
    SSL_CTX_set_verify(res.pCtx, SSL_VERIFY_PEER, nullptr);

    res.pSSL = SSL_new(res.pCtx);
    if (!res.pSSL) {
        return "";
    }

    if (SSL_set_fd(res.pSSL, res.sock) <= 0) {
        return "";
    }

    // Check if host is numeric IP or domain name for SNI and certificate validation
    struct in_addr in4;
    struct in6_addr in6;
    const bool isNumericIp = (inet_pton(AF_INET, host.c_str(), &in4) == 1) ||
                             (inet_pton(AF_INET6, host.c_str(), &in6) == 1);
    SSL_set_hostflags(res.pSSL, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (isNumericIp) {
        if (SSL_set1_ipaddr(res.pSSL, host.c_str()) != 1) {
            return "";
        }
    } else {
        SSL_set_tlsext_host_name(res.pSSL, host.c_str());
        if (SSL_set1_dnsname(res.pSSL, host.c_str()) != 1) {
            return "";
        }
    }

    // 4. Perform TLS handshake under deadline
    while (true) {
        if (std::chrono::steady_clock::now() >= deadline) return "";
        int rc = SSL_connect(res.pSSL);
        if (rc == 1) break;

        int err = SSL_get_error(res.pSSL, rc);
        if (err == SSL_ERROR_WANT_READ) {
            if (!waitForSocket(res.sock, true, deadline)) return "";
        } else if (err == SSL_ERROR_WANT_WRITE) {
            if (!waitForSocket(res.sock, false, deadline)) return "";
        } else {
            return "";
        }
    }

    // Verify certificate verification result and peer certificate presence
    if (SSL_get_verify_result(res.pSSL) != X509_V_OK) {
        return "";
    }
    X509* pPeerCert = SSL_get_peer_certificate(res.pSSL);
    if (!pPeerCert) {
        return "";
    }
    X509_free(pPeerCert);

    // 5. Send HTTP request under deadline
    const std::string hostHeader = (port == 443) ? host : (host + ":" + portStr);
    const std::string req = "GET " + path + " HTTP/1.1\r\n"
                            "Host: " + hostHeader + "\r\n"
                            "User-Agent: Mercury/" + conf::VERSION + "\r\n"
                            "Connection: close\r\n\r\n";

    size_t totalWritten = 0;
    while (totalWritten < req.size()) {
        if (std::chrono::steady_clock::now() >= deadline) return "";
        int rc = SSL_write(res.pSSL, req.c_str() + totalWritten, static_cast<int>(req.size() - totalWritten));
        if (rc > 0) {
            totalWritten += static_cast<size_t>(rc);
        } else {
            int err = SSL_get_error(res.pSSL, rc);
            if (err == SSL_ERROR_WANT_WRITE) {
                if (!waitForSocket(res.sock, false, deadline)) return "";
            } else if (err == SSL_ERROR_WANT_READ) {
                if (!waitForSocket(res.sock, true, deadline)) return "";
            } else {
                return "";
            }
        }
    }

    // 6. Read HTTP response under deadline with bounded buffer cap
    std::string response;
    char buffer[4096];
    while (response.size() < MAX_RESPONSE_SIZE) {
        if (std::chrono::steady_clock::now() >= deadline) return "";
        int rc = SSL_read(res.pSSL, buffer, sizeof(buffer));
        if (rc > 0) {
            if (response.size() + static_cast<size_t>(rc) > MAX_RESPONSE_SIZE) {
                return "";
            }
            response.append(buffer, static_cast<size_t>(rc));
        } else if (rc == 0) {
            break;
        } else {
            int err = SSL_get_error(res.pSSL, rc);
            if (err == SSL_ERROR_WANT_READ) {
                if (!waitForSocket(res.sock, true, deadline)) return "";
            } else if (err == SSL_ERROR_WANT_WRITE) {
                if (!waitForSocket(res.sock, false, deadline)) return "";
            } else if (err == SSL_ERROR_ZERO_RETURN) {
                break;
            } else {
                return "";
            }
        }
    }

    // 7. Validate HTTP 200 status and strip headers
    if (response.rfind("HTTP/1.1 200", 0) != 0 && response.rfind("HTTP/1.0 200", 0) != 0) {
        return "";
    }

    const size_t pos = response.find("\r\n\r\n");
    if (pos == std::string::npos) {
        return "";
    }

    std::string body = response.substr(pos + 4);
    while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' ')) {
        body.pop_back();
    }

    // Validate version format: must begin with "Mercury v" and be bounded in size
    if (body.size() < 11 || body.size() > 64 || body.rfind("Mercury v", 0) != 0) {
        return "";
    }

    return body;
}
