#include "toolbox.hpp"

bool doesFileExist(const std::string& path, const bool forceInDocumentRoot) {
    if (!forceInDocumentRoot)
        return std::filesystem::exists(path);
    else // Match document root at start of filename
        return std::filesystem::exists(path) && path.find(conf::DOCUMENT_ROOT.string()) == 0;
}

int loadErrorDoc(const int status, const std::string& title, std::string& buffer) {
    std::filesystem::path cwd = std::filesystem::current_path() / "conf" / "err.html";

    // Open file
    std::ifstream handle( cwd.string(), std::ios::binary );
    if (!handle.is_open()) return IO_FAILURE;

    // Read file to buffer
    std::stringstream sstream;
    sstream << handle.rdbuf();
    buffer = sstream.str();

    // Close file
    handle.close();

    // Replace status code & descriptor
    stringReplaceAll(buffer, "%title%", title);
    stringReplaceAll(buffer, "%status%", std::to_string(status));

    return IO_SUCCESS;
}

int loadConfHeaders(std::unordered_map<std::string, std::string>& buffer) {
    // Load preset headers
    buffer.insert({"CONNECTION", "close"});
    buffer.insert({"SERVER", VERSION});

    // Base case, return success
    return IO_SUCCESS;
}