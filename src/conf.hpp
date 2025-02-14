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

#include "../lib/pugixml.hpp"

#define CONF_SUCCESS 0
#define CONF_FAILURE 1

#define VERSION "Mercury v0.2.7"
#define CONF_FILE "../conf/mercury.conf"
#define MIMES_FILE "../conf/mimes.conf"

typedef unsigned short port_t;

/****** EXTERNAL FIELDS ******/

namespace conf {

    extern std::filesystem::path DOCUMENT_ROOT;
    extern std::string HOST;
    extern port_t PORT;
    extern unsigned short MAX_REQUEST_BACKLOG;
    extern unsigned int MAX_REQUEST_BUFFER;
    extern std::vector<conf::Match*> matchConfigs;
    extern std::string INDEX_FILE;

    extern std::filesystem::path ACCESS_LOG_FILE;
    extern std::filesystem::path ERROR_LOG_FILE;

    extern std::ofstream accessLogHandle;
    extern std::ofstream errorLogHandle;
    
    extern std::unordered_map<std::string, std::string> MIMES;
    extern std::filesystem::path CWD;

}

/*****************************/

int loadConfig();
void cleanupConfig();

#define ACCESS_LOG conf::accessLogHandle <<   __LogTimestamp()
#define ERROR_LOG conf::errorLogHandle << __LogTimestamp()

class __LogTimestamp {
    public:
        friend std::ofstream& operator<<(std::ofstream&, const __LogTimestamp&);
};

#endif