#include "version_checker.hpp"

#ifdef _WIN32
    #define close closesocket
#endif

// Used to fetch the latest version from remote
std::string fetchLatestVersion() {
    const std::string host = "wowtravis.com";
    const std::string path = "/mercury/latest";

    // 1. Resolve host
    addrinfo hints{};
    addrinfo* res;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(), "443", &hints, &res) != 0)
        return "";

    // 2. Create socket
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) return "";
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        return "";
    }

    // 3. Setup OpenSSL
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);

    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sock);
        return "";
    }

    // 4. Send request
    std::string req = "GET " + path + " HTTP/1.1\r\n"
                      "Host: " + host + "\r\n"
                      "Connection: close\r\n\r\n";
    SSL_write(ssl, req.c_str(), req.size());

    // 5. Read response
    std::string response;
    char buffer[4096];
    int n;
    while ((n = SSL_read(ssl, buffer, sizeof(buffer))) > 0)
        response.append(buffer, n);

    // 6. Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sock);
    freeaddrinfo(res);

    // 7. Strip headers
    const size_t pos = response.find("\r\n\r\n");
    if (pos != std::string::npos)
        return response.substr(pos + 4);
    return response;
}

#ifdef _WIN32
    #undef close
#endif