#include "file_tools.hpp"

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include <algorithm>
#include <random>
#include <sys/stat.h>

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"
#include "../util/privilege.hpp"

#define TMP_FILE_MAX_RETRIES 50
#define TMP_FILE_NAME_LEN 12

void normalizeBackslashes(std::string& path) {
    if (isUNCPath(path) && path.size() > 2)
        std::replace( path.begin() + 2, path.end(), '\\', '/' );
    else
        std::replace( path.begin(), path.end(), '\\', '/' );
}

bool queryFileMetadata(const std::string& path, FileMetadata& outMeta) {
    if (path.empty()) return false;

    #ifdef _WIN32
        std::string winPath = path;
        normalizeBackslashes(winPath);

        // Strip trailing slashes unless drive root (e.g. "C:/" or "/")
        while (winPath.size() > 1 && winPath.back() == '/') {
            if (winPath.size() == 3 && winPath[1] == ':') break;
            winPath.pop_back();
        }

        struct _stat64 st;
        if (_wstat64(std::filesystem::path(winPath).wstring().c_str(), &st) != 0) return false;
        outMeta.exists = true;
        outMeta.isDirectory = (st.st_mode & _S_IFDIR) != 0;
        outMeta.isRegularFile = (st.st_mode & _S_IFREG) != 0;
        outMeta.size = static_cast<size_t>(st.st_size);
        outMeta.lastModTime = st.st_mtime;
        return true;
    #else
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return false;
        outMeta.exists = true;
        outMeta.isDirectory = S_ISDIR(st.st_mode);
        outMeta.isRegularFile = S_ISREG(st.st_mode);
        outMeta.size = static_cast<size_t>(st.st_size);
        outMeta.lastModTime = st.st_mtime;
        return true;
    #endif
}

bool doesFileExist(const std::string& path, const bool forceInDocumentRoot) {
    FileMetadata meta;
    if (!queryFileMetadata(path, meta) || !meta.isRegularFile) return false;
    if (!forceInDocumentRoot) return true;
    std::string docRootStr = conf::DOCUMENT_ROOT.string();
    normalizeBackslashes(docRootStr);
    std::string normPath = path;
    normalizeBackslashes(normPath);
    return normPath.find(docRootStr) == 0;
}

bool doesDirectoryExist(const std::string& path, const bool forceInDocumentRoot) {
    FileMetadata meta;
    if (!queryFileMetadata(path, meta) || !meta.isDirectory) return false;
    if (!forceInDocumentRoot) return true;
    std::string docRootStr = conf::DOCUMENT_ROOT.string();
    normalizeBackslashes(docRootStr);
    std::string normPath = path;
    normalizeBackslashes(normPath);
    return normPath.find(docRootStr) == 0;
}

int isSymlinked(const std::filesystem::path& path, const bool isAlreadyCanonical) {
    namespace fs = std::filesystem; // Make my life EASIER

    // Resolve canonical paths
    const fs::path canonicalRoot = conf::DOCUMENT_ROOT;
    fs::path absolutePath;
    if (isAlreadyCanonical) {
        absolutePath = path;
    } else {
        try {
            if (!tryResolveCanonicalPath(path, absolutePath)) return FILE_NOT_EXIST;
        } catch (std::filesystem::filesystem_error&) {
            return FILE_NOT_EXIST; // Invalid path, handles as 404
        } catch (std::runtime_error&) {
            return INTERNAL_ERROR; // Manually thrown by IO failure
        }
    }

    // Verify canonical path is within document root (could escape via symlink)
    std::string rootStr = canonicalRoot.string();
    std::string absStr = absolutePath.string();
    normalizeBackslashes( rootStr );
    normalizeBackslashes( absStr );

    if (rootStr.back() == '/') rootStr.pop_back();
    if (absStr.find(rootStr) != 0)
        return IS_SYMLINK;

    // Walk from document root to path, checking for symlinks & reparse points
    const fs::path relativeToRoot = absolutePath.lexically_relative(canonicalRoot);
    fs::path currentParts = canonicalRoot;
    for (const auto& part : relativeToRoot) {
        // Check if individual part is a symlink
        currentParts /= part;
        #ifdef _WIN32
            const DWORD attrs = GetFileAttributesW(currentParts.wstring().c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
                return IS_SYMLINK;
        #else
            if (fs::is_symlink(fs::symlink_status(currentParts)))
                return IS_SYMLINK;
        #endif
    }

    // Base case, this is NOT a symlink
    return NOT_SYMLINK;
}

std::filesystem::path resolveCanonicalPath(const std::filesystem::path& path) {
    std::filesystem::path resolved;
    if (!tryResolveCanonicalPath(path, resolved))
        throw std::filesystem::filesystem_error("File does not exist", path,
            std::make_error_code(std::errc::no_such_file_or_directory));
    return resolved;
}

bool tryResolveCanonicalPath(const std::filesystem::path& path, std::filesystem::path& resolved) {
    // Note: ChatGPT helped me a bit with this part because WinAPI sucks :p
    // and I'm doing this to learn socket programming, NOT the Windows API

    #ifdef _WIN32
        // Return expected misses without raising debugger-visible exceptions
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory)
                throw std::filesystem::filesystem_error("Failed to inspect path", path, ec);
            return false;
        }

        // Get fully-qualified path
        std::wstring pathStr = path.wstring();
        wchar_t fullPathStr[MAX_PATH];
        DWORD result = GetFullPathNameW(pathStr.c_str(), MAX_PATH, fullPathStr, nullptr);
        if (result == 0 || result >= MAX_PATH)
            throw std::runtime_error("Failed to get full path name");

        // Open the file to get a handle
        HANDLE handle = CreateFileW(
            fullPathStr,
            0, // No access needed
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, // To open directories
            nullptr
        );

        if (handle == INVALID_HANDLE_VALUE) {
            // Handle removal between existence check and opening file
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
            throw std::runtime_error("Failed to open file handle");
        }

        // Get final path name from file handle
        wchar_t finalPathStr[MAX_PATH];
        result = GetFinalPathNameByHandleW(handle, finalPathStr, MAX_PATH, FILE_NAME_NORMALIZED);
        CloseHandle(handle);

        if (result == 0 || result >= MAX_PATH)
            throw std::runtime_error("Failed to get final path name");

        // Remove \\?\ prefix if present
        std::wstring resolvedPathStr(finalPathStr);
        std::wstring prefix = L"\\\\?\\";
        if (resolvedPathStr.compare(0, prefix.length(), prefix) == 0)
            resolvedPathStr = resolvedPathStr.substr(prefix.length());

        // Remove UNC prefix if present
        prefix = L"UNC";
        if (resolvedPathStr.compare(0, prefix.length(), prefix) == 0)
            resolvedPathStr = L"\\" + resolvedPathStr.substr(prefix.length());

        resolved = std::filesystem::path(resolvedPathStr);
        return true;
    #else
        // Distinguish routine misses from unexpected filesystem failures
        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::canonical(path, ec);
        if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory)
            return false;
        if (ec) throw std::filesystem::filesystem_error("Failed to resolve path", path, ec);
        resolved = std::move(canonical);
        return true;
    #endif
}

