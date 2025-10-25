#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <chrono>
#include <memory>
#include <string>

#include "../http/body_stream.hpp"

#define IO_SUCCESS 0
#define IO_FAILURE 1
#define IO_ABORTED 2

void formatFileSize(size_t, std::string&);
void formatDate(const std::chrono::system_clock::duration, std::string&);

int loadErrorDoc(const int, std::unique_ptr<http::IBodyStream>&);
int loadDirectoryListing(std::unique_ptr<http::IBodyStream>&, const std::string&, const std::string&);

// Time helper functions
std::time_t getTimeTFromGMT(const std::string&);
std::time_t getFileModTimeT(const std::string&);
std::string getFileModGMTString(const std::string&);
std::string getCurrentGMTString();

const char* getReasonFromStatus(uint16_t code);

#endif