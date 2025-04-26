#ifndef __TLS_HPP
#define __TLS_HPP

#include <string>

#include <openssl/ssl.h>

#include "../conf.hpp"
#include "../util/toolbox.hpp"

#define CERT_PATH "conf/ssl/cert.pem"
#define KEY_PATH "conf/ssl/key.pem"

SSL_CTX* initTLSContext();

#endif