#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

#include "string_tools.hpp"
#include "../conf.hpp"

bool doesFileExist(const std::string&, const bool);
bool doesDirectoryExist(const std::string&, const bool);
int loadErrorDoc(const int, std::string&);
int loadConfHeaders(std::unordered_map<std::string, std::string>&);

void formatFileSize(size_t, std::string&);
void formatDate(const std::chrono::system_clock::duration, std::string&);

int loadDirectoryListing(std::string&, const std::string&, const std::string&);

std::time_t getTimeTFromGMT(const std::string&);
std::time_t getFileModTimeT(const std::string&);
std::string getFileModGMTString(const std::string&);
std::string getCurrentGMTString();

std::string getReasonFromStatusCode(uint16_t);

// Debug profiling
long long debug_getTimestamp();

#endif