#ifndef __HTTP_VERSION_CHECKER_HPP
#define __HTTP_VERSION_CHECKER_HPP

#include <cstring>
#include <iostream>
#include <string>

#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef _WIN32
    #include "../winheader.hpp"
#else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

// Used to fetch the latest version from remote
std::string fetchLatestVersion();

#endif