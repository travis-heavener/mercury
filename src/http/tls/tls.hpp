#ifndef __TLS_HPP
#define __TLS_HPP

#include <openssl/ssl.h>

namespace http {
    SSL_CTX* initTLSContext();
    void printTLSInfo();
}

#endif
