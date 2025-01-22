#include "string_tools.hpp"

void strToUpper(std::string& str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
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

int deflateText(std::string& buffer) {
    // Handle deflate compression
    const size_t sourceLen = buffer.size();
    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;

    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return IO_FAILURE;

    // Determine output size
    const size_t outputLen = deflateBound(&zs, sourceLen);
    char* pBuffer = new char[outputLen];

    zs.avail_in = (uInt)sourceLen;
    zs.avail_out = (uInt)outputLen;
    zs.next_in = (Bytef*)buffer.c_str();
    zs.next_out = (Bytef*)pBuffer;

    // Deflate
    deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);
    deflate(&zs, Z_FINISH);
    deflateEnd(&zs);

    // Copy buffer
    buffer.clear();
    for (size_t i = 0; i < zs.total_out; ++i)
        buffer.push_back(pBuffer[i]);
    delete[] pBuffer;

    return IO_SUCCESS;
}