#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <unistd.h>
#endif

#include "string_tools.hpp"
#include "../conf.hpp"

#define SOCKET_FAILURE 1
#define BIND_FAILURE 2
#define LISTEN_FAILURE 3

bool doesFileExist(const std::string&, const bool);
bool doesDirectoryExist(const std::string&, const bool);
int loadErrorDoc(const int, const std::string&, std::string&);
int loadConfHeaders(std::unordered_map<std::string, std::string>&);

void formatFileSize(size_t, std::string&);
void formatDate(const std::chrono::system_clock::duration, std::string&);

int loadDirectoryListing(std::string&, const std::string&, const std::string&);

// Used to bind TCP sockets
int bindTCPSocket(int&, const std::string, const port_t, const u_short);

// Debug profiling
long long debug_getTimestamp();

#endif