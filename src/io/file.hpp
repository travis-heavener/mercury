#ifndef __FILE_HPP
#define __FILE_HPP

#include "../pch/common.hpp"
#include "../http/body_stream.hpp"

#define MIME_UNSET ""

class File {
    public:
        File(const std::string&);

        int loadToBuffer(std::unique_ptr<http::IBodyStream>&);
        std::string getLastModifiedGMT() const;
        bool exists = false;
        bool isLinked = false; // True if symlink or hardlink
        bool isDirectory = false;
        bool ioFailure = false; // True if an IO failure occured
        
        std::string MIME;

        // The FULL, absolute path to the file
        std::string path;

        // The RAW path from the HTTP request
        std::string rawPath;

        // The query string
        std::string queryStr;

        // The PHP path info string, for PHP files only
        std::string phpPathInfo;
};

#endif