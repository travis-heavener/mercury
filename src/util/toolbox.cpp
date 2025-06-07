#include "toolbox.hpp"

bool doesFileExist(const std::string& path, const bool forceInDocumentRoot) {
    const bool isFile = std::filesystem::is_regular_file(path);
    std::string docRootStr = conf::DOCUMENT_ROOT.string();
    std::replace( docRootStr.begin(), docRootStr.end(), '\\', '/' );
    return isFile && (!forceInDocumentRoot || path.find(docRootStr) == 0);
}

bool doesDirectoryExist(const std::string& path, const bool forceInDocumentRoot) {
    if (!forceInDocumentRoot) {
        return std::filesystem::is_directory(path);
    } else { // Match document root at start of filename
        std::string docRootStr = conf::DOCUMENT_ROOT.string();
        std::replace( docRootStr.begin(), docRootStr.end(), '\\', '/' );
        return std::filesystem::is_directory(path) && path.find(docRootStr) == 0;
    }
}

int loadErrorDoc(const int status, std::string& buffer) {
    std::filesystem::path cwd = conf::CWD / "conf" / "html" / "err.html";

    // Open file
    std::ifstream handle( cwd.string(), std::ios::binary | std::ios::ate );
    if (!handle.is_open()) return IO_FAILURE;

    // Read file to buffer
    std::streamsize size = handle.tellg();
    handle.seekg(0, std::ios::beg);
    buffer = std::string(size, '\0');
    handle.read(buffer.data(), size);

    // Close file
    handle.close();

    // Replace status code & descriptor
    stringReplaceAll(buffer, "%title%", getReasonFromStatusCode(status));
    stringReplaceAll(buffer, "%status%", std::to_string(status));

    return IO_SUCCESS;
}

int loadConfHeaders(std::unordered_map<std::string, std::string>& buffer) {
    // Load preset headers
    buffer.insert({"SERVER", conf::VERSION});
    buffer.insert({"DATE", getCurrentGMTString()});

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
    std::ifstream handle( directoryListing.string(), std::ios::binary | std::ios::ate );
    if (!handle.is_open()) return IO_FAILURE;

    // Read file to buffer
    std::streamsize size = handle.tellg();
    handle.seekg(0, std::ios::beg);
    buffer = std::string(size, '\0');
    handle.read(buffer.data(), size);

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

std::time_t getTimeTFromGMT(const std::string& gmtString) {
    std::tm tm = {};
    #if __linux__
        if (strptime(gmtString.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm) == nullptr)
            return -1;
        return timegm(&tm);
    #else
        std::istringstream ss(gmtString);
        ss.imbue(std::locale("C"));
        ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");

        if (ss.fail())
            return -1;

        return _mkgmtime(&tm);
    #endif
}

std::time_t getFileModTimeT(const std::string& filePath) {
    // Get the last write time
    auto lastWriteTime = std::filesystem::last_write_time(std::filesystem::path(filePath));
    auto systemTime = std::chrono::system_clock::now() + (lastWriteTime - std::filesystem::file_time_type::clock::now());
    return std::chrono::system_clock::to_time_t(systemTime);
}

std::string getFileModGMTString(const std::string& filePath) {
    std::time_t time = getFileModTimeT(filePath);

    // Format as GMT string
    std::tm* gmtTime = std::gmtime(&time);
    std::stringstream ss;
    ss << std::put_time(gmtTime, "%a, %d %b %Y %H:%M:%S GMT");
    return ss.str();
}

std::string getCurrentGMTString() {
    std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    // Format as GMT string
    std::tm* gmtTime = std::gmtime(&time);
    std::stringstream ss;
    ss << std::put_time(gmtTime, "%a, %d %b %Y %H:%M:%S GMT");
    return ss.str();
}

std::string getReasonFromStatusCode(uint16_t code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 207: return "Multi-Status";
        case 208: return "Already Reported";
        case 226: return "IM Used";

        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";

        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Content Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 418: return "I'm a teapot";
        case 421: return "Misdirected Request";
        case 422: return "Unprocessable Content";
        case 423: return "Locked";
        case 424: return "Failed Dependency";
        case 426: return "Upgrade Required";
        case 428: return "Precondition Required";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 451: return "Unavailable For Legal Reasons";

        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        case 506: return "Variant Also Negotiates";
        case 507: return "Insufficient Storage";
        case 508: return "Loop Detected";
        case 510: return "Not Extended";
        case 511: return "Network Authentication Required";

        default: return "Unknown";
    }
}

// Debug profiling
long long debug_getTimestamp() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return ms.count();
}