#ifndef __STRING_TOOLS_HPP
#define __STRING_TOOLS_HPP

#include <algorithm>

#include "../pch/common.hpp"
#include "../../lib/brotli-cpp.hpp"

#include <zlib.h>

#define IO_SUCCESS 0
#define IO_FAILURE 1
#define IO_ABORTED 2

#define COMPRESS_DEFLATE 0
#define COMPRESS_GZIP 1
#define COMPRESS_BROTLI 2

inline void strToUpper(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
}

void splitStringUnique(std::unordered_set<std::string>&, std::string&, char, bool);
void stringReplaceAll(std::string&, const std::string&, const std::string&);
void trimString(std::string&);
void decodeURI(std::string&);
void formatHeaderCasing(std::string&);

int compressText(std::string&, const int);

bool isMostlyAscii(const std::string&, double=0.95);

#endif