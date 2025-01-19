#include "toolbox.hpp"

void stringReplaceAll(std::string& haystack, const std::string& needle, const std::string& sub) {
    size_t index;
    while ((index = haystack.find(needle)) != std::string::npos) {
        haystack.erase(index, needle.size());
        haystack.insert(index, sub);
    }
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

int loadConfHeaders(std::string& buffer) {
    std::filesystem::path cwd = std::filesystem::current_path() / "conf" / "headers.conf";
    return loadTextFile(cwd.string(), buffer);
}