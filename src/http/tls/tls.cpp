#include "tls.hpp"

#include <iostream>
#include <memory>
#include <string>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "../../conf/conf.hpp"
#include "../../logs/logger.hpp"

#define CERT_PATH "conf/ssl/cert.pem"
#define KEY_PATH "conf/ssl/key.pem"

namespace http {

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
        SSL_CTX_free(ctx);
        return nullptr;
    }

    if ( SSL_CTX_use_PrivateKey_file(ctx, keyPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        ERROR_LOG << "Failed to load ./conf/ssl/key.pem" << std::endl;
        SSL_CTX_free(ctx);
        return nullptr;
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        ERROR_LOG << "Private key does not match public certificate" << std::endl;
        SSL_CTX_free(ctx);
        return nullptr;
    }

    // Normalize nulls
    if (ctx == NULL)
        ctx = nullptr;

    // Return new path
    return ctx;
}

void printTLSInfo() {
    if (!conf::USE_TLS) {
        std::cout << "> TLS is disabled in configuration (TLSPort: off)" << std::endl;
        return;
    }

    const std::string certPath = (conf::CWD / CERT_PATH).string();
    FILE* fp = fopen(certPath.c_str(), "r");
    if (!fp) {
        std::cout << "> TLS enabled on port " << conf::TLS_PORT
                  << " (certificate not found: " << certPath << ")" << std::endl;
        return;
    }

    X509* pCert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!pCert) {
        std::cout << "> Failed to parse certificate from " << certPath << std::endl;
        return;
    }

    // Wrap in unique_ptr for safe cleanup
    std::unique_ptr<X509, decltype(&X509_free)> certGuard(pCert, X509_free);

    char subjectBuf[512] = {0};
    char issuerBuf[512] = {0};
    X509_NAME_oneline(X509_get_subject_name(pCert), subjectBuf, sizeof(subjectBuf));
    X509_NAME_oneline(X509_get_issuer_name(pCert), issuerBuf, sizeof(issuerBuf));

    const ASN1_TIME* pNotBefore = X509_get0_notBefore(pCert);
    const ASN1_TIME* pNotAfter = X509_get0_notAfter(pCert);

    int days = 0, secs = 0;
    std::string expiryStatus = "Unknown";
    if (ASN1_TIME_diff(&days, &secs, nullptr, pNotAfter)) {
        if (days < 0)
            expiryStatus = "EXPIRED (" + std::to_string(-days) + " days ago)";
        else if (days <= 30)
            expiryStatus = "WARNING: Expires in " + std::to_string(days) + " days";
        else
            expiryStatus = "Valid (" + std::to_string(days) + " days remaining)";
    }

    // Used to format OpenSSL ASN1_TIME into printable string
    auto formatASN1Time = [](const ASN1_TIME* pTime) -> std::string {
        if (!pTime) return "Unknown";
        BIO* pBio = BIO_new(BIO_s_mem());
        if (!pBio) return "Unknown";
        ASN1_TIME_print(pBio, pTime);
        char buf[128] = {0};
        const int len = BIO_read(pBio, buf, sizeof(buf) - 1);
        BIO_free(pBio);
        return len > 0 ? std::string(buf, len) : "Unknown";
    };

    std::cout << "> TLS Status & Certificate:\n"
              << "  Port:          " << conf::TLS_PORT << '\n'
              << "  Subject:       " << (subjectBuf[0] ? subjectBuf : "Unknown") << '\n'
              << "  Issuer:        " << (issuerBuf[0] ? issuerBuf : "Unknown") << '\n'
              << "  Valid From:    " << formatASN1Time(pNotBefore) << '\n'
              << "  Valid Until:   " << formatASN1Time(pNotAfter) << '\n'
              << "  Status:        " << expiryStatus << '\n'
              << "  Protocol:      TLSv1.3 (OpenSSL)" << std::endl;
}

}

#undef CERT_PATH
#undef KEY_PATH