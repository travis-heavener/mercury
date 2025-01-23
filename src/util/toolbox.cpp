#include "toolbox.hpp"

bool doesFileExist(const std::string& path, const bool forceInDocumentRoot) {
    const bool isFile = std::filesystem::is_regular_file(path);
    return isFile && (!forceInDocumentRoot || path.find(conf::DOCUMENT_ROOT.string()) == 0);
}

bool doesDirectoryExist(const std::string& path, const bool forceInDocumentRoot) {
    if (!forceInDocumentRoot)
        return std::filesystem::is_directory(path);
    else // Match document root at start of filename
        return std::filesystem::is_directory(path) && path.find(conf::DOCUMENT_ROOT.string()) == 0;
}

int loadErrorDoc(const int status, const std::string& title, std::string& buffer) {
    std::filesystem::path cwd = conf::CWD / "conf" / "html" / "err.html";

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

void formatFileSize(size_t fileSize, std::string& buffer) {
    long double truncatedSize;
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);

    if (fileSize >= 1e12) {
        truncatedSize = fileSize / 1e12f;
        ss << truncatedSize << " TB";
    } else if (fileSize >= 1e9) {
        truncatedSize = fileSize / 1e9f;
        ss << truncatedSize << " GB";
    } else if (fileSize >= 1e6) {
        truncatedSize = fileSize / 1e6f;
        ss << truncatedSize << " MB";
    } else if (fileSize >= 1e3) {
        truncatedSize = fileSize / 1e3f;
        ss << truncatedSize << " KB";
    } else {
        ss << fileSize << " B";
    }

    // Write buffer
    buffer = ss.str();
}

void formatDate(std::filesystem::file_time_type dur, std::string& buffer) {
    // Cast timestamp
    using namespace std::chrono;
    auto tp = system_clock::to_time_t(
        time_point_cast<system_clock::duration>(dur - std::filesystem::file_time_type::clock::now()
              + system_clock::now())
    );

    // Format date string
    std::stringstream ss;
    ss << std::put_time(std::localtime(&tp), "%m/%d/%y, %I:%M:%S %p");

    // Write to buffer
    buffer = ss.str();
}

int loadDirectoryListing(std::string& buffer, const std::string& path, const std::string& rawPath) {
    std::filesystem::path directoryListing = conf::CWD / "conf" / "html" / "dir_index.html";

    // Read directory listing file
    std::ifstream handle( directoryListing.string(), std::ios::binary );
    if (!handle.is_open()) return IO_FAILURE;

    // Read file to buffer
    std::stringstream sstream;
    sstream << handle.rdbuf();
    buffer = sstream.str();

    // Close file
    handle.close();

    // Gather data from desired path
    std::stringstream rowsBuffer;
    std::string fnameBuf, fsizeBufStr, modTsBufStr, hrefBuf;

    // Add parent path, if available
    if (rawPath != "/") {
        // Format href
        std::string rawPathBuf = rawPath;
        while (rawPathBuf.size() && rawPathBuf.back() == '/')
            rawPathBuf.pop_back();

        if (rawPathBuf.size()) {
            // Format href
            hrefBuf = std::filesystem::path(rawPathBuf).parent_path().string();
            hrefBuf += hrefBuf.back() != '/' ? "/" : ""; // Affix trailing fwd slash

            // Format timestamp
            modTsBufStr.clear();
            formatDate(std::filesystem::last_write_time(std::filesystem::path(path).parent_path()), modTsBufStr);
            rowsBuffer << "<tr> <td><a href=\"" << hrefBuf << "\">..</a></td> <td></td> <td>" << modTsBufStr << "</td> </tr>\n";
        }
    }

    // Add other file contents
    for (const auto& file : std::filesystem::directory_iterator(path)) {
        // Get file/directory name
        fnameBuf = file.path().filename().string();
        if (std::filesystem::is_directory(file))
            fnameBuf += '/';

        // Get file size
        fsizeBufStr.clear();
        if (!std::filesystem::is_directory(file))
            formatFileSize(file.file_size(), fsizeBufStr);

        // Format last modified timestamp & href
        modTsBufStr.clear();
        formatDate(std::filesystem::last_write_time(file), modTsBufStr);
        hrefBuf = (std::filesystem::path(rawPath) / fnameBuf).string();

        // Append to document
        rowsBuffer << "<tr> <td><a href=\"" << hrefBuf << "\">" << fnameBuf << "</a></td> <td>" << fsizeBufStr << "</td> <td>" << modTsBufStr << "</td> </tr>\n";
    }

    // Replace document
    stringReplaceAll(buffer, "%dirname%", rawPath);
    stringReplaceAll(buffer, "%files%", rowsBuffer.str());

    // Base case, return success
    return IO_SUCCESS;
}

// Debug profiling
long long debug_getTimestamp() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return ms.count();
}