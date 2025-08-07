#include "conf.hpp"

/****** EXTERNAL FIELDS ******/

namespace conf {

    std::string VERSION;

    std::filesystem::path DOCUMENT_ROOT;
    port_t PORT;
    bool IS_IPV4_ENABLED;
    bool IS_IPV6_ENABLED;
    unsigned short MAX_REQUEST_BACKLOG;
    unsigned int MAX_REQUEST_BUFFER;
    unsigned int THREADS_PER_CHILD;
    std::vector<conf::Match*> matchConfigs;
    std::string INDEX_FILE;

    std::filesystem::path ACCESS_LOG_FILE;
    std::filesystem::path ERROR_LOG_FILE;

    bool USE_TLS;
    port_t TLS_PORT;

    std::ofstream accessLogHandle;
    std::ofstream errorLogHandle;

    std::unordered_map<std::string, std::string> MIMES;

    // CWD in root directory of repo
    std::filesystem::path CWD = std::filesystem::current_path().parent_path();

}

/*****************************/

// Forward dec
int loadMIMES();

int loadConfig() {
    using namespace conf;

    // Read XML config file
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(CONF_FILE);

    if (!result) {
        std::cerr << "Failed to open config file.\n";
        return CONF_FAILURE;
    }

    // Load version
    std::ifstream versionHandle( VERSION_FILE );
    std::getline(versionHandle, VERSION);
    if (VERSION.back() == '\n') VERSION.pop_back();
    if (VERSION.back() == '\r') VERSION.pop_back();
    versionHandle.close();

    // Extract root node
    pugi::xml_node root = doc.child("Mercury");
    if (!root) {
        std::cerr << "Failed to parse config file, missing root node.\n";
        return CONF_FAILURE;
    }

    /************************** Extract DocumentRoot **************************/
    pugi::xml_node documentRootNode = root.child("DocumentRoot");
    if (!documentRootNode) {
        std::cerr << "Failed to parse config file, missing DocumentRoot node.\n";
        return CONF_FAILURE;
    }

    std::string documentRootStr( documentRootNode.text().as_string() );
    trimString(documentRootStr);

    if (documentRootStr.find(".") == 0) // Relative paths
        DOCUMENT_ROOT = CWD / documentRootStr;
    else // Absolute paths
        DOCUMENT_ROOT = documentRootStr;

    // Resolve as absolute path
    try {
        DOCUMENT_ROOT = resolveCanonicalPath(DOCUMENT_ROOT);

        // Affix trailing slash
        if (DOCUMENT_ROOT.string().size() > 0 && DOCUMENT_ROOT.string().back() != '/')
            DOCUMENT_ROOT = DOCUMENT_ROOT.string() + '/';

        if (DOCUMENT_ROOT.string().size() == 0 || !std::filesystem::exists(DOCUMENT_ROOT) || !std::filesystem::is_directory(DOCUMENT_ROOT)) {
            std::cerr << "Failed to parse config file, DocumentRoot points to invalid/nonexistant directory.\n";
            return CONF_FAILURE;
        }
    } catch (std::filesystem::filesystem_error&) {
        // Handle std::filesystem::canonical failure
        std::cerr << "Failed to parse config file, DocumentRoot points to invalid/nonexistant directory.\n";
        return CONF_FAILURE;
    } catch (std::runtime_error&) {
        // Handle canonical failure from IO error
        std::cerr << "Failed to parse config file, DocumentRoot points to invalid/nonexistant directory.\n";
        return CONF_FAILURE;
    }

    /************************** Extract Port **************************/
    pugi::xml_node portNode = root.child("Port");
    if (!portNode) {
        std::cerr << "Failed to parse config file, missing Port node.\n";
        return CONF_FAILURE;
    }

    PORT = portNode.text().as_uint();

    /************************** Extract IPv4 toggle **************************/
    pugi::xml_node ipv4Node = root.child("EnableIPv4");
    if (!ipv4Node) {
        std::cerr << "Failed to parse config file, missing EnableIPv4 node.\n";
        return CONF_FAILURE;
    }

    std::string ipv4StatusStr = ipv4Node.text().as_string();
    trimString(ipv4StatusStr);

    // Verify valid value provided
    if (ipv4StatusStr != "on" && ipv4StatusStr != "off") {
        std::cerr << "Failed to parse config file, invalid value for EnableIPv4.\n";
        return CONF_FAILURE;
    }

    IS_IPV4_ENABLED = ipv4StatusStr == "on";

    /************************** Extract IPv6 toggle **************************/
    pugi::xml_node ipv6Node = root.child("EnableIPv6");
    if (!ipv6Node) {
        std::cerr << "Failed to parse config file, missing EnableIPv6 node.\n";
        return CONF_FAILURE;
    }

    std::string ipv6StatusStr = ipv6Node.text().as_string();
    trimString(ipv6StatusStr);

    // Verify valid value provided
    if (ipv6StatusStr != "on" && ipv6StatusStr != "off") {
        std::cerr << "Failed to parse config file, invalid value for EnableIPv6.\n";
        return CONF_FAILURE;
    }

    IS_IPV6_ENABLED = ipv6StatusStr == "on";

    if (!IS_IPV4_ENABLED && !IS_IPV6_ENABLED) {
        std::cerr << "Either IPv4 or IPv6 must be enabled in the config file!\n";
        return CONF_FAILURE;
    }

    /************************** Extract IndexFile **************************/
    pugi::xml_node indexFileNode = root.child("IndexFile");
    if (!indexFileNode) {
        std::cerr << "Failed to parse config file, missing IndexFile node.\n";
        return CONF_FAILURE;
    }

    INDEX_FILE = indexFileNode.text().as_string();
    trimString(INDEX_FILE);

    if (!INDEX_FILE.size() || INDEX_FILE.find('/') != std::string::npos || INDEX_FILE.find('\\') != std::string::npos) {
        std::cerr << "Failed to parse config file, invalid IndexFile value.\n";
        return CONF_FAILURE;
    }

    /************************** Extract Matches **************************/
    // Load all Matches
    pugi::xml_object_range matchNodes = root.children("Match");

    for (pugi::xml_node& match : matchNodes) {
        // Parse match
        Match* pMatch = loadMatch(match);
        if (pMatch == nullptr) return CONF_FAILURE;
        matchConfigs.push_back(pMatch);
    }

    /************************** Extract MaxRequestBacklog **************************/
    pugi::xml_node reqBacklogNode = root.child("MaxRequestBacklog");
    if (!reqBacklogNode) {
        std::cerr << "Failed to parse config file, missing MaxRequestBacklog node.\n";
        return CONF_FAILURE;
    }

    MAX_REQUEST_BACKLOG = reqBacklogNode.text().as_uint();

    /************************** Extract MaxRequestBuffer **************************/
    pugi::xml_node reqBufferNode = root.child("MaxRequestBuffer");
    if (!reqBufferNode) {
        std::cerr << "Failed to parse config file, missing MaxRequestBuffer node.\n";
        return CONF_FAILURE;
    }

    MAX_REQUEST_BUFFER = reqBufferNode.text().as_uint();

    /************************** Extract ThreadsPerChild **************************/
    pugi::xml_node threadsPerChildNode = root.child("ThreadsPerChild");
    if (!threadsPerChildNode) {
        std::cerr << "Failed to parse config file, missing ThreadsPerChild node.\n";
        return CONF_FAILURE;
    }

    THREADS_PER_CHILD = threadsPerChildNode.text().as_uint();

    /************************** Extract AccessLogFile **************************/
    pugi::xml_node accessLogNode = root.child("AccessLogFile");
    if (!accessLogNode) {
        std::cerr << "Failed to parse config file, missing AccessLogFile node.\n";
        return CONF_FAILURE;
    }

    std::string accessFileStr( accessLogNode.text().as_string() );
    trimString(accessFileStr);

    // Format string
    ACCESS_LOG_FILE = (accessFileStr.find("./") == 0) ? (CWD / accessFileStr.substr(2)) : std::filesystem::path(accessFileStr);

    if (!ACCESS_LOG_FILE.string().size()) {
        std::cerr << "Failed to parse config file, invalid Access Log File.\n";
        return CONF_FAILURE;
    }

    /************************** Extract ErrorLogFile **************************/
    pugi::xml_node errorLogNode = root.child("ErrorLogFile");
    if (!errorLogNode) {
        std::cerr << "Failed to parse config file, missing ErrorLogFile node.\n";
        return CONF_FAILURE;
    }

    std::string errorFileStr( errorLogNode.text().as_string() );
    trimString(errorFileStr);

    // Format string
    ERROR_LOG_FILE = (errorFileStr.find("./") == 0) ? (CWD / errorFileStr.substr(2)) : std::filesystem::path(errorFileStr);

    if (!ERROR_LOG_FILE.string().size()) {
        std::cerr << "Failed to parse config file, invalid Error Log File.\n";
        return CONF_FAILURE;
    }

    /************************** Load TLS **************************/
    pugi::xml_node tlsPortNode = root.child("TLSPort");
    if (!tlsPortNode) {
        std::cerr << "Failed to parse config file, missing TLSPort node.\n";
        return CONF_FAILURE;
    }

    std::string tlsPortRaw = tlsPortNode.text().as_string();
    trimString(tlsPortRaw);
    
    // If TLS is enabled, grab the port
    USE_TLS = tlsPortRaw != "off";
    TLS_PORT = USE_TLS ? std::stoull(tlsPortRaw) : 0;

    /************************** Load known MIMES **************************/
    if (loadMIMES() == CONF_FAILURE)
        return CONF_FAILURE;

    /************************** Open log files **************************/
    accessLogHandle = std::ofstream(ACCESS_LOG_FILE, std::ios_base::app);
    if (!accessLogHandle.is_open()) {
        std::cerr << "Failed to open Access Log File.\n";
        return CONF_FAILURE;
    }

    errorLogHandle = std::ofstream(ERROR_LOG_FILE, std::ios_base::app);
    if (!errorLogHandle.is_open()) {
        std::cerr << "Failed to open Error Log File.\n";
        return CONF_FAILURE;
    }

    // Return success
    return CONF_SUCCESS;
}

int loadMIMES() {
    using namespace conf;

    // Load MIMES
    std::ifstream mimeHandle(MIMES_FILE);
    if (!mimeHandle.is_open()) return CONF_FAILURE;

    std::string line, extBuf, mimeBuf;
    while (std::getline(mimeHandle, line)) {
        size_t spaceIndex = line.find(' ');
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
    for (conf::Match* pMatch : conf::matchConfigs)
        delete pMatch;

    conf::matchConfigs.clear();
}