#ifndef __STRING_TOOLS_HPP
#define __STRING_TOOLS_HPP

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#define NO_COMPRESS 256
#define COMPRESS_DEFLATE 512
#define COMPRESS_GZIP 1024
#define COMPRESS_BROTLI 2048
#define COMPRESS_ZSTD 4096

inline void strToUpper(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
}

void splitString(std::vector<std::string>&, const std::string&, const char, const bool);
void splitStringUnique(std::unordered_set<std::string>&, const std::string&, const char, const bool);

inline void stringReplaceAll(std::string& haystack, const std::string& needle, const std::string& sub) {
    if (needle.empty()) return;
    size_t index = 0;
    while ((index = haystack.find(needle, index)) != std::string::npos) {
        haystack.replace(index, needle.size(), sub);
        // Skip inserted text; an empty replacement continues at the same index.
        index += sub.size();
    }
}

inline void trimString(std::string& str) {
    size_t start = str.find_first_not_of(" \t");
    if (start == std::string::npos) {
        str.clear();
        return;
    }
    str = str.substr(start, str.find_last_not_of(" \t") - start + 1);
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