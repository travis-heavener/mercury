#include "file.hpp"

File::File(const std::string& rawPath) {
    // Format path
    const std::string path = (conf::DOCUMENT_ROOT / rawPath.substr(1)).string();
    this->rawPath = rawPath;

    // Handle query string
    size_t queryIndex = path.find('?');
    if (queryIndex == std::string::npos) {
        this->path = path;
    } else {
        this->path = path.substr(0, queryIndex);
        this->queryStr = path.substr(queryIndex);
        this->rawPath = this->rawPath.substr(0, this->rawPath.find('?'));
    }

    // Check for index file
    if (this->path.back() == '/' && std::filesystem::exists(this->path + conf::INDEX_FILE) &&
        !std::filesystem::is_directory(this->path + conf::INDEX_FILE))
        this->path += conf::INDEX_FILE;

    // Handle the MIME type if this is a directory
    if (doesDirectoryExist(this->path, true)) {
        this->MIME = "text/html";
    } else {
        // Lookup MIME type
        std::string ext = std::filesystem::path(this->path).extension().string();
        if (ext.size()) ext = ext.substr(1); // Remove leading period
        this->MIME = conf::MIMES.find(ext) != conf::MIMES.end() ? conf::MIMES[ext] : "";
    }
}

int File::loadToBuffer(std::string& buffer) {
    // Handle directory listings
    if (doesDirectoryExist(path, true)) {
        // Load directory listing document
        if (loadDirectoryListing(buffer, path, rawPath) == IO_FAILURE)
            return IO_FAILURE;

        // Update MIME
        this->MIME = "text/html";
        return IO_SUCCESS;
    }

    // Base case, load as normal file
    // Open file
    std::ifstream handle( path, std::ios::binary | std::ios::ate );
    if (!handle.is_open()) return IO_FAILURE;

    // Read file to buffer
    std::streamsize size = handle.tellg();
    handle.seekg(0, std::ios::beg);
    buffer = std::string(size, '\0');
    handle.read(buffer.data(), size);

    // Close file
    handle.close();

    return IO_SUCCESS;
}