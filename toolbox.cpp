#include "toolbox.hpp"

std::unordered_map<std::string, std::string> MIMES;

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

void splitString(std::vector<std::string>& splitVec, std::string& string, char delimiter, bool stripWhitespace) {
    size_t startIndex = 0;
    for (size_t i = 0; i < string.size(); i++) {
        if (string[i] == delimiter) {
            std::string substr = string.substr(startIndex, i-startIndex);
            if (stripWhitespace)
                trimString(substr);
            if (substr.size() > 0)
                splitVec.push_back(substr);
            startIndex = i+1;
        }
    }

    // append last snippet
    if (startIndex < string.size()) {
        std::string substr = string.substr(startIndex);
        if (stripWhitespace)
            trimString(substr);
        if (substr.size() > 0)
            splitVec.push_back(substr);
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
    delete pBuffer;

    return IO_SUCCESS;
}

int loadResources() {
    // Load MIMES
    const std::string path = (std::filesystem::current_path() / "conf/mimes.conf").string();
    std::ifstream mimeHandle(path);
    if (!mimeHandle.is_open()) return IO_FAILURE;

    std::string line, extBuf, mimeBuf;
    while (std::getline(mimeHandle, line)) {
        size_t spaceIndex = line.find(' ');
        extBuf = line.substr(0, spaceIndex);
        mimeBuf = line.substr(spaceIndex+1);
        trimString(extBuf);
        trimString(mimeBuf);
        MIMES.insert({extBuf, mimeBuf});
    }

    mimeHandle.close();

    return IO_SUCCESS;
}

void stringReplaceAll(std::string& haystack, const std::string& needle, const std::string& sub) {
    size_t index;
    while ((index = haystack.find(needle)) != std::string::npos) {
        haystack.erase(index, needle.size());
        haystack.insert(index, sub);
    }
}

bool doesFileExist(const std::string& path, const bool forceInDocumentRoot) {
    if (!forceInDocumentRoot)
        return std::filesystem::exists(path);
    else // Match document root at start of filename
        return std::filesystem::exists(path) && path.find(DOCUMENT_ROOT.string()) == 0;
}

int loadTextFile(const std::string& path, std::string& buffer) {
    // Open file
    std::ifstream inHandle(path);
    if (!inHandle.is_open()) return IO_FAILURE;

    // Read to buffer
    std::string line;
    while (std::getline(inHandle, line))
        buffer += line + '\n';

    // Pop extra newline
    if (line.size() && line.back() == '\n') line.pop_back();

    // Close file handle
    inHandle.close();

    // Base case, return success
    return IO_SUCCESS;
}

int loadErrorDoc(const int status, const std::string& title, std::string& buffer) {
    std::filesystem::path cwd = std::filesystem::current_path() / "conf" / "err.html";
    int ioStatus = loadTextFile(cwd.string(), buffer);

    // Replace status code & descriptor
    stringReplaceAll(buffer, "%title%", title);
    stringReplaceAll(buffer, "%status%", std::to_string(status));

    return ioStatus;
}

int loadConfHeaders(std::unordered_map<std::string, std::string>& buffer) {
    std::filesystem::path cwd = std::filesystem::current_path() / "conf" / "headers.conf";

    // Load text file
    std::ifstream inHandle(cwd.string());
    if (!inHandle.is_open()) return IO_FAILURE;

    // Read to buffer
    std::string line, headerName, value;
    while (std::getline(inHandle, line)) {
        size_t delimIndex = line.find(':');
        headerName = line.substr(0, delimIndex);
        value = line.substr(delimIndex+1);
        trimString(headerName);
        strToUpper(headerName);
        trimString(value);
        buffer.insert({headerName, value});
    }

    // Close file handle
    inHandle.close();

    // Load preset headers
    buffer.insert({"CONNECTION", "close"});
    buffer.insert({"SERVER", VERSION});

    // Base case, return success
    return IO_SUCCESS;
}