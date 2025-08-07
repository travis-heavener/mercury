#include "string_tools.hpp"

void strToUpper(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
}

void strToLower(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
}

void splitStringUnique(std::unordered_set<std::string>& splitVec, std::string& string, char delimiter, bool stripWhitespace) {
    size_t startIndex = 0;
    for (size_t i = 0; i < string.size(); i++) {
        if (string[i] == delimiter) {
            std::string substr = string.substr(startIndex, i-startIndex);
            if (stripWhitespace)
                trimString(substr);
            if (substr.size() > 0)
                splitVec.insert(substr);
            startIndex = i+1;
        }
    }

    // append last snippet
    if (startIndex < string.size()) {
        std::string substr = string.substr(startIndex);
        if (stripWhitespace)
            trimString(substr);
        if (substr.size() > 0)
            splitVec.insert(substr);
    }
}

void stringReplaceAll(std::string& haystack, const std::string& needle, const std::string& sub) {
    size_t index;
    while ((index = haystack.find(needle)) != std::string::npos) {
        haystack.erase(index, needle.size());
        haystack.insert(index, sub);
    }
}

void trimString(std::string& str) {
    while (str.size() && std::isspace(str.back()))
        str.pop_back();

    while (str.size() && std::isspace(str[0]))
        str = str.substr(1);
}

void decodeURI(std::string& str) {
    size_t index;
    while ((index = str.find('%')) != std::string::npos) {
        unsigned short ascii = std::stoul(str.substr(index+1, 2), nullptr, 16);
        str.erase(index, 2);
        str[index] = ascii;
    }
}

void formatHeaderCasing(std::string& header) {
    bool isNextCharCapital = true;
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == '-') {
            isNextCharCapital = true;
        } else {
            header[i] = isNextCharCapital ? ::toupper(header[i]) : ::tolower(header[i]);
            isNextCharCapital = false;
        }
    }
}

int compressText(std::string& buffer, const int method) {
    if (method == COMPRESS_BROTLI) {
        buffer = brotli::compress(buffer);
    } else {
        // Handle deflate compression
        const size_t sourceLen = buffer.size();
        z_stream zs;
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;

        const int windowBits = method == COMPRESS_GZIP ? (15 | 16) : 15; 
        if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return IO_FAILURE;

        // Determine output size
        const size_t outputLen = deflateBound(&zs, sourceLen);
        char* pBuffer = new char[outputLen];

        zs.avail_in = (uInt)sourceLen;
        zs.avail_out = (uInt)outputLen;
        zs.next_in = (Bytef*)buffer.c_str();
        zs.next_out = (Bytef*)pBuffer;

        // Deflate
        deflate(&zs, Z_FINISH);
        deflateEnd(&zs);

        // Copy buffer
        buffer.clear();
        for (size_t i = 0; i < zs.total_out; ++i)
            buffer.push_back(pBuffer[i]);
        delete[] pBuffer;
    }
    return IO_SUCCESS;
}