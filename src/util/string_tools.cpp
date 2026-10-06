#include "string_tools.hpp"

void splitString(std::vector<std::string>& vec, const std::string& string, const char delimiter, const bool stripWhitespace) {
    size_t startIndex = 0;
    for (size_t i = 0; i < string.size(); ++i) {
        if (string[i] == delimiter) {
            std::string substr = string.substr(startIndex, i-startIndex);
            if (stripWhitespace)
                trimString(substr);
            if (substr.size() > 0)
                vec.push_back(substr);
            startIndex = i+1;
        }
    }

    // Append last snippet
    if (startIndex < string.size()) {
        std::string substr = string.substr(startIndex);
        if (stripWhitespace)
            trimString(substr);
        if (substr.size() > 0)
            vec.push_back(substr);
    }
}

void splitStringUnique(std::unordered_set<std::string>& set, const std::string& string, const char delimiter, const bool stripWhitespace) {
    size_t startIndex = 0;
    for (size_t i = 0; i < string.size(); ++i) {
        if (string[i] == delimiter) {
            std::string substr = string.substr(startIndex, i-startIndex);
            if (stripWhitespace)
                trimString(substr);
            if (substr.size() > 0)
                set.insert(substr);
            startIndex = i+1;
        }
    }

    // Append last snippet
    if (startIndex < string.size()) {
        std::string substr = string.substr(startIndex);
        if (stripWhitespace)
            trimString(substr);
        if (substr.size() > 0)
            set.insert(substr);
    }
}

void decodeURI(std::string& str) {
    auto hexValue = [](const char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string decoded;
    decoded.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c == '%') {
            if (i + 2 >= str.size()) throw std::invalid_argument("Incomplete URI escape");
            const int high = hexValue(str[i + 1]), low = hexValue(str[i + 2]);
            if (high < 0 || low < 0) throw std::invalid_argument("Invalid URI escape");
            c = static_cast<unsigned char>((high << 4) | low);
            i += 2;
        }
        if (c == 0) throw std::invalid_argument("NUL in URI");
        decoded.push_back(static_cast<char>(c));
    }
    str = std::move(decoded);
}

void formatHeaderCasing(std::string& header) {
    bool isNextCharCapital = true;
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == '-') {
            isNextCharCapital = true;
        } else {
            if (isNextCharCapital)
                header[i] = std::toupper(static_cast<unsigned char>(header[i]));
            else
                header[i] = std::tolower(static_cast<unsigned char>(header[i]));
            isNextCharCapital = false;
        }
    }
}

// Reads from the input string until the next line or end,
// starting at startIndex and updating it in place (by ref).
// Returns true if there was data read, false otherwise.
bool readLine(const std::string& input, std::string& lineBuf, size_t& startIndex) {
    // Check if at end
    if (startIndex == input.length()) return false;

    // Otherwise, read the next line
    size_t endIndex = input.find('\n', startIndex);
    lineBuf = input.substr(startIndex, endIndex - startIndex);

    // Update startIndex
    startIndex = (endIndex == std::string::npos) ? input.length() : (endIndex + 1);
    return true;
}