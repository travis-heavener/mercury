#ifndef __FILE_HPP
#define __FILE_HPP

#include "../pch/common.hpp"
#include "../http/http_tools.hpp"
#include "../http/body_stream.hpp"

#define MIME_UNSET ""

class File {
    public:
        File(const http::RequestPath& paths);

        int loadToBuffer(std::unique_ptr<http::IBodyStream>&);
        std::string getLastModifiedGMT() const;
        bool exists = false;
        bool isLinked = false; // True if symlink or hardlink
        bool isDirectory = false;
        bool ioFailure = false; // True if an IO failure occured
        
        std::string MIME;

        // The FULL, absolute path to the file
        std::string absoluteResourcePath;

        // The decoded URI from the request, W/O any path info
        std::string decodedURIWithoutPathInfo;

        // The query string
        std::string queryString;

        // The PHP path info string, for PHP files only
        std::string phpPathInfo;
};

#endif