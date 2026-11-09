#ifndef __STRING_TOOLS_HPP
#define __STRING_TOOLS_HPP

#include <algorithm>
#include <cctype>
#include <ostream>
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

// Return true if path ends with .php (case-insensitive on Windows)
inline bool isPHPFile(const std::string_view path) {
#ifdef _WIN32
    if (path.size() >= 4)
        return equalsCi(path.substr(path.size() - 4), ".php");
    return false;
#else
    return path.ends_with(".php");
#endif
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

// Wrapper for streaming HTML-escaped characters (&, <, >, ", ') without allocations
struct HtmlEscaped {
    std::string_view text;

    friend std::ostream& operator<<(std::ostream& os, const HtmlEscaped& esc) {
        size_t start = 0;
        const size_t len = esc.text.size();
        for (size_t i = 0; i < len; ++i) {
            const char c = esc.text[i];
            const char* pSub = nullptr;
            size_t subLen = 0;
            switch (c) {
                case '&':  pSub = "&amp;";  subLen = 5; break;
                case '<':  pSub = "&lt;";   subLen = 4; break;
                case '>':  pSub = "&gt;";   subLen = 4; break;
                case '"':  pSub = "&quot;"; subLen = 6; break;
                case '\'': pSub = "&#39;";  subLen = 5; break;
                default:   continue;
            }
            if (i > start)
                os.write(esc.text.data() + start, static_cast<std::streamsize>(i - start));
            os.write(pSub, static_cast<std::streamsize>(subLen));
            start = i + 1;
        }
        if (start < len)
            os.write(esc.text.data() + start, static_cast<std::streamsize>(len - start));
        return os;
    }
};

// Returns string with HTML entities escaped (&, <, >, ", ')
inline std::string escapeHtml(std::string_view input) {
    size_t extra = 0;
    for (const char c : input) {
        if (c == '&' || c == '<' || c == '>' || c == '"' || c == '\'')
            extra += 5;
    }
    if (extra == 0) return std::string(input);

    std::string out;
    out.reserve(input.size() + extra);
    size_t start = 0;
    const size_t len = input.size();
    for (size_t i = 0; i < len; ++i) {
        const char* pSub = nullptr;
        switch (input[i]) {
            case '&':  pSub = "&amp;";  break;
            case '<':  pSub = "&lt;";   break;
            case '>':  pSub = "&gt;";   break;
            case '"':  pSub = "&quot;"; break;
            case '\'': pSub = "&#39;";  break;
            default:   continue;
        }
        if (i > start) out.append(input.substr(start, i - start));
        out.append(pSub);
        start = i + 1;
    }
    if (start < len) out.append(input.substr(start, len - start));
    return out;
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