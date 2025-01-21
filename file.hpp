#ifndef __FILE_HPP
#define __FILE_HPP

#include <fstream>
#include <sstream>
#include <string>

#include "toolbox.hpp"

class File {
    public:
        File(const std::string&);

        int loadToBuffer(std::string&) const;

        std::string MIME;
        std::string path;
        std::string queryStr;
};

#endif