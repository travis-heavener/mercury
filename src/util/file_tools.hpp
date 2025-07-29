#ifndef __FILE_TOOLS_HPP
#define __FILE_TOOLS_HPP

#include <filesystem>
#include <string>

#ifdef _WIN32
    // Fix issue w/ missing headers (before windows.h)
    #ifdef _WIN32_WINNT
        #undef _WIN32_WINNT
    #endif
    #define _WIN32_WINNT 0x0600

    #include <windows.h>
#endif

std::filesystem::path resolveCanonicalPath(const std::filesystem::path& path);

#endif