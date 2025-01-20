#include "file.hpp"

int File::loadToBuffer(std::string& buffer) const {
    // Open file
    std::ifstream handle( path, std::ios::binary );
    if (!handle.is_open()) return IO_FAILURE;

    // Read file to buffer
    std::stringstream sstream;
    sstream << handle.rdbuf();
    buffer = sstream.str();

    // Close file
    handle.close();

    return IO_SUCCESS;
}

File lookupFile(const std::string& path) {
    // Lookup MIME type
    std::string ext = std::filesystem::path(path).extension().string();

    if (ext.size()) // Remove leading period
        ext = ext.substr(1);

    return File(
        MIMES.find(ext) != MIMES.end() ? MIMES[ext] : "",
        path
    );
}