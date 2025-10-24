#ifndef __CONF_HPP
#define __CONF_HPP

#include <optional>

#include "../pch/common.hpp"
#include "conf_match.hpp"
#include "conf_redirect.hpp"

#define CONF_SUCCESS 0
#define CONF_FAILURE 1

#define CONF_FILE "conf/mercury.conf"
#define MIMES_FILE "conf/mimes.conf"
#define VERSION_FILE "version.txt"

typedef unsigned short port_t;

/****** EXTERNAL FIELDS ******/

namespace conf {
    
    extern std::string VERSION;
    extern std::filesystem::path TMP_PATH;

    extern std::filesystem::path DOCUMENT_ROOT;
    extern port_t PORT;

    extern bool IS_IPV4_ENABLED;
    extern std::optional<SanitizedIP> BIND_ADDR_IPV4;
    extern bool IS_IPV6_ENABLED;
    extern std::optional<SanitizedIP> BIND_ADDR_IPV6;

    extern bool IS_KEEP_ALIVE_ENABLED;
    extern unsigned int KEEP_ALIVE_TIMEOUT;
    extern unsigned int MAX_KEEP_ALIVE_REQUESTS;
    extern unsigned int MIN_COMPRESSION_SIZE;

    extern bool ENABLE_LEGACY_HTTP;
    extern unsigned short MAX_REQUEST_BACKLOG;
    extern unsigned int REQUEST_BUFFER_SIZE, RESPONSE_BUFFER_SIZE;
    extern unsigned int MAX_REQUEST_BODY, MAX_RESPONSE_BODY;
    extern unsigned int IDLE_THREADS_PER_CHILD, MAX_THREADS_PER_CHILD;
    extern std::vector<std::unique_ptr<Match>> matchConfigs;
    extern std::vector<std::string> INDEX_FILES;
    extern std::vector<std::unique_ptr<Redirect>> redirectRules;

    extern std::filesystem::path ACCESS_LOG_FILE;
    extern std::filesystem::path ERROR_LOG_FILE;
    extern bool REDACT_LOG_IPS;

    extern bool USE_TLS;
    extern port_t TLS_PORT;

    extern bool IS_PHP_ENABLED;
    #ifdef _WIN32
        extern std::filesystem::path PHP_CGI_EXE_PATH;
    #endif

    extern bool SHOW_WELCOME_BANNER;
    extern bool CHECK_LATEST_RELEASE;

    extern std::ofstream accessLogHandle;
    extern std::ofstream errorLogHandle;
    
    extern std::unordered_map<std::string, std::string> MIMES;
    extern std::filesystem::path CWD;

    // Static methods
    int loadConfig(int argc, char* argv[]);
    void cleanupConfig();
    bool isVersionOutdated(const std::string& latestRemoteVersion);
}

#endif