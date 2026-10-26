#include "file.hpp"

#include "../conf/conf.hpp"
#include "../io/file_tools.hpp"
#include "../util/string_tools.hpp"
#include "../util/toolbox.hpp"

File::File(const http::RequestPath& paths) {
    this->decodedURIWithoutPathInfo = paths.decodedURI;
    this->queryString = paths.rawQueryString.empty() ? "" : paths.rawQueryString.substr(1);

    FileMetadata meta;
    bool metaQueried = false;

    // Extract PHP path info only when PHP or FastCGI enabled and path contains .php
    if ((conf::IS_PHP_ENABLED || conf::IS_FASTCGI_ENABLED) && decodedURIWithoutPathInfo.find(".php") != std::string::npos) {
        std::filesystem::path rawPathNoPathInfo;
        std::filesystem::path rawPathCopy = std::filesystem::path(decodedURIWithoutPathInfo);
        bool hasPathInfo = false;
        for (const auto& part : rawPathCopy) {
            if (!hasPathInfo) {
                rawPathNoPathInfo /= part;
                if (part.string().ends_with(".php")) {
                    FileMetadata phpMeta;
                    const std::string candidatePath = (conf::DOCUMENT_ROOT / rawPathNoPathInfo.relative_path()).string();
                    if (queryFileMetadata(candidatePath, phpMeta) && phpMeta.isRegularFile) {
                        hasPathInfo = true;
                        meta = phpMeta;
                        metaQueried = true;
                    }
                }
            } else {
                // Add path info parts
                this->phpPathInfo += '/' + part.string();
            }
        }

        // Remove path info from decodedURI
        if (hasPathInfo) {
            this->decodedURIWithoutPathInfo = rawPathNoPathInfo.generic_string();
        } else {
            metaQueried = false;
        }
    }

    // Update the actual path
    if (this->decodedURIWithoutPathInfo == "/") {
        this->absoluteResourcePath = conf::DOCUMENT_ROOT.string();
        normalizeBackslashes(absoluteResourcePath); // Replace any backslashes w/ fwd slashes
        stringReplaceAll(absoluteResourcePath, "//", "/"); // Replace all '//' with '/'
        if (!metaQueried && !queryFileMetadata(this->absoluteResourcePath, meta)) {
            this->exists = false;
            return;
        }
        metaQueried = true;
        if (meta.isDirectory && this->absoluteResourcePath.back() != '/')
            this->absoluteResourcePath += '/';
    } else {
        // Replace all '//' with '/'
        normalizeBackslashes(decodedURIWithoutPathInfo);
        stringReplaceAll(decodedURIWithoutPathInfo, "//", "/");

        try {
            std::filesystem::path resolved;
            if (!tryResolveCanonicalPath(
                    conf::DOCUMENT_ROOT / std::filesystem::path(this->decodedURIWithoutPathInfo).relative_path(), resolved)) {
                this->exists = false;
                return;
            }
            this->absoluteResourcePath = resolved.string();
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

        if (!metaQueried && !queryFileMetadata(this->absoluteResourcePath, meta)) {
            this->exists = false;
            return;
        }
        metaQueried = true;

        // Re-append slash if directory
        if (meta.isDirectory && this->absoluteResourcePath.back() != '/')
            this->absoluteResourcePath += '/';
    }

    // Verify resource resides within document root
    std::string docRootStr = conf::DOCUMENT_ROOT.string();
    normalizeBackslashes(docRootStr);
    std::string normResourcePath = this->absoluteResourcePath;
    normalizeBackslashes(normResourcePath);
    if (normResourcePath.find(docRootStr) != 0) {
        this->exists = false;
        return;
    }

    // Check for index file
    if (this->absoluteResourcePath.back() == '/') {
        for (const std::string& indexFile : conf::INDEX_FILES) {
            const std::string candidatePath = this->absoluteResourcePath + indexFile;
            FileMetadata indexMeta;
            if (queryFileMetadata(candidatePath, indexMeta) && indexMeta.isRegularFile) {
                this->absoluteResourcePath = candidatePath;
                meta = indexMeta; // Reuse queried metadata
                break;
            }
        }
    }

    // Handle directory index or static file
    if (meta.isDirectory) {
        // Prepare directory index listing
        this->MIME = "text/html; charset=UTF-8";
        this->exists = true;
        this->isDirectory = true;
    } else {
        // Lookup MIME type
        std::string ext = std::filesystem::path(this->absoluteResourcePath).extension().string();
        if (ext.size()) ext = ext.substr(1); // Remove leading period
        this->MIME = conf::MIMES.find(ext) != conf::MIMES.end() ? conf::MIMES[ext] : MIME_UNSET;
        this->exists = meta.isRegularFile;
    }

    this->hasModTime = true;
    this->lastModTime = meta.lastModTime;

    // Reject symlinks or hardlinks
    const int linkStatus = isSymlinked( this->absoluteResourcePath, true );
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
    if (!lastModGMT.empty()) return lastModGMT;
    if (!hasModTime) return lastModGMT = getFileModGMTString(this->absoluteResourcePath);

    // Format GMT string
    std::tm gmtTime{};
    #ifdef _WIN32
        if (gmtime_s(&gmtTime, &lastModTime) != 0)
            return lastModGMT = getFileModGMTString(this->absoluteResourcePath);
    #else
        if (gmtime_r(&lastModTime, &gmtTime) == nullptr)
            return lastModGMT = getFileModGMTString(this->absoluteResourcePath);
    #endif

    char gmtBuf[32];
    const size_t n = std::strftime(gmtBuf, sizeof(gmtBuf), "%a, %d %b %Y %H:%M:%S GMT", &gmtTime);
    return lastModGMT = std::string(gmtBuf, n);
}
