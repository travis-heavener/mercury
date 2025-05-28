#include "file.hpp"

File::File(const std::string& rawPath) {
    this->rawPath = rawPath;

    // Update the actual path
    size_t queryIndex = rawPath.find('?');
    if ((rawPath.size() == 1 || rawPath[1] == '?') && rawPath[0] == '/') {
        this->path = conf::DOCUMENT_ROOT.string();
    } else {
        try {
            this->path = std::filesystem::canonical( // Canonicalize
                conf::DOCUMENT_ROOT / rawPath.substr(1, queryIndex-1) // Prepend document root
            ).string();
        } catch (std::filesystem::filesystem_error&) {
            this->exists = false;
            return;
        }

        // Re-append slash if directory
        if (doesDirectoryExist(this->path, true)) this->path += '/';
    }

    // Handle query string
    if (queryIndex != std::string::npos) {
        // Remove query string from raw path
        this->rawPath = rawPath.substr(0, queryIndex);
        this->queryStr = rawPath.substr(queryIndex);
    }

    // Check for index file
    if (this->path.back() == '/' && std::filesystem::exists(this->path + conf::INDEX_FILE) &&
        !std::filesystem::is_directory(this->path + conf::INDEX_FILE))
        this->path += conf::INDEX_FILE;

    // Handle the MIME type if this is a directory
    if (doesDirectoryExist(this->path, true)) {
        this->MIME = "text/html";
        this->exists = true;
        this->isDirectory = true;
    } else {
        // Lookup MIME type
        std::string ext = std::filesystem::path(this->path).extension().string();
        if (ext.size()) ext = ext.substr(1); // Remove leading period
        this->MIME = conf::MIMES.find(ext) != conf::MIMES.end() ? conf::MIMES[ext] : "";
        this->exists = doesFileExist(this->path, true);
    }
}

int File::loadToBuffer(std::string& buffer) {
    // Handle directory listings
    if (this->isDirectory) {
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

std::string File::getLastModifiedGMT() const {
    return getFileModGMTString(this->path);
}