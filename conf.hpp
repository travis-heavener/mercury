#ifndef __CONF_HPP
#define __CONF_HPP

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

#include "util/string_tools.hpp"
#include "util/conf_match.hpp"

#include "lib/pugixml.hpp"

#define CONF_SUCCESS 0
#define CONF_FAILURE 1

#define VERSION "Mercury v0.2.2"
#define CONF_FILE "conf/mercury.conf"
#define MIMES_FILE "conf/mimes.conf"

typedef unsigned short port_t;

/****** EXTERNAL FIELDS ******/

namespace conf {

    extern std::filesystem::path DOCUMENT_ROOT;
    extern std::string HOST;
    extern port_t PORT;
    extern std::vector<conf::Match*> matchConfigs;

    extern std::unordered_map<std::string, std::string> MIMES;

}

/*****************************/

int loadConfig();
void cleanupConfig();

#endif