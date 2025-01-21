#ifndef __CONF_HPP
#define __CONF_HPP

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include "lib/pugixml.hpp"

#define CONF_SUCCESS 0
#define CONF_FAILURE 1

#define VERSION "Mercury v0.1.5"
#define CONF_FILE "conf/mercury.conf"
#define MIMES_FILE "conf/mimes.conf"

typedef unsigned short port_t;

/****** EXTERNAL FIELDS ******/

extern std::filesystem::path DOCUMENT_ROOT;
extern port_t PORT;

extern std::unordered_map<std::string, std::string> MIMES;

/*****************************/

int loadConfig();
void trimString(std::string&);

#endif