#ifndef __TLS_HPP
#define __TLS_HPP

#include <string>

#include <openssl/ssl.h>

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"

#define CERT_PATH "conf/ssl/cert.pem"
#define KEY_PATH "conf/ssl/key.pem"

SSL_CTX* initTLSContext();

#endif