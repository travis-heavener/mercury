#include "conf.hpp"

#include <iostream>

#include <pugixml.hpp>

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include "../io/file_tools.hpp"
#include "../util/string_tools.hpp"

#define LOAD_UINT_FORBID_ZERO false

/****** EXTERNAL FIELDS ******/

namespace conf {

    std::string VERSION;
    std::filesystem::path TMP_PATH;

    std::filesystem::path DOCUMENT_ROOT;
    port_t PORT;

    bool IS_IPV4_ENABLED;
    std::optional<SanitizedIP> BIND_ADDR_IPV4;
    bool IS_IPV6_ENABLED;
    std::optional<SanitizedIP> BIND_ADDR_IPV6;

    bool IS_KEEP_ALIVE_ENABLED;
    unsigned int KEEP_ALIVE_TIMEOUT;
    unsigned int MAX_KEEP_ALIVE_REQUESTS;
    unsigned int MIN_COMPRESSION_SIZE;

    bool ENABLE_LEGACY_HTTP;
    unsigned short MAX_REQUEST_BACKLOG;
    unsigned int REQUEST_BUFFER_SIZE, RESPONSE_BUFFER_SIZE;
    unsigned int MAX_REQUEST_BODY, MAX_RESPONSE_BODY;
    unsigned int IDLE_THREADS_PER_CHILD, MAX_THREADS_PER_CHILD;
    std::vector<std::unique_ptr<Match>> matchConfigs;
    std::vector<std::string> INDEX_FILES;
    std::vector<std::unique_ptr<Redirect>> redirectRules;

    std::filesystem::path ACCESS_LOG_FILE;
    std::filesystem::path ERROR_LOG_FILE;
    bool REDACT_LOG_IPS;

    bool USE_TLS;
    port_t TLS_PORT;

    bool IS_PHP_ENABLED;
    #ifdef _WIN32
        std::filesystem::path PHP_CGI_EXE_PATH;
    #endif

    bool SHOW_WELCOME_BANNER;
    bool CHECK_LATEST_RELEASE;

    std::ofstream accessLogHandle;
    std::ofstream errorLogHandle;

    std::unordered_map<std::string, std::string> MIMES;

    // CWD in root directory of repo
    std::filesystem::path CWD, previousCWD = std::filesystem::current_path();

    /**********************************************************/
    /********************* STATIC METHODS *********************/
    /**********************************************************/

    // Forward decs
    int loadMIMES();
    int loadUint(const pugi::xml_node& root, unsigned int& var, const std::string& nodeName, const bool allowZero=true);
    int loadUint(const pugi::xml_node& root, unsigned short& var, const std::string& nodeName, const bool allowZero=true);
    int loadOnOff(const pugi::xml_node& root, bool& var, const std::string& nodeName);
    int loadBindAddress(const pugi::xml_node& root, const bool isIPv6);
    int loadBool(const pugi::xml_node& root, bool& var, const std::string& nodeName);
    int loadPath(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName, const bool isLogFile=false);
    int loadDirectoryAndCanonicalize(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName);
    int loadTempFileDirectory();

    // Loads the directory of the running executable to path
    // Returns true if successful or false otherwise
    bool loadExecutableDirectory(std::filesystem::path& path) {
        #ifdef _WIN32
            std::string buf(MAX_PATH, '\0');
            if (!GetModuleFileNameA(NULL, &buf[0], MAX_PATH)) return false;
            path = std::filesystem::path(buf).parent_path();
        #else
            // Get the directory of the running executable
            try {
                path = std::filesystem::canonical("/proc/self/exe").parent_path();
            } catch (std::filesystem::filesystem_error&) {
                return false;
            }
        #endif
        return true;
    }

