#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <zlib.h>

#include "conf.hpp"

#define IO_SUCCESS 0
#define IO_FAILURE 1
#define IO_ABORTED 2

void strToUpper(std::string&);
void splitStringUnique(std::unordered_set<std::string>&, std::string&, char, bool);
void stringReplaceAll(std::string&, const std::string&, const std::string&);

int deflateText(std::string&);

bool doesFileExist(const std::string&, const bool);
int loadErrorDoc(const int, const std::string&, std::string&);
int loadConfHeaders(std::unordered_map<std::string, std::string>&);

#endif