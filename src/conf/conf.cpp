#include "conf.hpp"

#include <iostream>

#include "../io/file_tools.hpp"
#include "../util/string_tools.hpp"
#include "../../lib/pugixml.hpp"

/****** EXTERNAL FIELDS ******/

namespace conf {

    std::string VERSION;
    std::filesystem::path TMP_PATH;

    std::filesystem::path DOCUMENT_ROOT;
    port_t PORT;
    bool IS_IPV4_ENABLED;
    bool IS_IPV6_ENABLED;
    bool ENABLE_LEGACY_HTTP;
    unsigned short MAX_REQUEST_BACKLOG;
    unsigned int REQUEST_BUFFER_SIZE, RESPONSE_BUFFER_SIZE;
    unsigned int MAX_REQUEST_BODY, MAX_RESPONSE_BODY;
    unsigned int THREADS_PER_CHILD;
    std::vector<Match*> matchConfigs;
    std::string INDEX_FILE;

    std::filesystem::path ACCESS_LOG_FILE;
    std::filesystem::path ERROR_LOG_FILE;

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
    std::filesystem::path CWD = std::filesystem::current_path().parent_path();

    /**********************************************************/
    /********************* STATIC METHODS *********************/
    /**********************************************************/

    // Forward decs
    int loadMIMES();
    int loadUint(const pugi::xml_node& root, unsigned int& var, const std::string& nodeName, const bool allowZero=true);
    int loadUint(const pugi::xml_node& root, unsigned short& var, const std::string& nodeName, const bool allowZero=true);
    int loadOnOff(const pugi::xml_node& root, bool& var, const std::string& nodeName);
    int loadTrueFalse(const pugi::xml_node& root, bool& var, const std::string& nodeName);
    int loadPath(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName);
    int loadDirectoryAndCanonicalize(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName);
    int loadTempFileDirectory();

