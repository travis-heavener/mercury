#include "conf.hpp"

#include <cctype>
#include <iostream>
#include <mutex>
#include <utility>

#include <pugixml.hpp>

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include "../http/fastcgi/client.hpp"
#include "../io/file_tools.hpp"
#include "../util/privilege.hpp"
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
    bool ENABLE_CUSTOM_STATUS_DOCS;
    unsigned short MAX_REQUEST_BACKLOG;
    unsigned int MAX_REQUEST_LINE_LENGTH;
    unsigned int REQUEST_BUFFER_SIZE, RESPONSE_BUFFER_SIZE;
    unsigned int MAX_REQUEST_BODY, MAX_RESPONSE_BODY;
    unsigned int IDLE_THREADS_PER_CHILD, MAX_THREADS_PER_CHILD;
    std::vector<std::unique_ptr<Match>> matchConfigs;
    std::vector<std::string> INDEX_FILES;
    std::vector<std::unique_ptr<Redirect>> redirectRules;
    std::vector<std::unique_ptr<Rewrite>> rewriteRules;

    std::filesystem::path ACCESS_LOG_FILE;
    std::filesystem::path ERROR_LOG_FILE;
    int CLIENT_SECURITY_MODE;
    std::string IP_HASH_SALT;

    bool USE_TLS;
    port_t TLS_PORT;

    bool IS_PHP_ENABLED;
    #ifdef _WIN32
        std::filesystem::path PHP_CGI_EXE_PATH;
    #endif

    bool IS_FASTCGI_ENABLED;
    std::string FASTCGI_ADDRESS;
    unsigned int FASTCGI_TIMEOUT;

    bool SHOW_WELCOME_BANNER;
    bool SHOW_DONATION_BANNER;
    bool CHECK_LATEST_RELEASE;

    std::ofstream accessLogHandle;
    std::ofstream errorLogHandle;

    std::unordered_map<std::string, std::string> MIMES;

    // CWD in root directory of repo
    std::filesystem::path CWD, previousCWD = std::filesystem::current_path();

    /**********************************************************/
    /********************* STATIC METHODS *********************/
    /**********************************************************/

    const std::vector<std::string> mercuryNodeNames = {
        "DocumentRoot", "BindAddressIPv4", "BindAddressIPv6", "Port", "TLSPort", "Redirect", "Rewrite",
        "AccessLogFile", "ErrorLogFile", "ClientSecurityMode", "ClientSecurityIPSalt", "EnablePHPCGI", "WinPHPCGIPath", "EnableFastCGI", "FastCGIAddress", "FastCGITimeout", "EnableLegacyHTTPVersions", "EnableCustomStatusDocs",
        "Match", "KeepAlive", "KeepAliveMaxTimeout", "KeepAliveMaxRequests", "IndexFiles",
        "MaxRequestLineLength", "MaxRequestBacklog", "RequestBufferSize", "ResponseBufferSize", "MaxRequestBody", "MaxResponseBody",
        "MinResponseCompressionSize", "IdleThreadsPerChild", "MaxThreadsPerChild", "ShowWelcomeBanner", "ShowDonationBanner", "StartupCheckLatestRelease"
    };

    const std::vector<std::string> matchNodeNames = {
        "FilterIfHeaderMatch", "FilterIfNotHeaderMatch", "FilterIfHeaderExist", "FilterIfNotHeaderExist",
        "Header", "ShowDirectoryIndexes", "Access"
    };

    struct TagMismatchInfo {
        std::string expectedTag; // Expected closing tag name
        size_t openLine = 0;     // 1-based line number of opening tag
        size_t openCol = 0;      // 1-based column number of opening tag
        std::string foundTag;    // Actual closing tag name found
    };

    // Forward decs
    int precheckNodeNames(pugi::xml_document& doc);

    std::pair<size_t, size_t> getLineAndColumn(const std::string& filePath, ptrdiff_t offset);
    std::optional<TagMismatchInfo> findTagMismatch(const std::string& filePath);

    int loadMIMES();
    int resolvePath(const std::string& rawValue, std::filesystem::path& var, const std::string& nodeName, const bool isLogFile=false);
    int loadUint(const pugi::xml_node& root, unsigned int& var, const std::string& nodeName, const unsigned int defaultValue, const bool allowZero=true);
    int loadUint(const pugi::xml_node& root, unsigned short& var, const std::string& nodeName, const unsigned short defaultValue, const bool allowZero=true);
    int loadOnOff(const pugi::xml_node& root, bool& var, const std::string& nodeName, const bool defaultValue);
    int loadBindAddress(const pugi::xml_node& root, const bool isIPv6, const std::string& defaultAddr);
    int loadBool(const pugi::xml_node& root, bool& var, const std::string& nodeName, const bool defaultValue);
    int loadPath(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName, const bool isLogFile=false, const std::string& defaultPath="");
    int loadDirectoryAndCanonicalize(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName, const std::string& defaultPath="");
    int canonicalizeDirectory(std::filesystem::path& var, const std::string& nodeName);
    int setDocumentRoot(const std::filesystem::path& path);
    int loadTempFileDirectory();
    int loadClientSecurityMode(const pugi::xml_node& root, int& var, const int defaultMode=CLIENT_SEC_GPC_MASKED);
    int loadClientSecurityIPSalt(const pugi::xml_node& root, std::string& var, const std::string& defaultSalt="fAhGD0OnCe4YhSyx");
    int loadFastCGIAddress(const pugi::xml_node& root, std::string& var);

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

    // Print command line help documentation
    void printHelp() {
        std::cout << "Mercury - A lightweight, configurable HTTP server\n\n"
            << "Usage:\n"
            << "    mercury [options]\n"
            << "    mercury [options] -c <config-file>\n\n"
            << "Options:\n"
            << "    -c, --config <file>     Path to configuration file (default: " << CONF_FILE << ")\n"
            << "    -h, --help              Display this help message and exit\n"
            << "    -p, --port <port>       Override HTTP port\n"
            << "    -r, --root <path>       Override document root directory\n"
            << "    -t, --test              Test configuration file syntax and exit\n"
            << "    -v, --version           Display version information and exit\n\n"
            << "Examples:\n"
            << "    mercury\n"
            << "    mercury -c conf/mercury.conf\n"
            << "    mercury -t\n"
            << "    mercury -t -c /etc/mercury/mercury.conf\n"
            << "    mercury -r ./public -p 8080\n\n"
            << "For detailed configuration directives, see CONFIG.md or visit:\n"
            << "    https://mercury.wowtravis.com/" << std::endl;
    }

    // Print server version
    void printVersion() {
        std::cout << (VERSION.empty() ? "Mercury" : VERSION) << std::endl;
    }

    // Used to calculate 1-based line and column numbers from byte offset
    std::pair<size_t, size_t> getLineAndColumn(const std::string& filePath, ptrdiff_t offset) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return {0, 0};

        size_t line = 1;
        size_t col = 1;
        char ch = 0;
        for (ptrdiff_t i = 0; i < offset && file.get(ch); ++i) {
            if (ch == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        return {line, col};
    }

    // Used to find unclosed or mismatched tags for detailed diagnostics
    std::optional<TagMismatchInfo> findTagMismatch(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return std::nullopt;

        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        file.close();

        struct OpenTag {
            std::string name; // Element tag name
            size_t line;      // 1-based line number
            size_t col;       // 1-based column number
        };

        std::vector<OpenTag> tagStack;
        size_t i = 0;
        size_t line = 1;
        size_t col = 1;

        auto advance = [&](size_t count = 1) {
            for (size_t c = 0; c < count && i < content.size(); ++c) {
                if (content[i] == '\n') {
                    ++line;
                    col = 1;
                } else {
                    ++col;
                }
                ++i;
            }
        };

        while (i < content.size()) {
            if (content[i] != '<') {
                advance();
                continue;
            }

            const size_t tagLine = line;
            const size_t tagCol = col;

            // Check comment
            if (i + 3 < content.size() && content.compare(i, 4, "<!--") == 0) {
                advance(4);
                while (i + 2 < content.size() &&
                       !(content[i] == '-' && content[i + 1] == '-' && content[i + 2] == '>')) {
                    advance();
                }
                if (i + 2 < content.size()) advance(3);
                continue;
            }

            // Check CDATA section
            if (i + 8 < content.size() && content.compare(i, 9, "<![CDATA[") == 0) {
                advance(9);
                while (i + 2 < content.size() &&
                       !(content[i] == ']' && content[i + 1] == ']' && content[i + 2] == '>')) {
                    advance();
                }
                if (i + 2 < content.size()) advance(3);
                continue;
            }

            // Check processing instruction
            if (i + 1 < content.size() && content.compare(i, 2, "<?") == 0) {
                advance(2);
                while (i + 1 < content.size() &&
                       !(content[i] == '?' && content[i + 1] == '>')) {
                    advance();
                }
                if (i + 1 < content.size()) advance(2);
                continue;
            }

            // Check declaration
            if (i + 1 < content.size() && content[i + 1] == '!') {
                advance(2);
                while (i < content.size() && content[i] != '>') {
                    advance();
                }
                if (i < content.size()) advance();
                continue;
            }

            // Check closing tag
            if (i + 1 < content.size() && content[i + 1] == '/') {
                advance(2);

                while (i < content.size() && std::isspace(static_cast<unsigned char>(content[i]))) {
                    advance();
                }

                std::string closingName;
                while (i < content.size() && !std::isspace(static_cast<unsigned char>(content[i])) &&
                       content[i] != '>') {
                    closingName += content[i];
                    advance();
                }

                while (i < content.size() && content[i] != '>') {
                    advance();
                }
                if (i < content.size()) advance();

                if (tagStack.empty()) return TagMismatchInfo{"", 0, 0, closingName};

                if (tagStack.back().name != closingName)
                    return TagMismatchInfo{tagStack.back().name, tagStack.back().line, tagStack.back().col, closingName};

                tagStack.pop_back();
                continue;
            }

            // Check opening tag
            advance();

            while (i < content.size() && std::isspace(static_cast<unsigned char>(content[i]))) {
                advance();
            }

            std::string openName;
            while (i < content.size() && !std::isspace(static_cast<unsigned char>(content[i])) &&
                   content[i] != '>' && content[i] != '/') {
                openName += content[i];
                advance();
            }

            if (openName.empty()) continue;

            bool isSelfClosing = false;
            char inQuote = 0;
            while (i < content.size()) {
                const char c = content[i];
                if (inQuote != 0) {
                    if (c == inQuote) inQuote = 0;
                } else {
                    if (c == '"' || c == '\'') {
                        inQuote = c;
                    } else if (c == '/' && i + 1 < content.size() && content[i + 1] == '>') {
                        isSelfClosing = true;
                    } else if (c == '>') {
                        advance();
                        break;
                    }
                }
                advance();
            }

            if (!isSelfClosing) {
                tagStack.push_back({openName, tagLine, tagCol});
            }
        }

        if (!tagStack.empty())
            return TagMismatchInfo{tagStack.back().name, tagStack.back().line, tagStack.back().col, ""};

        return std::nullopt;
    }

    int loadConfig(int argc, char* argv[]) {
        // Set CWD to Mercury project root
        if (!loadExecutableDirectory(CWD)) {
            std::cerr << "Failed to set current working directory." << std::endl;
            return CONF_FAILURE;
        }

        std::filesystem::current_path( CWD = CWD.parent_path() );

        // Load version
        std::ifstream versionHandle( VERSION_FILE );
        if (versionHandle.is_open()) {
            std::getline(versionHandle, VERSION);
            if (!VERSION.empty() && VERSION.back() == '\n') VERSION.pop_back();
            if (!VERSION.empty() && VERSION.back() == '\r') VERSION.pop_back();
            versionHandle.close();
        }

        // Parse CLI arguments
        std::string configPath = CONF_FILE;
        std::optional<std::string> overrideDocumentRoot;
        std::optional<port_t> overridePort;
        bool isTestMode = false;

        auto parsePortValue = [](const std::string& portStr, const std::string& optName) -> std::optional<port_t> {
            try {
                if (portStr.empty() || portStr[0] == '-') throw std::invalid_argument("");
                for (char c : portStr) {
                    if (!std::isdigit(static_cast<unsigned char>(c)))
                        throw std::invalid_argument("");
                }
                const unsigned long p = std::stoul(portStr);
                if (p == 0 || p > 65535) throw std::out_of_range("");
                return static_cast<port_t>(p);
            } catch (...) {
                std::cerr << "Error: Invalid port number '" << portStr << "' for option '" << optName << "'. Port must be between 1 and 65535.\n"
                    << "Try 'mercury --help' for usage information." << std::endl;
                return std::nullopt;
            }
        };

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];

            if (arg == "-h" || arg == "--help") {
                printHelp();
                return CONF_EXIT;
            }

            if (arg == "-v" || arg == "--version") {
                printVersion();
                return CONF_EXIT;
            }

            if (arg == "-t" || arg == "--test" || arg == "--test-config") {
                isTestMode = true;
            } else if (arg == "-c" || arg == "--config") {
                if (++i >= argc) {
                    std::cerr << "Error: Option '" << arg << "' requires a configuration file path.\n"
                        << "Try 'mercury --help' for usage information." << std::endl;
                    return CONF_FAILURE;
                }
                configPath = argv[i];
            } else if (arg.starts_with("-c=")) {
                configPath = arg.substr(3);
            } else if (arg.starts_with("--config=")) {
                configPath = arg.substr(9);
            } else if (arg == "-r" || arg == "--root") {
                if (++i >= argc) {
                    std::cerr << "Error: Option '" << arg << "' requires a document root path.\n"
                        << "Try 'mercury --help' for usage information." << std::endl;
                    return CONF_FAILURE;
                }
                overrideDocumentRoot = argv[i];
            } else if (arg.starts_with("-r=")) {
                overrideDocumentRoot = arg.substr(3);
            } else if (arg.starts_with("--root=")) {
                overrideDocumentRoot = arg.substr(7);
            } else if (arg == "-p" || arg == "--port") {
                if (++i >= argc) {
                    std::cerr << "Error: Option '" << arg << "' requires a port number.\n"
                        << "Try 'mercury --help' for usage information." << std::endl;
                    return CONF_FAILURE;
                }
                auto parsed = parsePortValue(argv[i], arg);
                if (!parsed.has_value()) return CONF_FAILURE;
                overridePort = *parsed;
            } else if (arg.starts_with("-p=")) {
                auto parsed = parsePortValue(arg.substr(3), "-p");
                if (!parsed.has_value()) return CONF_FAILURE;
                overridePort = *parsed;
            } else if (arg.starts_with("--port=")) {
                auto parsed = parsePortValue(arg.substr(7), "--port");
                if (!parsed.has_value()) return CONF_FAILURE;
                overridePort = *parsed;
            } else if (!arg.starts_with("-")) {
                // Positional argument for backward compatibility
                configPath = arg;
            } else {
                std::cerr << "Error: Unrecognized option '" << arg << "'.\n"
                    << "Try 'mercury --help' for usage information." << std::endl;
                return CONF_FAILURE;
            }
        }

        if (configPath.empty()) {
            std::cerr << "Error: Configuration file path cannot be empty.\n"
                << "Try 'mercury --help' for usage information." << std::endl;
            return CONF_FAILURE;
        }

        if (overrideDocumentRoot.has_value() && overrideDocumentRoot->empty()) {
            std::cerr << "Error: Document root path cannot be empty.\n"
                << "Try 'mercury --help' for usage information." << std::endl;
            return CONF_FAILURE;
        }

        // Resolve configuration file path relative to invocation directory if necessary
        std::filesystem::path resolvedConfigPath = configPath;
        std::error_code ecConf;
        if (!resolvedConfigPath.is_absolute() && std::filesystem::exists(previousCWD / resolvedConfigPath, ecConf)) {
            resolvedConfigPath = previousCWD / resolvedConfigPath;
        }

        // Read XML config file
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(resolvedConfigPath.c_str());

        if (!result) {
            std::error_code ec;
            if (result.status == pugi::status_file_not_found || !std::filesystem::exists(resolvedConfigPath, ec)) {
                std::cerr << "Failed to open config file (" << configPath << "): File does not exist." << std::endl;
            } else if (std::filesystem::is_directory(resolvedConfigPath, ec)) {
                std::cerr << "Failed to open config file (" << configPath << "): Path is a directory." << std::endl;
            } else if (result.status == pugi::status_io_error) {
                std::cerr << "Failed to open config file (" << configPath << "): Error reading file." << std::endl;
            } else if (result.status == pugi::status_out_of_memory) {
                std::cerr << "Failed to load config file (" << configPath << "): Out of memory." << std::endl;
            } else {
                const auto [line, col] = getLineAndColumn(resolvedConfigPath.string(), result.offset);
                std::string errorDesc = result.description();

                if (result.status == pugi::status_end_element_mismatch) {
                    const auto mismatch = findTagMismatch(resolvedConfigPath.string());
                    if (mismatch) {
                        if (!mismatch->expectedTag.empty() && !mismatch->foundTag.empty()) {
                            errorDesc += " (expected '</" + mismatch->expectedTag + ">' for tag opened at line "
                                + std::to_string(mismatch->openLine) + ", column " + std::to_string(mismatch->openCol)
                                + ", but found '</" + mismatch->foundTag + ">')";
                        } else if (!mismatch->expectedTag.empty()) {
                            errorDesc += " (unclosed tag '<" + mismatch->expectedTag + ">' opened at line "
                                + std::to_string(mismatch->openLine) + ", column " + std::to_string(mismatch->openCol) + ")";
                        } else if (!mismatch->foundTag.empty()) {
                            errorDesc += " (unexpected closing tag '</" + mismatch->foundTag + ">')";
                        }
                    }
                }

                if (line > 0) {
                    std::cerr << "Failed to parse XML syntax in config file (" << configPath
                        << ") at line " << line << ", column " << col << ": "
                        << errorDesc << "." << std::endl;
                } else {
                    std::cerr << "Failed to parse XML syntax in config file (" << configPath
                        << "): " << errorDesc << "." << std::endl;
                }
            }
            return CONF_FAILURE;
        }

        // Precheck all nodes for invalid node names
        if (precheckNodeNames(doc) == CONF_FAILURE)
            return CONF_FAILURE;

        // Load tmp file directory
        if (loadTempFileDirectory() == CONF_FAILURE)
            return CONF_FAILURE;

        // Extract root node
        pugi::xml_node root = doc.child("Mercury");
        if (!root) {
            std::cerr << "Failed to parse config file, missing root \"Mercury\" node." << std::endl;
            return CONF_FAILURE;
        }

        /************************** Extract DocumentRoot **************************/

        if (overrideDocumentRoot.has_value()) {
            if (setDocumentRoot(*overrideDocumentRoot) == CONF_FAILURE)
                return CONF_FAILURE;
        } else {
            if (loadDirectoryAndCanonicalize(root, DOCUMENT_ROOT, "DocumentRoot", "./public/") == CONF_FAILURE)
                return CONF_FAILURE;
        }

        /************************** LOAD IPv4/IPv6 TOGGLES **************************/

        if (loadBindAddress(root, false, "0.0.0.0") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadBindAddress(root, true, "::") == CONF_FAILURE)
            return CONF_FAILURE;

        if (!IS_IPV4_ENABLED && !IS_IPV6_ENABLED) {
            std::cerr << "Either IPv4 or IPv6 must be enabled in the config file!" << std::endl;
            return CONF_FAILURE;
        }

        /************ Load ClientSecurityMode ************/

        if (loadClientSecurityMode(root, CLIENT_SECURITY_MODE) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadClientSecurityIPSalt(root, IP_HASH_SALT) == CONF_FAILURE)
            return CONF_FAILURE;

        /************ LOAD BOOLEANS ************/

        if (loadOnOff(root, ENABLE_LEGACY_HTTP, "EnableLegacyHTTPVersions", true) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadOnOff(root, ENABLE_CUSTOM_STATUS_DOCS, "EnableCustomStatusDocs", true) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadOnOff(root, IS_PHP_ENABLED, "EnablePHPCGI", false) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadOnOff(root, IS_FASTCGI_ENABLED, "EnableFastCGI", false) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadFastCGIAddress(root, FASTCGI_ADDRESS) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, FASTCGI_TIMEOUT, "FastCGITimeout", 10, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadOnOff(root, IS_KEEP_ALIVE_ENABLED, "KeepAlive", true) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadBool(root, SHOW_WELCOME_BANNER, "ShowWelcomeBanner", true) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadBool(root, SHOW_DONATION_BANNER, "ShowDonationBanner", true) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadBool(root, CHECK_LATEST_RELEASE, "StartupCheckLatestRelease", true) == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** Load IndexFiles **************************/

        pugi::xml_node indexFileNode = root.child("IndexFiles");
        std::string indexFilesRaw;
        if (!indexFileNode) {
            indexFilesRaw = "index.html, index.htm, index.php";
        } else {
            indexFilesRaw = indexFileNode.text().as_string();
        }
        trimString(indexFilesRaw);
        INDEX_FILES.clear();
        splitString(INDEX_FILES, indexFilesRaw, ',', true);

        // Validate all index files
        for (const std::string& str : INDEX_FILES) {
            if (str.size() == 0 || str.find('/') != std::string::npos || str.find('\\') != std::string::npos) {
                std::cerr << "Failed to parse config file, invalid IndexFiles value." << std::endl;
                return CONF_FAILURE;
            }
        }

        /************ LOAD UINTS/USHORTS ************/

        if (loadUint(root, PORT, "Port", 80, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (overridePort.has_value())
            PORT = *overridePort;

        if (loadUint(root, MAX_REQUEST_LINE_LENGTH, "MaxRequestLineLength", 4096, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_REQUEST_BACKLOG, "MaxRequestBacklog", 100, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, REQUEST_BUFFER_SIZE, "RequestBufferSize", 16384, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, RESPONSE_BUFFER_SIZE, "ResponseBufferSize", 16384, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_REQUEST_BODY, "MaxRequestBody", 268435456) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_RESPONSE_BODY, "MaxResponseBody", 268435456) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, IDLE_THREADS_PER_CHILD, "IdleThreadsPerChild", 12, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_THREADS_PER_CHILD, "MaxThreadsPerChild", 60, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (MAX_THREADS_PER_CHILD <= IDLE_THREADS_PER_CHILD) {
            std::cerr << "Failed to parse config file, MaxThreadsPerChild must be greater than IdleThreadsPerChild." << std::endl;
            return CONF_FAILURE;
        }

        if (loadUint(root, KEEP_ALIVE_TIMEOUT, "KeepAliveMaxTimeout", 3, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MAX_KEEP_ALIVE_REQUESTS, "KeepAliveMaxRequests", 100, LOAD_UINT_FORBID_ZERO) == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadUint(root, MIN_COMPRESSION_SIZE, "MinResponseCompressionSize", 750) == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** LOAD PATHS **************************/

        if (loadPath(root, ACCESS_LOG_FILE, "AccessLogFile", true, "./logs/access.log") == CONF_FAILURE)
            return CONF_FAILURE;

        if (loadPath(root, ERROR_LOG_FILE, "ErrorLogFile", true, "./logs/error.log") == CONF_FAILURE)
            return CONF_FAILURE;

        #ifdef _WIN32
            if (IS_PHP_ENABLED && loadPath(root, PHP_CGI_EXE_PATH, "WinPHPCGIPath", false, "./php/php-cgi.exe") == CONF_FAILURE)
                return CONF_FAILURE;
        #endif

        /************ LOAD MATCHES, REDIRECTS/REWRITES, & MIMES ************/

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

        pugi::xml_object_range rewriteRuleNodes = root.children("Rewrite");
        for (pugi::xml_node& rewriteRule : rewriteRuleNodes) {
            // Parse match
            std::unique_ptr<Rewrite> pRewrite = loadRewrite(rewriteRule);
            if (pRewrite == nullptr) return CONF_FAILURE;
            rewriteRules.push_back( std::move(pRewrite) );
        }

        if (loadMIMES() == CONF_FAILURE)
            return CONF_FAILURE;

        /************************** LOAD TLS **************************/

        pugi::xml_node tlsPortNode = root.child("TLSPort");
        std::string tlsPortRaw = "off";
        if (tlsPortNode) {
            tlsPortRaw = tlsPortNode.text().as_string();
            trimString(tlsPortRaw);
        }

        // If TLS is enabled, grab the port
        USE_TLS = tlsPortRaw != "off";
        try {
            // Prevent negatives
            if (tlsPortRaw.size() == 0) throw std::invalid_argument("");
            if (USE_TLS && tlsPortRaw[0] == '-') throw std::invalid_argument("");
            unsigned long val = USE_TLS ? std::stoul(tlsPortRaw) : 0;
            if (USE_TLS && (val == 0 || val > 65535)) throw std::invalid_argument("");
            TLS_PORT = USE_TLS ? static_cast<port_t>(val) : 0;
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
        util::privilege::applyOwnershipAndPermissions(ACCESS_LOG_FILE, 0644);

        errorLogHandle = std::ofstream(ERROR_LOG_FILE, std::ios_base::app);
        if (!errorLogHandle.is_open()) {
            std::cerr << "Failed to open Error Log File." << std::endl;
            return CONF_FAILURE;
        }
        util::privilege::applyOwnershipAndPermissions(ERROR_LOG_FILE, 0644);

        // Handle configuration test mode
        if (isTestMode) {
            accessLogHandle.close();
            errorLogHandle.close();
            cleanupConfig();
            std::cout << "Configuration file " << configPath << " syntax is OK\n"
                << "Configuration file " << configPath << " test is successful" << std::endl;
            return CONF_EXIT;
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
        // Clear match configs and rules
        matchConfigs.clear();
        rewriteRules.clear();
        redirectRules.clear();

        // Reset FastCGI settings
        IS_FASTCGI_ENABLED = false;
        FASTCGI_ADDRESS = "auto";
        FASTCGI_TIMEOUT = 10;

        // Remove any stray temp files under lock
        std::unordered_set<std::string> remainingTempFiles;
        {
            std::unique_lock lock(tempFileSetMutex);
            remainingTempFiles = std::move(currentTempFiles);
        }

        for (const std::string& tmpPath : remainingTempFiles) {
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
        }

        // Reset CWD
        std::error_code ec;
        std::filesystem::current_path( previousCWD, ec );
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

    int precheckNodeNames(pugi::xml_document& doc) {
        // Check each node for invalid root names
        const std::string rootNodeName = "Mercury";
        for (auto itr = doc.begin(); itr != doc.end(); ++itr) {
            if (itr->name() != rootNodeName) {
                std::cerr << "Invalid node name in XML root: " << itr->name() << std::endl;
                return CONF_FAILURE;
            }
        }

        // Check each node for invalid node names in Mercury root
        pugi::xml_node root = doc.child("Mercury");
        for (auto itr = root.begin(); itr != root.end(); ++itr) {
            bool isFound = false;
            for (const std::string& name : mercuryNodeNames) {
                if (name == itr->name()) {
                    isFound = true;
                    break;
                }
            }

            if (isFound) continue;

            // Base case
            std::cerr << "Invalid node name in Mercury root node: " << itr->name() << std::endl;
            return CONF_FAILURE;
        }

        // Check each Match node for invalid node names in Mercury root
        pugi::xml_object_range matchNodes = root.children("Match");
        for (pugi::xml_node& match : matchNodes) {
            for (auto itr = match.begin(); itr != match.end(); ++itr) {
                bool isFound = false;
                for (const std::string& name : matchNodeNames) {
                    if (name == itr->name()) {
                        isFound = true;
                        break;
                    }
                }

                if (isFound) continue;

                // Base case
                std::cerr << "Invalid node name in Match node: " << itr->name() << std::endl;
                return CONF_FAILURE;
            }
        }

        // Base case, is valid
        return CONF_SUCCESS;
    }

    /************************* Config loader helpers *************************/

    int loadUint(const pugi::xml_node& root, unsigned int& var, const std::string& nodeName, const unsigned int defaultValue, const bool allowZero) {
        const pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            var = defaultValue;
            return CONF_SUCCESS;
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

    int loadUint(const pugi::xml_node& root, unsigned short& var, const std::string& nodeName, const unsigned short defaultValue, const bool allowZero) {
        const pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            var = defaultValue;
            return CONF_SUCCESS;
        }

        // Extract and stringify
        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        try {
            // Prevent negatives
            if (valueStr.size() == 0) throw std::invalid_argument("");
            if (valueStr[0] == '-') throw std::invalid_argument("");

            unsigned long val = std::stoul(valueStr);
            if (!allowZero && val == 0) throw std::invalid_argument("");
            if (val > 65535) throw std::invalid_argument("");
            var = static_cast<unsigned short>(val);
        } catch (std::invalid_argument&) {
            std::cerr << "Failed to parse config file, invalid unsigned integer value for " << nodeName << '.' << std::endl;
            return CONF_FAILURE;
        }

        return CONF_SUCCESS;
    }

    int loadOnOff(const pugi::xml_node& root, bool& var, const std::string& nodeName, const bool defaultValue) {
        pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            var = defaultValue;
            return CONF_SUCCESS;
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

    int loadBindAddress(const pugi::xml_node& root, const bool isIPv6, const std::string& defaultAddr) {
        const std::string nodeName = isIPv6 ? "BindAddressIPv6" : "BindAddressIPv4";
        pugi::xml_node node = root.child(nodeName.c_str());

        std::string rawValue;
        if (!node) {
            rawValue = defaultAddr;
        } else {
            rawValue = node.text().as_string();
            trimString(rawValue);
        }

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

    int loadBool(const pugi::xml_node& root, bool& var, const std::string& nodeName, const bool defaultValue) {
        pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            var = defaultValue;
            return CONF_SUCCESS;
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

    int resolvePath(const std::string& rawValue, std::filesystem::path& var, const std::string& nodeName, const bool isLogFile) {
        std::string value = rawValue;
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

    int loadPath(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName, const bool isLogFile, const std::string& defaultPath) {
        pugi::xml_node node = root.child(nodeName.c_str());
        if (!node) {
            if (defaultPath.empty()) {
                std::cerr << "Failed to parse config file, missing " << nodeName << '.' << std::endl;
                return CONF_FAILURE;
            }
            return resolvePath(defaultPath, var, nodeName, isLogFile);
        }

        return resolvePath(node.text().as_string(), var, nodeName, isLogFile);
    }

    // Canonicalize directory path, affix trailing slash, and verify existence
    int canonicalizeDirectory(std::filesystem::path& var, const std::string& nodeName) {
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

    int loadDirectoryAndCanonicalize(const pugi::xml_node& root, std::filesystem::path& var, const std::string& nodeName, const std::string& defaultPath) {
        if (loadPath(root, var, nodeName, false, defaultPath) == CONF_FAILURE) return CONF_FAILURE;
        return canonicalizeDirectory(var, nodeName);
    }

    // Canonicalize and set document root directory
    int setDocumentRoot(const std::filesystem::path& path) {
        std::filesystem::path target = path;
        if (!target.is_absolute()) {
            std::error_code ec;
            if (std::filesystem::exists(previousCWD / target, ec))
                target = previousCWD / target;
        }

        if (canonicalizeDirectory(target, "DocumentRoot") == CONF_FAILURE)
            return CONF_FAILURE;

        DOCUMENT_ROOT = target;
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
            util::privilege::applyOwnershipAndPermissions(tmpPathStr, 0755);
        } catch (std::filesystem::filesystem_error&) {
            std::cerr << "Failed to create \"tmp\" directory in the Mercury root directory." << std::endl;
            return CONF_FAILURE;
        }

        // Set the temp path
        TMP_PATH = tmpPathStr;

        return CONF_SUCCESS;
    }

    int loadClientSecurityMode(const pugi::xml_node& root, int& var, const int defaultMode) {
        pugi::xml_node node = root.child("ClientSecurityMode");
        if (!node) {
            var = defaultMode;
            return CONF_SUCCESS;
        }

        // Extract and stringify
        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        // Verify valid value provided
        if (valueStr == "Minimal") {
            var = CLIENT_SEC_MINIMAL;
        } else if (valueStr == "GPCMasked") {
            var = CLIENT_SEC_GPC_MASKED;
        } else if (valueStr == "GPCHashed") {
            var = CLIENT_SEC_GPC_HASHED;
        } else if (valueStr == "Masked") {
            var = CLIENT_SEC_MASKED;
        } else if (valueStr == "Hashed") {
            var = CLIENT_SEC_HASHED;
        } else {
            std::cerr << "Failed to parse config file, invalid value for ClientSecurityMode." << std::endl;
            return CONF_FAILURE;
        }

        // Base case
        return CONF_SUCCESS;
    }

    int loadClientSecurityIPSalt(const pugi::xml_node& root, std::string& var, const std::string& defaultSalt) {
        pugi::xml_node node = root.child("ClientSecurityIPSalt");
        if (!node) {
            var = defaultSalt;
            return CONF_SUCCESS;
        }

        // Extract and stringify
        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        // Verify valid value provided
        if (valueStr.size() < 16) {
            std::cerr << "Failed to parse config file, ClientSecurityIPSalt hash salt must be at least 128 bits." << std::endl;
            return CONF_FAILURE;
        }

        // Base case
        var = valueStr;
        return CONF_SUCCESS;
    }

    int loadFastCGIAddress(const pugi::xml_node& root, std::string& var) {
        pugi::xml_node node = root.child("FastCGIAddress");
        if (!node) {
            var = "auto";
            return CONF_SUCCESS;
        }

        std::string valueStr = node.text().as_string();
        trimString(valueStr);

        if (valueStr.empty() || valueStr == "auto") {
            var = "auto";
            return CONF_SUCCESS;
        }

        // Validate address syntax via Endpoint::parse
        http::fastcgi::Endpoint endpoint;
        if (!http::fastcgi::Endpoint::parse(valueStr, endpoint)) {
            std::cerr << "Failed to parse config file, invalid FastCGIAddress: " << valueStr << std::endl;
            return CONF_FAILURE;
        }

        var = valueStr;
        return CONF_SUCCESS;
    }
}

#undef LOAD_UINT_FORBID_ZERO