    int loadConfig(int argc, char* argv[]) {
        // Set CWD to Mercury project root
        if (!loadExecutableDirectory(CWD)) {
            std::cerr << "Failed to set current working directory." << std::endl;
            return CONF_FAILURE;
        }

        std::filesystem::current_path( CWD = CWD.parent_path() );

        // Read XML config file
        pugi::xml_document doc;
        pugi::xml_parse_result result;
        try {
            result = doc.load_file(argc > 1 ? argv[1] : CONF_FILE);
            if (!result) throw 0;
        } catch (...) {
            std::cerr << "Failed to open config file." << std::endl;
            return CONF_FAILURE;
        }

        // Load tmp file directory
        if (loadTempFileDirectory() == CONF_FAILURE)
            return CONF_FAILURE;

        // Load version
        std::ifstream versionHandle( VERSION_FILE );
        std::getline(versionHandle, VERSION);
        if (VERSION.back() == '\n') VERSION.pop_back();
        if (VERSION.back() == '\r') VERSION.pop_back();
        versionHandle.close();

        // Extract root node
        pugi::xml_node root = doc.child("Mercury");
        if (!root) {
            std::cerr << "Failed to parse config file, missing root \"Mercury\" node." << std::endl;
            return CONF_FAILURE;
        }

        /************************** Extract DocumentRoot **************************/

        if (loadDirectoryAndCanonicalize(root, DOCUMENT_ROOT, "DocumentRoot") == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** LOAD IPv4/IPv6 TOGGLES **************************/

        if (loadBindAddress(root, false) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadBindAddress(root, true) == CONF_FAILURE)
            return CONF_FAILURE;

        if (!IS_IPV4_ENABLED && !IS_IPV6_ENABLED) {
            std::cerr << "Either IPv4 or IPv6 must be enabled in the config file!" << std::endl;
            return CONF_FAILURE;
        }

        /************ LOAD BOOLEANS ************/

        if (loadOnOff(root, ENABLE_LEGACY_HTTP, "EnableLegacyHTTPVersions") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadOnOff(root, IS_PHP_ENABLED, "EnablePHPCGI") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadOnOff(root, IS_KEEP_ALIVE_ENABLED, "KeepAlive") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadBool(root, REDACT_LOG_IPS, "RedactLogIPs") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadBool(root, SHOW_WELCOME_BANNER, "ShowWelcomeBanner") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadBool(root, CHECK_LATEST_RELEASE, "StartupCheckLatestRelease") == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** Load IndexFiles **************************/

        pugi::xml_node indexFileNode = root.child("IndexFiles");
        if (!indexFileNode) {
            std::cerr << "Failed to parse config file, missing IndexFiles node." << std::endl;
            return CONF_FAILURE;
        }

        // Extract index files
        std::string indexFilesRaw( indexFileNode.text().as_string() );
        trimString(indexFilesRaw);
        splitString(INDEX_FILES, indexFilesRaw, ',', true);

        // Validate all index files
        for (const std::string& str : INDEX_FILES) {
            if (str.size() == 0 || str.find('/') != std::string::npos || str.find('\\') != std::string::npos) {
                std::cerr << "Failed to parse config file, invalid IndexFiles value." << std::endl;
                return CONF_FAILURE;
            }
        }

        /************ LOAD UINTS/USHORTS ************/

        if (loadUint(root, PORT, "Port", LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_REQUEST_BACKLOG, "MaxRequestBacklog", LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, REQUEST_BUFFER_SIZE, "RequestBufferSize", LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, RESPONSE_BUFFER_SIZE, "ResponseBufferSize", LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_REQUEST_BODY, "MaxRequestBody") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_RESPONSE_BODY, "MaxResponseBody") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, IDLE_THREADS_PER_CHILD, "IdleThreadsPerChild", LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_THREADS_PER_CHILD, "MaxThreadsPerChild", LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (MAX_THREADS_PER_CHILD <= IDLE_THREADS_PER_CHILD) {
            std::cerr << "Failed to parse config file, MaxThreadsPerChild must be greater than IdleThreadsPerChild.";
            return CONF_FAILURE;
        }

        if (loadUint(root, KEEP_ALIVE_TIMEOUT, "KeepAliveMaxTimeout", LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_KEEP_ALIVE_REQUESTS, "KeepAliveMaxRequests", LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MIN_COMPRESSION_SIZE, "MinResponseCompressionSize") == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** LOAD PATHS **************************/

        if (loadPath(root, ACCESS_LOG_FILE, "AccessLogFile", true) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadPath(root, ERROR_LOG_FILE, "ErrorLogFile", true) == CONF_FAILURE)
            return CONF_FAILURE;

        #ifdef _WIN32
            if (IS_PHP_ENABLED && loadPath(root, PHP_CGI_EXE_PATH, "WinPHPCGIPath") == CONF_FAILURE)
                return CONF_FAILURE;
        #endif

        /************ LOAD MATCHES, REDIRECTS, & MIMES ************/

        pugi::xml_object_range matchNodes = root.children("Match");
        for (pugi::xml_node& match : matchNodes) {
            // Parse match
            std::unique_ptr<Match> pMatch = loadMatch(match);
            if (pMatch == nullptr) return CONF_FAILURE;
            matchConfigs.push_back( std::move(pMatch) );
        }

        pugi::xml_object_range redirectRuleNodes = root.children("Redirect");
        for (pugi::xml_node& redirectRule : redirectRuleNodes) {
            // Parse match
            std::unique_ptr<Redirect> pRedirect = loadRedirect(redirectRule);
            if (pRedirect == nullptr) return CONF_FAILURE;
            redirectRules.push_back( std::move(pRedirect) );
        }

        if (loadMIMES() == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** LOAD TLS **************************/

        pugi::xml_node tlsPortNode = root.child("TLSPort");
        if (!tlsPortNode) {
            std::cerr << "Failed to parse config file, missing TLSPort node." << std::endl;
            return CONF_FAILURE;
        }

        std::string tlsPortRaw = tlsPortNode.text().as_string();
        trimString(tlsPortRaw);

        // If TLS is enabled, grab the port
        USE_TLS = tlsPortRaw != "off";
        try {
            // Prevent negatives
            if (tlsPortRaw.size() == 0) throw std::invalid_argument("");
            if (USE_TLS && tlsPortRaw[0] == '-') throw std::invalid_argument("");
            TLS_PORT = USE_TLS ? std::stoul(tlsPortRaw) : 0;
        } catch (std::invalid_argument&) {
            std::cerr << "Failed to parse config file, invalid value for TLSPort." << std::endl;
            return CONF_FAILURE;
        }

        /************************** Open log files **************************/

        accessLogHandle = std::ofstream(ACCESS_LOG_FILE, std::ios_base::app);
        if (!accessLogHandle.is_open()) {
            std::cerr << "Failed to open Access Log File." << std::endl;
            return CONF_FAILURE;
        }

        errorLogHandle = std::ofstream(ERROR_LOG_FILE, std::ios_base::app);
        if (!errorLogHandle.is_open()) {
            std::cerr << "Failed to open Error Log File." << std::endl;
            return CONF_FAILURE;
        }

        // Return success
        return CONF_SUCCESS;
    }

    int loadMIMES() {
        // Load MIMES
        std::ifstream mimeHandle(MIMES_FILE);
        if (!mimeHandle.is_open()) return CONF_FAILURE;

        std::string line, extBuf, mimeBuf;
        while (std::getline(mimeHandle, line)) {
            size_t spaceIndex = line.find(' ');
            if (spaceIndex == std::string::npos) continue;

            extBuf = line.substr(0, spaceIndex);
            mimeBuf = line.substr(spaceIndex+1);
            trimString(extBuf);
            trimString(mimeBuf);
            MIMES.insert({extBuf, mimeBuf});
        }

        mimeHandle.close();

        return CONF_SUCCESS;
    }

    void cleanupConfig() {
        // Clear match configs
        matchConfigs.clear();

        // Remove any stray temp files
        while (!currentTempFiles.empty())
            removeTempFile(*currentTempFiles.begin());

        // Reset CWD
        std::filesystem::current_path( previousCWD );
    }

    void getMajorMinorPatchFromVersion(std::string ver, int& major, int& minor, int& patch) {
        ver = ver.substr(9); // Remove "Mercury v"

        const size_t firstDot = ver.find('.');
        const size_t secondDot = ver.find('.', firstDot+1);

        major = std::stoi( ver.substr(0, firstDot) );
        minor = std::stoi( ver.substr(firstDot+1, secondDot - firstDot) );
        patch = std::stoi( ver.substr(secondDot+1) );
    }

    // Compares the current version to the latest on the remote
    bool isVersionOutdated(const std::string& latestRemoteVersion) {
        int remoteMajor, remoteMinor, remotePatch;
        getMajorMinorPatchFromVersion(latestRemoteVersion, remoteMajor, remoteMinor, remotePatch);

        int thisMajor, thisMinor, thisPatch;
        getMajorMinorPatchFromVersion(conf::VERSION, thisMajor, thisMinor, thisPatch);

        // Compare versions
        return thisMajor < remoteMajor ||
            (thisMajor == remoteMajor  && thisMinor < remoteMinor) ||
            (thisMajor == remoteMajor  && thisMinor == remoteMinor && thisPatch < remotePatch);
    }

    /************************* Config loader helpers *************************/

    int loadUint(const pugi::xml_node& root, unsigned int& var, const std::string& nodeName, const bool allowZero) {
        const pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            std::cerr << "Failed to parse config file, missing " << nodeName << " node." << std::endl;
            return CONF_FAILURE;
        }

        // Extract and stringify
        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        try {
            // Prevent negatives
            if (valueStr.size() == 0) throw std::invalid_argument("");
            if (valueStr[0] == '-') throw std::invalid_argument("");

            var = std::stoull(valueStr);
            if (!allowZero && var == 0) throw std::invalid_argument("");
        } catch (std::invalid_argument&) {
            std::cerr << "Failed to parse config file, invalid unsigned integer value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        return CONF_SUCCESS;
    }

    int loadUint(const pugi::xml_node& root, unsigned short& var, const std::string& nodeName, const bool allowZero) {
        const pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            std::cerr << "Failed to parse config file, missing " << nodeName << " node." << std::endl;
            return CONF_FAILURE;
        }

        // Extract and stringify
        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        try {
            // Prevent negatives
            if (valueStr.size() == 0) throw std::invalid_argument("");
            if (valueStr[0] == '-') throw std::invalid_argument("");

            var = std::stoul(valueStr);
            if (!allowZero && var == 0) throw std::invalid_argument("");
        } catch (std::invalid_argument&) {
            std::cerr << "Failed to parse config file, invalid unsigned integer value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        return CONF_SUCCESS;
    }

    int loadOnOff(const pugi::xml_node& root, bool& var, const std::string& nodeName) {
        pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            std::cerr << "Failed to parse config file, missing " << nodeName << " node." << std::endl;
            return CONF_FAILURE;
        }

        // Extract and stringify
        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        // Verify valid value provided
        if (valueStr != "on" && valueStr != "off") {
            std::cerr << "Failed to parse config file, invalid on/off value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        var = (valueStr == "on");
        return CONF_SUCCESS;
    }

    int loadBindAddress(const pugi::xml_node& root, const bool isIPv6) {
        const std::string nodeName = isIPv6 ? "BindAddressIPv6" : "BindAddressIPv4";
        pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            std::cerr << "Failed to parse config file, missing " << nodeName << " node." << std::endl;
            return CONF_FAILURE;
        }

        std::string rawValue = node.text().as_string();
        trimString(rawValue);

        // If the IP protocol is enabled, grab the bind address
        const bool isEnabled = rawValue != "off";
        if (isIPv6) IS_IPV6_ENABLED = isEnabled;
        else        IS_IPV4_ENABLED = isEnabled;

        if (!isEnabled) return CONF_SUCCESS;

        // Base case, parse bind address
        try {
            // Validate the IP address (throws std::invalid_argument if invalid)
            if (isIPv6)
                BIND_ADDR_IPV6.emplace( parseSanitizedClientIP(rawValue) );
            else
                BIND_ADDR_IPV4.emplace( parseSanitizedClientIP(rawValue) );
        } catch (std::invalid_argument&) {
            std::cerr << "Failed to parse config file, invalid value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        // Base case
        return CONF_SUCCESS;
    }

    int loadBool(const pugi::xml_node& root, bool& var, const std::string& nodeName) {
        pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            std::cerr << "Failed to parse config file, missing " << nodeName << " node." << std::endl;
            return CONF_FAILURE;
        }

        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        // Verify valid value provided
        if (valueStr != "true" && valueStr != "false") {
            std::cerr << "Failed to parse config file, invalid true/false value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        var = (valueStr == "true");
        return CONF_SUCCESS;
    }

    int loadPath(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName, const bool isLogFile) {
        pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            std::cerr << "Failed to parse config file, missing " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        std::string value( node.text().as_string() );
        trimString(value);

        if (value.size() == 0) {
            std::cerr << "Failed to parse config file, invalid path provided for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        // Format string
        if (value == ".") { // Use relative path
            var = CWD;
            if (isLogFile) return CONF_FAILURE; // Cannot be a directory
        } else if (value.find("./") == 0) { // Use relative path
            var = CWD / value.substr(2);
            if (isLogFile) createLogDirectoryIfMissing(var);
        } else { // Try as absolute path
            var = std::filesystem::path(value);
            if (isLogFile) createLogDirectoryIfMissing(var);

            if (!std::filesystem::exists(var)) {
                // Try as relative path (ex. "path" instead of "./path")
                var = CWD / value;
                if (isLogFile) createLogDirectoryIfMissing(var);
            }
        }

        // Skip exists check for log files (handled separately)
        if (!isLogFile && !std::filesystem::exists(var)) {
            std::cerr << "Failed to parse config file, path provided for " << nodeName << " could not be found." << std::endl;
            return CONF_FAILURE;
        }

        return CONF_SUCCESS;
    }

    int loadDirectoryAndCanonicalize(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName) {
        if (loadPath(root, var, nodeName) == CONF_FAILURE) return CONF_FAILURE;

        // Canonicalize document root
        try {
            var = resolveCanonicalPath(var);

            #ifdef _WIN32
                if (!var.string().empty() && isUNCPath(var) && var.string().back() == '\\') {
                    std::string buf = var.string();
                    buf.pop_back();
                    buf += '/';
                    var = buf;
                }
            #endif

            // Affix trailing slash
            if (!var.string().empty() && var.string().back() != '/')
                var = var.string() + '/';

            // Validate path exists and is a directory (passthru to catch block if failed)
            if (var.string().empty() || !std::filesystem::exists(var) || !std::filesystem::is_directory(var))
                throw std::runtime_error("");
        } catch (...) {
            /**
             * Handle internal std::filesystem::canonical failure or
             * canonical failure from IO error (std::runtime_error)
             */

            std::cerr << "Failed to parse config file, " << nodeName << " points to invalid/nonexistant directory." << std::endl;
            return CONF_FAILURE;
        }

        return CONF_SUCCESS;
    }

    int loadTempFileDirectory() {
        // Specify temp file directory
        const std::string tmpPathStr( (CWD / "tmp").string() );
        try {
            if (!std::filesystem::exists(tmpPathStr)) {
                std::filesystem::create_directory(tmpPathStr);
            } else if (!std::filesystem::is_directory(tmpPathStr)) {
                std::cerr << "Failed to open \"tmp\" directory: \"tmp\" already exists as a file in the Mercury root directory." << std::endl;
                return CONF_FAILURE;
            }
        } catch (std::filesystem::filesystem_error&) {
            std::cerr << "Failed to create \"tmp\" directory in the Mercury root directory." << std::endl;
            return CONF_FAILURE;
        }

        // Set the temp path
        TMP_PATH = tmpPathStr;

        return CONF_SUCCESS;
    }
}

#undef LOAD_UINT_FORBID_ZERO