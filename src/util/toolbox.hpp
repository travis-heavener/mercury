#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include "string_tools.hpp"
#include "../io/file_tools.hpp"
#include "../conf/conf.hpp"

void formatFileSize(size_t, std::string&);
void formatDate(const std::chrono::system_clock::duration, std::string&);

int loadErrorDoc(const int, std::string&);
int loadDirectoryListing(std::string&, const std::string&, const std::string&);

// Time helper functions
std::time_t getTimeTFromGMT(const std::string&);
std::time_t getFileModTimeT(const std::string&);
std::string getFileModGMTString(const std::string&);
std::string getCurrentGMTString();

std::string getReasonFromStatusCode(uint16_t);

#endif