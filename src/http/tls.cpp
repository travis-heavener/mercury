#include "tls.hpp"

#include <string>

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"

#define CERT_PATH "conf/ssl/cert.pem"
#define KEY_PATH "conf/ssl/key.pem"

SSL_CTX* initTLSContext() {
    OPENSSL_no_config();

    SSL_CTX* ctx = SSL_CTX_new( TLS_server_method() );
    if ( !ctx ) {
        ERROR_LOG << "Failed to create SSL context." << std::endl;
        return nullptr;
    }

    // Load cert and key
    const std::string certPath = (conf::CWD / CERT_PATH).string();
    const std::string keyPath = (conf::CWD / KEY_PATH).string();

    if ( SSL_CTX_use_certificate_file(ctx, certPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERROR_LOG << "Failed to load ./conf/ssl/cert.pem" << std::endl;
        return nullptr;
    }

    if ( SSL_CTX_use_PrivateKey_file(ctx, keyPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERROR_LOG << "Failed to load ./conf/ssl/key.pem" << std::endl;
        return nullptr;
    }

    // Normalize nulls
    if (ctx == NULL)
        ctx = nullptr;

    // Return new path
    return ctx;
}

#undef CERT_PATH
#undef KEY_PATH