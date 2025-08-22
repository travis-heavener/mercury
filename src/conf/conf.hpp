#ifndef __CONF_HPP
#define __CONF_HPP

#include <iostream>

#include "../pch/common.hpp"
#include "../io/file_tools.hpp"
#include "../util/string_tools.hpp"
#include "conf_match.hpp"

#include "../../lib/pugixml.hpp"

#define CONF_SUCCESS 0
#define CONF_FAILURE 1

#define CONF_FILE "../conf/mercury.conf"
#define MIMES_FILE "../conf/mimes.conf"
#define VERSION_FILE "../version.txt"

typedef unsigned short port_t;

/****** EXTERNAL FIELDS ******/

namespace conf {

    extern std::string VERSION;

    extern std::filesystem::path DOCUMENT_ROOT;
    extern port_t PORT;
    extern bool IS_IPV4_ENABLED;
    extern bool IS_IPV6_ENABLED;
    extern unsigned short MAX_REQUEST_BACKLOG;
    extern unsigned int MAX_REQUEST_BUFFER;
    extern unsigned int THREADS_PER_CHILD;
    extern std::vector<conf::Match*> matchConfigs;
    extern std::string INDEX_FILE;

    extern std::filesystem::path ACCESS_LOG_FILE;
    extern std::filesystem::path ERROR_LOG_FILE;

    extern bool USE_TLS;
    extern port_t TLS_PORT;

    extern bool SHOW_WELCOME_BANNER;
    extern bool CHECK_LATEST_RELEASE;

    extern std::ofstream accessLogHandle;
    extern std::ofstream errorLogHandle;
    
    extern std::unordered_map<std::string, std::string> MIMES;
    extern std::filesystem::path CWD;

}

/*****************************/

int loadConfig();
void cleanupConfig();

#endif