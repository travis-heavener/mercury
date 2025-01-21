#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <zlib.h>

#include "conf.hpp"

#define IO_SUCCESS 0
#define IO_FAILURE 1
#define IO_ABORTED 2

extern std::unordered_map<std::string, std::string> MIMES;

void strToUpper(std::string&);
void splitStringUnique(std::unordered_set<std::string>&, std::string&, char, bool);
void splitString(std::vector<std::string>&, std::string&, char, bool);
void trimString(std::string&);

int deflateText(const char*, int, char*, int);

int loadResources();

void stringReplaceAll(std::string&, const std::string&, const std::string&);
bool doesFileExist(const std::string&);
int loadTextFile(const std::string&, std::string&);
int loadErrorDoc(const int, const std::string&, std::string&);
int loadConfHeaders(std::unordered_map<std::string, std::string>&);

#endif