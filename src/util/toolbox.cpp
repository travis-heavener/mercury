#include "toolbox.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <string>

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include "string_tools.hpp"
#include "../conf/conf.hpp"
#include "../io/file_tools.hpp"

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
    std::tm localTime{};
    #ifdef _WIN32
        localtime_s(&localTime, &tp);
    #else
        localtime_r(&tp, &localTime);
    #endif

    char dateBuf[32];
    const size_t n = std::strftime(dateBuf, sizeof(dateBuf), "%m/%d/%y, %I:%M:%S %p", &localTime);
    buffer.assign(dateBuf, n);
}

// Used to load status or error document, attempting custom ones in document root
int loadErrorDoc(const int status, std::unique_ptr<http::IBodyStream>& pStream) {
    // Attempt to locate custom user-provided status document
    const std::string userFilename = std::to_string(status) + ".html";
    std::filesystem::path templatePath = conf::DOCUMENT_ROOT / userFilename;

    std::error_code ec;
    if (conf::ENABLE_CUSTOM_STATUS_DOCS && std::filesystem::is_regular_file(templatePath, ec)) {
        pStream = std::make_unique<http::FileStream>(templatePath.string(), false);
        return pStream->status() == STREAM_SUCCESS ? IO_SUCCESS : IO_FAILURE;
    }

    // Use default template path
    templatePath = conf::CWD / "conf/html/err.html";

    // Open file
    std::ifstream handle( templatePath.string(), std::ios::binary );
    if (!handle.is_open()) return IO_FAILURE;

    std::ostringstream out;

    // Read file to buffer
    const size_t bufferSize = 8192;
    std::string buffer(bufferSize, '\0');
    while (!handle.eof()) {
        handle.read(buffer.data(), bufferSize);
        const size_t bytesRead = handle.gcount();

        // Check for % escaped chars
        size_t offset = 0;
        do {
            if (offset >= bytesRead) break;

            size_t percIndex = buffer.find('%', offset);
            if (percIndex == std::string::npos) {
                out.write(buffer.data() + offset, bytesRead - offset);
                break;
            }

            // Write up to the %
            out.write(buffer.data() + offset, percIndex - offset);

            // Check for end of escaped sequence
            if (percIndex+1 < bytesRead) {
                // Check escaped char
                if (buffer[percIndex+1] == 'A')      out << getReasonFromStatus(status);
                else if (buffer[percIndex+1] == 'B') out << status;
                else if (buffer[percIndex+1] == 'C') out << conf::VERSION;
                else                                 out << '%' << buffer[percIndex+1];

                // Update the offset (skip % and letter)
                offset = percIndex + 2;
                continue;
            }

            // Otherwise, read next character
            char nextChar;
            handle.read(&nextChar, 1);

            // Break if nothing read
            if (handle.gcount() == 0) {
                out << '%';
                break;
            }

            // Check escaped char
            if (nextChar == 'A')      out << getReasonFromStatus(status);
            else if (nextChar == 'B') out << status;
            else if (nextChar == 'C') out << conf::VERSION;
            else                      out << '%' << nextChar;

            // End of chunk
            break;
        } while (true);
    }

    // Close handle
    handle.close();

    // Stream from memory
    pStream = std::make_unique<http::MemoryStream>(std::move(out).str());
    return IO_SUCCESS;
}