// Creates the immediate directory for log files if missing, will silently fail
void createLogDirectoryIfMissing(const std::filesystem::path& path) {
    // Check if the full path exists
    const std::filesystem::path parentPath = path.parent_path();
    if (std::filesystem::exists( parentPath )) {
        util::privilege::applyOwnershipAndPermissions(parentPath, 0755);
        return;
    }

    // Parent path does not exist, check if it's parent path does
    const std::filesystem::path grandparentPath = parentPath.parent_path();
    if (!std::filesystem::exists( grandparentPath )) return;

    // Parent does not exist, but grandparent does
    try {
        std::filesystem::create_directory( parentPath );
        util::privilege::applyOwnershipAndPermissions(parentPath, 0755);
    } catch (...) {
        // Silently fail
        return;
    }
}

// Used to generate random string for temporary file names
static const char randomCharset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
void genRandomString(std::string& result, size_t length) {
    // Thread-local random generator avoids re-opening /dev/urandom on every call
    thread_local std::mt19937 gen(std::random_device{}());
    thread_local std::uniform_int_distribution<size_t> dist(0, sizeof(randomCharset) - 2);

    result.resize(length);
    for (size_t i = 0; i < length; ++i)
        result[i] = randomCharset[dist(gen)];
}

// Temp file managers
std::unordered_set<std::string> currentTempFiles;
std::shared_mutex tempFileSetMutex;

// Creates and returns the full, absolute path to a new tmp file, returning true if successful
bool createTempFile(std::string& outPath) {
    std::string buffer;
    std::error_code ec;

    int i = 0;
    std::unique_lock lock(tempFileSetMutex);
    do {
        // Determine temp file name
        genRandomString(buffer, TMP_FILE_NAME_LEN);
        outPath = (conf::TMP_PATH / buffer).string();
    } while (++i < TMP_FILE_MAX_RETRIES && (currentTempFiles.find(outPath) != currentTempFiles.end() || std::filesystem::exists(outPath, ec)));

    // Return true if filename is available
    if (currentTempFiles.find(outPath) == currentTempFiles.end() && !std::filesystem::exists(outPath, ec)) {
        currentTempFiles.insert(outPath);
        return true;
    }

    ERROR_LOG << "Failed to create temp file: " << outPath << std::endl;
    return false;
}

// Attempts to remove the temp file, returns true if successful
bool removeTempFile(const std::string& tmpPath) {
    // Always untrack path from currentTempFiles, even if filesystem remove fails
    {
        std::unique_lock lock(tempFileSetMutex);
        currentTempFiles.erase(tmpPath);
    }

    std::error_code ec;
    std::filesystem::remove(tmpPath, ec);
    if (ec) {
        ERROR_LOG << "Failed to remove temp file: " << tmpPath << ", error: " << ec.message() << std::endl;
        return false;
    }
    return true;
}

#undef TMP_FILE_MAX_RETRIES
#undef TMP_FILE_NAME_LEN