#include "conf.hpp"

/****** EXTERNAL FIELDS ******/

namespace conf {

    std::filesystem::path DOCUMENT_ROOT;
    std::string HOST;
    port_t PORT;
    std::vector<conf::Match*> matchConfigs;

    std::unordered_map<std::string, std::string> MIMES;

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

    if (documentRootStr.find("./") == 0) // Relative paths
        DOCUMENT_ROOT = std::filesystem::current_path() / documentRootStr.substr(2);
    else // Absolute paths
        DOCUMENT_ROOT = documentRootStr;

    // Affix trailing slash
    if (DOCUMENT_ROOT.string().size() && DOCUMENT_ROOT.string().back() != '/')
        DOCUMENT_ROOT = DOCUMENT_ROOT.string() + '/';

    if (!DOCUMENT_ROOT.string().size() || !std::filesystem::exists(DOCUMENT_ROOT) || !std::filesystem::is_directory(DOCUMENT_ROOT)) {
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

    /************************** Extract DocumentRoot **************************/
    pugi::xml_node hostNode = root.child("Host");
    if (!hostNode) {
        std::cerr << "Failed to parse config file, missing Host node.\n";
        return CONF_FAILURE;
    }

    HOST = hostNode.text().as_string();
    trimString(HOST);

    // Load all Matches
    pugi::xml_object_range matchNodes = root.children("Match");

    for (pugi::xml_node& match : matchNodes) {
        // Parse match
        Match* pMatch = loadMatch(match);
        if (pMatch == nullptr) return CONF_FAILURE;
        matchConfigs.push_back(pMatch);
    }

    /************************** Load known MIMES **************************/
    if (loadMIMES() == CONF_FAILURE)
        return CONF_FAILURE;

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