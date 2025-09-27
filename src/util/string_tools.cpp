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
    size_t index;
    while ((index = str.find('%')) != std::string::npos) {
        if (index + 2 < str.size()) {
            str.replace(
                index, 3, 1,
                static_cast<unsigned char>(std::stoi(str.substr(index + 1, 2), nullptr, 16))
            );
            ++index;
        } else {
            throw std::invalid_argument("");
        }
    }
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