#ifndef __FILE_HPP
#define __FILE_HPP

#include <fstream>
#include <sstream>
#include <string>

#include "toolbox.hpp"

class File {
    public:
        File(const std::string&);

        int loadToBuffer(std::string&);
        bool exists = false;
        bool isDirectory = false;

        std::string MIME;
        std::string path;
        std::string rawPath;
        std::string queryStr;
};

#endif