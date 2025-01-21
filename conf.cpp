#include "conf.hpp"

/****** EXTERNAL FIELDS ******/

std::filesystem::path DOCUMENT_ROOT;
unsigned short PORT;

std::unordered_map<std::string, std::string> MIMES;

/*****************************/

// Forward dec
int loadMIMES();

int loadConfig() {
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
    if (!root) {
        std::cerr << "Failed to parse config file, missing DocumentRoot node.\n";
        return CONF_FAILURE;
    }

    std::string documentRootStr( documentRootNode.text().as_string() );
    trimString(documentRootStr);

    if (documentRootStr.find("./") == 0) // Relative paths
        DOCUMENT_ROOT = std::filesystem::current_path() / documentRootStr.substr(2);
    else // Absolute paths
        DOCUMENT_ROOT = documentRootStr;

    if (!std::filesystem::exists(DOCUMENT_ROOT) || !std::filesystem::is_directory(DOCUMENT_ROOT)) {
        std::cerr << "Failed to parse config file, DocumentRoot points to invalid/nonexistant directory.\n";
        return CONF_FAILURE;
    }

    /************************** Extract Port **************************/
    pugi::xml_node portNode = root.child("Port");
    if (!root) {
        std::cerr << "Failed to parse config file, missing Port node.\n";
        return CONF_FAILURE;
    }

    PORT = portNode.text().as_uint();

    // Load known MIMES
    if (loadMIMES() == CONF_FAILURE)
        return CONF_FAILURE;

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
        extBuf = line.substr(0, spaceIndex);
        mimeBuf = line.substr(spaceIndex+1);
        trimString(extBuf);
        trimString(mimeBuf);
        MIMES.insert({extBuf, mimeBuf});
    }

    mimeHandle.close();

    return CONF_SUCCESS;
}

void trimString(std::string& str) {
    while (str.size() && std::isspace(str.back()))
        str.pop_back();

    while (str.size() && std::isspace(str[0]))
        str = str.substr(1);
}