    int loadConfig() {
        // Read XML config file
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(CONF_FILE);

        if (!result) {
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

        if (loadOnOff(root, IS_IPV4_ENABLED, "EnableIPv4") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadOnOff(root, IS_IPV6_ENABLED, "EnableIPv6") == CONF_FAILURE)
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

        if (loadTrueFalse(root, SHOW_WELCOME_BANNER, "ShowWelcomeBanner") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadTrueFalse(root, CHECK_LATEST_RELEASE, "StartupCheckLatestRelease") == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** Extract IndexFile **************************/

        pugi::xml_node indexFileNode = root.child("IndexFile");
        if (!indexFileNode) {
            std::cerr << "Failed to parse config file, missing IndexFile node.\n";
            return CONF_FAILURE;
        }

        INDEX_FILE = indexFileNode.text().as_string();
        trimString(INDEX_FILE);

        if (INDEX_FILE.size() == 0 || INDEX_FILE.find('/') != std::string::npos || INDEX_FILE.find('\\') != std::string::npos) {
            std::cerr << "Failed to parse config file, invalid IndexFile value.\n";
            return CONF_FAILURE;
        }

        /************ LOAD UINTS/USHORTS ************/

        if (loadUint(root, PORT, "Port", false) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_REQUEST_BACKLOG, "MaxRequestBacklog", false) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, REQUEST_BUFFER_SIZE, "RequestBufferSize", false) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, RESPONSE_BUFFER_SIZE, "ResponseBufferSize", false) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_REQUEST_BODY, "MaxRequestBody") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_RESPONSE_BODY, "MaxResponseBody") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, THREADS_PER_CHILD, "ThreadsPerChild", false) == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** LOAD PATHS **************************/

        if (loadPath(root, ACCESS_LOG_FILE, "AccessLogFile") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadPath(root, ERROR_LOG_FILE, "ErrorLogFile") == CONF_FAILURE)
            return CONF_FAILURE;

        #ifdef _WIN32
            if (loadPath(root, PHP_CGI_EXE_PATH, "WinPHPCGIPath") == CONF_FAILURE)
                return CONF_FAILURE;
        #endif

        /************ LOAD MATCHES & MIMES ************/

        pugi::xml_object_range matchNodes = root.children("Match");
        for (pugi::xml_node& match : matchNodes) {
            // Parse match
            Match* pMatch = loadMatch(match);
            if (pMatch == nullptr) return CONF_FAILURE;
            matchConfigs.push_back(pMatch);
        }

        if (loadMIMES() == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** LOAD TLS **************************/

        pugi::xml_node tlsPortNode = root.child("TLSPort");
        if (!tlsPortNode) {
            std::cerr << "Failed to parse config file, missing TLSPort node.\n";
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

        createLogDirectoryIfMissing(ACCESS_LOG_FILE);

        accessLogHandle = std::ofstream(ACCESS_LOG_FILE, std::ios_base::app);
        if (!accessLogHandle.is_open()) {
            std::cerr << "Failed to open Access Log File.\n";
            return CONF_FAILURE;
        }

        createLogDirectoryIfMissing(ERROR_LOG_FILE);

        errorLogHandle = std::ofstream(ERROR_LOG_FILE, std::ios_base::app);
        if (!errorLogHandle.is_open()) {
            std::cerr << "Failed to open Error Log File.\n";
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
        // Free any matches
        for (Match* pMatch : matchConfigs)
            delete pMatch;

        matchConfigs.clear();
    }

    /************************* Config loader helpers *************************/

    int loadUint(const pugi::xml_node& root, unsigned int& var, const std::string& nodeName, const bool allowZero) {
        const pugi::xml_node node = root.child(nodeName);
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
            std::cerr << "Failed to parse config file, invalid value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        return CONF_SUCCESS;
    }

    int loadUint(const pugi::xml_node& root, unsigned short& var, const std::string& nodeName, const bool allowZero) {
        const pugi::xml_node node = root.child(nodeName);
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
            std::cerr << "Failed to parse config file, invalid value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        return CONF_SUCCESS;
    }

    int loadOnOff(const pugi::xml_node& root, bool& var, const std::string& nodeName) {
        pugi::xml_node node = root.child(nodeName);
        if (!node) {
            std::cerr << "Failed to parse config file, missing " << nodeName << " node." << std::endl;
            return CONF_FAILURE;
        }

        // Extract and stringify
        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        // Verify valid value provided
        if (valueStr != "on" && valueStr != "off") {
            std::cerr << "Failed to parse config file, invalid value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        var = (valueStr == "on");
        return CONF_SUCCESS;
    }

    int loadTrueFalse(const pugi::xml_node& root, bool& var, const std::string& nodeName) {
        pugi::xml_node node = root.child(nodeName);
        if (!node) {
            std::cerr << "Failed to parse config file, missing " << nodeName << " node." << std::endl;
            return CONF_FAILURE;
        }

        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        // Verify valid value provided
        if (valueStr != "true" && valueStr != "false") {
            std::cerr << "Failed to parse config file, invalid value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        var = (valueStr == "true");
        return CONF_SUCCESS;
    }

    int loadPath(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName) {
        pugi::xml_node node = root.child(nodeName);
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
        if (value.find("./") == 0) { // Use relative path
            var = CWD / value.substr(2);
        } else { // Try as absolute path
            var = std::filesystem::path(value);

            if (!std::filesystem::exists(var)) {
                // Try as relative path (ex. "path" instead of "./path")
                var = CWD / value;
            }
        }

        if (!std::filesystem::exists(var)) {
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

            // Affix trailing slash
            if (var.string().size() > 0 && var.string().back() != '/')
                var = var.string() + '/';

            // Validate path exists and is a directory (passthru to catch block if failed)
            if (var.string().size() == 0 || !std::filesystem::exists(var) || !std::filesystem::is_directory(var))
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
                std::cerr << "Failed to open \"tmp\" directory: \"tmp\" already exists as a file in the Mercury root directory.\n";
                return CONF_FAILURE;
            }
        } catch (std::filesystem::filesystem_error&) {
            std::cerr << "Failed to create \"tmp\" directory in the Mercury root directory.\n";
            return CONF_FAILURE;
        }

        // Canonicalize the tmp path
        try {
            TMP_PATH = resolveCanonicalPath(tmpPathStr);
        } catch (...) {
            std::cerr << "Failed to canonicalize \"tmp\" directory.\n";
            return CONF_FAILURE;
        }

        return CONF_SUCCESS;
    }
}