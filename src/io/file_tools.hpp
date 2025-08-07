#ifndef __FILE_TOOLS_HPP
#define __FILE_TOOLS_HPP

#include <filesystem>
#include <string>

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

std::filesystem::path resolveCanonicalPath(const std::filesystem::path& path);

#endif