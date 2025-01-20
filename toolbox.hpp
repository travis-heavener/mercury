#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#define IO_SUCCESS 0
#define IO_FAILURE 1

extern std::unordered_map<std::string, std::string> MIMES;

void strToUpper(std::string&);
void splitStringUnique(std::set<std::string>&, std::string&, char);
void splitString(std::vector<std::string>&, std::string&, char);
void trimString(std::string&);

int loadResources();

void stringReplaceAll(std::string&, const std::string&, const std::string&);
bool doesFileExist(const std::string&);
int loadTextFile(const std::string&, std::string&);
int loadErrorDoc(const int, const std::string&, std::string&);
int loadConfHeaders(std::string&);

#endif