// Used to generate HTML directory listing for a directory path
int loadDirectoryListing(std::unique_ptr<http::IBodyStream>& pStream, const std::string& path, const std::string& rawPath) {
    const std::filesystem::path templatePath = conf::CWD / "conf/html/dir_index.html";

    // Open file
    std::ifstream handle( templatePath.string(), std::ios::binary );
    if (!handle.is_open()) return IO_FAILURE;

    std::ostringstream out;

    // Lambda to load all files to the output stream
    auto writeFiles = [&]() -> void {
        std::string fnameBuf, fsizeBufStr, modTsBufStr, hrefBuf;

        // Add parent path, if available
        if (rawPath != "/") {
            // Format href
            std::string rawPathBuf = rawPath;
            while (!rawPathBuf.empty() && rawPathBuf.back() == '/') rawPathBuf.pop_back();

            if (!rawPathBuf.empty()) {
                // Format href
                hrefBuf = std::filesystem::path(rawPathBuf).parent_path().generic_string();
                hrefBuf += hrefBuf.back() != '/' ? "/" : ""; // Affix trailing fwd slash

                // Format timestamp
                modTsBufStr.clear();
                std::error_code timeEc;
                formatDate(std::filesystem::last_write_time(std::filesystem::path(path).parent_path(), timeEc), modTsBufStr);
                out << "<tr> <td><a href=\"" << hrefBuf << "\">..</a></td> <td></td> <td>" << modTsBufStr << "</td> </tr>\n";
            }
        }

        // Add directories first
        std::error_code dirEc;
        for (const auto& file : std::filesystem::directory_iterator(path, dirEc)) {
            if (dirEc || !file.is_directory(dirEc)) continue;

            // Get file/directory name
            fnameBuf = file.path().filename().generic_string();
            fnameBuf += '/';

            // Get file size
            fsizeBufStr.clear();

            // Format last modified timestamp & href
            modTsBufStr.clear();
            std::error_code fileEc;
            formatDate(file.last_write_time(fileEc), modTsBufStr);
            hrefBuf = (std::filesystem::path(rawPath) / fnameBuf).generic_string();

            // Append to document
            out << "<tr> <td><a href=\"" << hrefBuf << "\">" << fnameBuf << "</a></td> <td>" << fsizeBufStr << "</td> <td>" << modTsBufStr << "</td> </tr>\n";
        }

        // Add other file contents
        for (const auto& file : std::filesystem::directory_iterator(path, dirEc)) {
            if (dirEc || file.is_directory(dirEc)) continue;

            // Get file/directory name
            fnameBuf = file.path().filename().generic_string();

            // Get file size
            fsizeBufStr.clear();
            std::error_code sizeEc;
            formatFileSize(file.file_size(sizeEc), fsizeBufStr);

            // Format last modified timestamp & href
            modTsBufStr.clear();
            std::error_code fileEc;
            formatDate(file.last_write_time(fileEc), modTsBufStr);
            hrefBuf = (std::filesystem::path(rawPath) / fnameBuf).generic_string();

            // Append to document
            out << "<tr> <td><a href=\"" << hrefBuf << "\">" << fnameBuf << "</a></td> <td>" << fsizeBufStr << "</td> <td>" << modTsBufStr << "</td> </tr>\n";
        }
    };

    // Read file to buffer
    const size_t bufferSize = 8192;
    std::string buffer(bufferSize, '\0');
    while (!handle.eof()) {
        handle.read(buffer.data(), bufferSize);
        const size_t bytesRead = handle.gcount();

        // Check for % escaped chars
        size_t offset = 0;
        do {
            if (offset >= bytesRead) break;

            size_t percIndex = buffer.find('%', offset);
            if (percIndex == std::string::npos) {
                out.write(buffer.data() + offset, bytesRead - offset);
                break;
            }

            // Write up to the %
            out.write(buffer.data() + offset, percIndex - offset);

            // Check for end of escaped sequence
            if (percIndex+1 < bytesRead) {
                // Check escaped char
                if (buffer[percIndex+1] == 'A')      out << rawPath;
                else if (buffer[percIndex+1] == 'B') out << conf::VERSION;
                else if (buffer[percIndex+1] == 'C') writeFiles(); // Load each file
                else                                 out << '%' << buffer[percIndex+1];

                // Update the offset (skip % and letter)
                offset = percIndex + 2;
                continue;
            }

            // Otherwise, read next character
            char nextChar;
            handle.read(&nextChar, 1);

            // Break if nothing read
            if (handle.gcount() == 0) {
                out << '%';
                break;
            }

            // Check escaped char
            if (nextChar == 'A')      out << rawPath;
            else if (nextChar == 'B') out << conf::VERSION;
            else if (nextChar == 'C') writeFiles(); // Load each file
            else                      out << '%' << nextChar;

            // End of chunk
            break;
        } while (true);
    }

    // Close handle
    handle.close();

    // Stream from memory
    pStream = std::make_unique<http::MemoryStream>(std::move(out).str());
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
    std::tm gmtTime{};
    #ifdef _WIN32
        gmtime_s(&gmtTime, &time);
    #else
        gmtime_r(&time, &gmtTime);
    #endif

    char gmtBuf[32];
    const size_t n = std::strftime(gmtBuf, sizeof(gmtBuf), "%a, %d %b %Y %H:%M:%S GMT", &gmtTime);
    return std::string(gmtBuf, n);
}

std::string getCurrentGMTString() {
    std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    // Format as GMT string
    std::tm gmtTime{};
    #ifdef _WIN32
        gmtime_s(&gmtTime, &time);
    #else
        gmtime_r(&time, &gmtTime);
    #endif

    char gmtBuf[32];
    const size_t n = std::strftime(gmtBuf, sizeof(gmtBuf), "%a, %d %b %Y %H:%M:%S GMT", &gmtTime);
    return std::string(gmtBuf, n);
}

const char* getReasonFromStatus(uint16_t code) {
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