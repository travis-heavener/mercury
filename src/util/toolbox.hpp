#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <chrono>
#include <iomanip>

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include "../pch/common.hpp"
#include "string_tools.hpp"
#include "../conf/conf.hpp"
#include "../http/body_stream.hpp"

void formatFileSize(size_t, std::string&);
void formatDate(const std::chrono::system_clock::duration, std::string&);

int loadErrorDoc(const int, std::unique_ptr<http::IBodyStream>&);
int loadDirectoryListing(std::unique_ptr<http::IBodyStream>&, const std::string&, const std::string&);

// Time helper functions
std::time_t getTimeTFromGMT(const std::string&);
std::time_t getFileModTimeT(const std::string&);
std::string getFileModGMTString(const std::string&);
std::string getCurrentGMTString();

std::string getReasonFromStatusCode(uint16_t);

#endif