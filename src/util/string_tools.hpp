#ifndef __STRING_TOOLS_HPP
#define __STRING_TOOLS_HPP

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#define NO_COMPRESS 256
#define COMPRESS_DEFLATE 512
#define COMPRESS_GZIP 1024
#define COMPRESS_BROTLI 2048
#define COMPRESS_ZSTD 4096

// Convert string characters to uppercase in-place
inline void strToUpper(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
}

// Convert string characters to lowercase in-place
inline void strToLower(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
}

// Compare string views case-insensitively
inline bool equalsCi(const std::string_view a, const std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const char x, const char y) {
        return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
    });
}

void splitString(std::vector<std::string>&, const std::string&, const char, const bool);

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
    const size_t start = str.find_first_not_of(" \t");
    if (start == std::string::npos) {
        str.clear();
        return;
    }
    const size_t end = str.find_last_not_of(" \t");
    if (end + 1 < str.size()) str.erase(end + 1);
    if (start > 0) str.erase(0, start);
}

// Returns string view trimmed of leading and trailing whitespace
inline std::string_view trimStringView(std::string_view str) {
    const size_t start = str.find_first_not_of(" \t");
    if (start == std::string_view::npos) return {};
    const size_t end = str.find_last_not_of(" \t");
    return str.substr(start, end - start + 1);
}

// Returns true if string views match case-insensitively
inline bool strEqualsCaseInsensitive(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

void decodeURI(std::string&);

void formatHeaderCasing(std::string&);


inline size_t countAscii(const char* pData, const size_t size) {
    size_t asciiCount = 0;
    for (const char* p = pData; p < pData+size; ++p)
        if ((*p >= 0x20 && *p <= 0x7E) || *p == '\n' || *p == '\r' || *p == '\t')
            ++asciiCount;
    return asciiCount;
}

#endif