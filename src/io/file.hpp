#ifndef __FILE_HPP
#define __FILE_HPP

#include <fstream>
#include <sstream>
#include <string>

#include "../util/toolbox.hpp"

class File {
    public:
        File(const std::string&);

        int loadToBuffer(std::string&);
        std::string getLastModifiedGMT() const;
        bool exists = false;
        bool isLinked = false; // True if symlink or hardlink
        bool isDirectory = false;
        bool ioFailure = false; // True if an IO failure occured

        std::string MIME;
        std::string path;
        std::string rawPath;
        std::string queryStr;
};

#endif