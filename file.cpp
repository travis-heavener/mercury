#include "file.hpp"

File::File(const std::string& path) {
    // Handle query string
    size_t queryIndex = path.find('?');
    if (queryIndex == std::string::npos) {
        this->path = path;
    } else {
        this->path = path.substr(0, queryIndex);
        this->queryStr = path.substr(queryIndex);
    }

    // Format index files
    if (this->path.back() == '/')
        this->path += "index.html";

    // Lookup MIME type
    std::string ext = std::filesystem::path(this->path).extension().string();
    if (ext.size()) ext = ext.substr(1); // Remove leading period
    this->MIME = conf::MIMES.find(ext) != conf::MIMES.end() ? conf::MIMES[ext] : "";
}

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