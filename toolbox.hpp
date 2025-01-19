#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#define IO_SUCCESS 0
#define IO_FAILURE 1

void stringReplaceAll(std::string&, const std::string&, const std::string&);
int loadTextFile(const std::string&, std::string&);
int loadErrorDoc(const int, const std::string&, std::string&);
int loadConfHeaders(std::string&);

#endif