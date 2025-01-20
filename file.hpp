#ifndef __FILE_HPP
#define __FILE_HPP

#include <fstream>
#include <sstream>
#include <string>

#include "toolbox.hpp"

class File {
    public:
        File(const std::string& MIME, const std::string& path) : MIME(MIME), path(path) {};

        int loadToBuffer(std::string&) const;

        const std::string MIME;
        const std::string path;
};

File lookupFile(const std::string&);

#endif