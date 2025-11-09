#include "file.hpp"

#include "../conf/conf.hpp"
#include "../io/file_tools.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

File::File(const http::RequestPath& paths) {
    this->decodedURIWithoutPathInfo = paths.decodedURI;
    this->queryString = paths.decodedQueryString;

    // Extract PHP path info
    std::filesystem::path rawPathNoPathInfo;
    std::filesystem::path rawPathCopy = std::filesystem::path(decodedURIWithoutPathInfo);
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

    // Remove path info from decodedURI
    if (hasPathInfo)
        this->decodedURIWithoutPathInfo = rawPathNoPathInfo.string();

    // Update the actual path
    if (this->decodedURIWithoutPathInfo == "/") {
        this->absoluteResourcePath = conf::DOCUMENT_ROOT.string();
        normalizeBackslashes(absoluteResourcePath); // Replace any backslashes w/ fwd slashes
        stringReplaceAll(absoluteResourcePath, "//", "/"); // Replace all '//' with '/'
    } else {
        // Replace all '//' with '/'
        normalizeBackslashes(decodedURIWithoutPathInfo);
        stringReplaceAll(decodedURIWithoutPathInfo, "//", "/");

        try {
            this->absoluteResourcePath = resolveCanonicalPath( conf::DOCUMENT_ROOT / this->decodedURIWithoutPathInfo.substr(1) ).string();
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
            normalizeBackslashes(absoluteResourcePath);
        #endif

        // Re-append slash if directory
        if (doesDirectoryExist(this->absoluteResourcePath, true)) this->absoluteResourcePath += '/';
    }

    // Check for index file
    if (this->absoluteResourcePath.back() == '/') {
        for (const std::string& indexFile : conf::INDEX_FILES) {
            if (std::filesystem::exists(absoluteResourcePath + indexFile) &&
                !std::filesystem::is_directory(absoluteResourcePath + indexFile)) {
                this->absoluteResourcePath += indexFile;
                break;
            }
        }
    }

    // Handle the MIME type if this is a directory
    if (doesDirectoryExist(this->absoluteResourcePath, true)) {
        // Prepare directory index listing
        this->MIME = "text/html; charset=UTF-8";
        this->exists = true;
        this->isDirectory = true;
    } else {
        // Lookup MIME type
        std::string ext = std::filesystem::path(this->absoluteResourcePath).extension().string();
        if (ext.size()) ext = ext.substr(1); // Remove leading period
        this->MIME = conf::MIMES.find(ext) != conf::MIMES.end() ? conf::MIMES[ext] : MIME_UNSET;
        this->exists = doesFileExist(this->absoluteResourcePath, true);
    }

    // Reject symlinks or hardlinks
    const int linkStatus = isSymlinked( this->absoluteResourcePath );
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
        if (loadDirectoryListing(pStream, absoluteResourcePath, decodedURIWithoutPathInfo) == IO_FAILURE)
            return IO_FAILURE;

        // Update MIME
        this->MIME = "text/html; charset=UTF-8";
        return IO_SUCCESS;
    }

    // Base case, load to FileStream
    pStream = std::unique_ptr<http::IBodyStream>( new http::FileStream(absoluteResourcePath) );
    return pStream->status() == STREAM_SUCCESS ? IO_SUCCESS : IO_FAILURE;
}

std::string File::getLastModifiedGMT() const {
    return getFileModGMTString(this->absoluteResourcePath);
}