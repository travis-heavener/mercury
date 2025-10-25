#ifndef __WINHEADER_HPP
#define __WINHEADER_HPP

// A collective Windows header to streamline the order of includes

// Fix inet_ntop & canonical WinAPI methods
#ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0600
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define poll WSAPoll
#define ssize_t SSIZE_T

#ifdef errno
    #undef errno
    #define errno WSAGetLastError()
#endif

#endif