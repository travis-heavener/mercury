#include "tls.hpp"

SSL_CTX* initTLSContext() {
    OPENSSL_no_config();

    SSL_CTX* ctx = SSL_CTX_new( TLS_server_method() );
    if ( !ctx ) {
        ERROR_LOG << "Failed to create SSL context.\n";
        return nullptr;
    }

    // Load cert and key
    const std::string certPath = (conf::CWD / CERT_PATH).string();
    const std::string keyPath = (conf::CWD / KEY_PATH).string();

    if ( SSL_CTX_use_certificate_file(ctx, certPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERROR_LOG << "Failed to load ./conf/ssl/cert.pem\n";
        return nullptr;
    }

    if ( SSL_CTX_use_PrivateKey_file(ctx, keyPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERROR_LOG << "Failed to load ./conf/ssl/key.pem\n";
        return nullptr;
    }

    // Normalize nulls
    if (ctx == NULL)
        ctx = nullptr;

    // Return new path
    return ctx;
}