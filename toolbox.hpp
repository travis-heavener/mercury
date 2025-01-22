#ifndef __TOOLBOX_HPP
#define __TOOLBOX_HPP

#include <sstream>
#include <string>
#include <unordered_map>

#include "util/string_tools.hpp"
#include "conf.hpp"

bool doesFileExist(const std::string&, const bool);
int loadErrorDoc(const int, const std::string&, std::string&);
int loadConfHeaders(std::unordered_map<std::string, std::string>&);

#endif