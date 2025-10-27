#include "file.hpp"

#include "../conf/conf.hpp"
#include "../io/file_tools.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

File::File(const std::string& _rawPath) {
    // URI decode and remove query string
    const size_t queryIndex = _rawPath.find('?');
    if (queryIndex == std::string::npos) {
        this->rawPath = _rawPath; // Raw path from request, but URI decoded
    } else {
        this->rawPath = _rawPath.substr(0, queryIndex); // Raw path from request, but URI decoded
        this->queryStr = _rawPath.substr(queryIndex);
    }

    // Doesn't need try-catch, already checked in Request constructor
    decodeURI(this->rawPath);
    decodeURI(this->queryStr);

    // Extract PHP path info
    std::filesystem::path rawPathNoPathInfo;
    std::filesystem::path rawPathCopy = std::filesystem::path(rawPath);
    bool hasPathInfo = false;
    for (const auto& part : rawPathCopy) {
        if (!hasPathInfo) {
            rawPathNoPathInfo /= part;
            if (part.string().ends_with(".php") &&
                std::filesystem::is_regular_file(conf::DOCUMENT_ROOT / rawPathNoPathInfo.string().substr(1)))
                hasPathInfo = true;
        } else {
            // Add path info parts
            this->phpPathInfo += '/' + part.string();
        }
    }

    // Remove path info from rawPath
    if (hasPathInfo)
        this->rawPath = rawPathNoPathInfo.string();

    // Update the actual path
    if (this->rawPath == "/") {
        this->path = conf::DOCUMENT_ROOT.string();
        normalizeBackslashes( path ); // Replace any backslashes w/ fwd slashes
        stringReplaceAll(path, "//", "/"); // Replace all '//' with '/'
    } else {
        // Replace all '//' with '/'
        normalizeBackslashes(rawPath);
        stringReplaceAll(rawPath, "//", "/");

        try {
            this->path = resolveCanonicalPath( conf::DOCUMENT_ROOT / this->rawPath.substr(1) ).string();
        } catch (std::filesystem::filesystem_error&) {
            this->exists = false;
            return;
        } catch (std::runtime_error&) {
            this->ioFailure = true;
            return;
        }

        // Replace any backslashes w/ fwd slashes
        #ifdef _WIN32
            // Do this again because on Windows, resolveCanonicalPath returns \ directory separators
            normalizeBackslashes(path);
        #endif

        // Re-append slash if directory
        if (doesDirectoryExist(this->path, true)) this->path += '/';
    }

    // Check for index file
    if (this->path.back() == '/') {
        for (const std::string& indexFile : conf::INDEX_FILES) {
            if (std::filesystem::exists(path + indexFile) &&
                !std::filesystem::is_directory(path + indexFile)) {
                this->path += indexFile;
                break;
            }
        }
    }

    // Handle the MIME type if this is a directory
    if (doesDirectoryExist(this->path, true)) {
        // Prepare directory index listing
        this->MIME = "text/html; charset=UTF-8";
        this->exists = true;
        this->isDirectory = true;
    } else {
        // Lookup MIME type
        std::string ext = std::filesystem::path(this->path).extension().string();
        if (ext.size()) ext = ext.substr(1); // Remove leading period
        this->MIME = conf::MIMES.find(ext) != conf::MIMES.end() ? conf::MIMES[ext] : MIME_UNSET;
        this->exists = doesFileExist(this->path, true);
    }

    // Reject symlinks or hardlinks
    const int linkStatus = isSymlinked( this->path );
    if (linkStatus == INTERNAL_ERROR) {
        this->ioFailure = true;
        return;
    } else if (linkStatus == IS_SYMLINK) {
        this->isLinked = true;
        return;
    } else if (linkStatus == FILE_NOT_EXIST) {
        this->exists = false;
        return;
    }
}

int File::loadToBuffer(std::unique_ptr<http::IBodyStream>& pStream) {
    // Handle directory listings
    if (this->isDirectory) {
        // Load directory listing document
        if (loadDirectoryListing(pStream, path, rawPath) == IO_FAILURE)
            return IO_FAILURE;

        // Update MIME
        this->MIME = "text/html; charset=UTF-8";
        return IO_SUCCESS;
    }

    // Base case, load to FileStream
    pStream = std::unique_ptr<http::IBodyStream>( new http::FileStream(path) );
    return pStream->status() == STREAM_SUCCESS ? IO_SUCCESS : IO_FAILURE;
}

std::string File::getLastModifiedGMT() const {
    return getFileModGMTString(this->path);
}