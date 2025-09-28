#ifndef __STRING_TOOLS_HPP
#define __STRING_TOOLS_HPP

#include <algorithm>

#include "../pch/common.hpp"

#define COMPRESS_DEFLATE 0
#define COMPRESS_GZIP 1
#define COMPRESS_BROTLI 2
#define NO_COMPRESS 3

inline void strToUpper(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
}

void splitString(std::vector<std::string>&, const std::string&, const char, const bool);
void splitStringUnique(std::unordered_set<std::string>&, const std::string&, const char, const bool);

inline void stringReplaceAll(std::string& haystack, const std::string& needle, const std::string& sub) {
    if (needle.empty()) return;
    size_t index = 0;
    while ((index = haystack.find(needle, index)) != std::string::npos) {
        haystack.replace(index, needle.size(), sub);
        index += sub.size();
    }
}

inline void trimString(std::string& str) {
    size_t start = str.find_first_not_of(' ');
    if (start == std::string::npos) {
        str.clear();
        return;
    }
    str = str.substr(start, str.find_last_not_of(' ') - start + 1);
}

void decodeURI(std::string&);

void formatHeaderCasing(std::string&);

// Reads from the input string until the next line or end,
// starting at startIndex and updating it in place (by ref).
// Returns true if there was data read, false otherwise.
bool readLine(const std::string& input, std::string& lineBuf, size_t& startIndex);

inline size_t countAscii(const char* pData, const size_t size) {
    size_t asciiCount = 0;
    for (const char* p = pData; p < pData+size; ++p)
        if ((*p >= 0x20 && *p <= 0x7E) || *p == '\n' || *p == '\r' || *p == '\t')
            ++asciiCount;
    return asciiCount;
}

#endif