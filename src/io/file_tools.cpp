#include "file_tools.hpp"

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

#include <random>

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"

#define TMP_FILE_MAX_RETRIES 50
#define TMP_FILE_NAME_LEN 12

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

int isSymlinked(const std::filesystem::path& path) {
    namespace fs = std::filesystem; // Make my life EASIER

    // Resolve canonical paths
    const fs::path canonicalRoot = conf::DOCUMENT_ROOT;
    fs::path absolutePath;
    try {
        absolutePath = resolveCanonicalPath(path);
    } catch (std::filesystem::filesystem_error&) {
        return FILE_NOT_EXIST; // Invalid path, handles as 404
    } catch (std::runtime_error&) {
        return INTERNAL_ERROR; // Manually thrown by IO failure
    }

    // Verify canonical path is within document root (could escape via symlink)
    std::string rootStr = canonicalRoot.string();
    std::string absStr = absolutePath.string();
    std::replace( rootStr.begin(), rootStr.end(), '\\', '/' );
    std::replace( absStr.begin(), absStr.end(), '\\', '/' );

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
    // Note: ChatGPT helped me a bit with this part because WinAPI sucks :p
    // and I'm doing this to learn socket programming, NOT the Windows API

    #ifdef _WIN32
        // Check if file exists
        if (!std::filesystem::exists(path))
            throw std::filesystem::filesystem_error("File does not exist", path, std::error_code());

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

        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Failed to open file handle");

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

        return std::filesystem::path(resolvedPathStr);
    #else
        // Linux passthru
        return std::filesystem::canonical(path);
    #endif
}

// Creates the immediate directory for log files if missing, will silently fail
void createLogDirectoryIfMissing(const std::filesystem::path& path) {
    // Check if the full path exists
    const std::filesystem::path parentPath = path.parent_path();
    if (std::filesystem::exists( parentPath )) return;

    // Parent path does not exist, check if it's parent path does
    const std::filesystem::path grandparentPath = parentPath.parent_path();
    if (!std::filesystem::exists( grandparentPath )) return;

    // Parent does not exist, but grandparent does
    try {
        std::filesystem::create_directory( parentPath );
    } catch (...) {
        // Silently fail
        return;
    }
}

// Generates a random string (helper for createTempFile)
static const char randomCharset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
void genRandomString(std::string& result, size_t length) {
    // Create randomizer
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(randomCharset) - 2); // Exclude null byte

    // Place random characters
    for (size_t i = 0; i < length; ++i)
        result += randomCharset[dist(gen)];
}

// Creates and returns the full, absolute path to a new tmp file, returning true if successful
bool createTempFile(std::string& outPath) {
    std::string buffer;
    buffer.reserve(TMP_FILE_NAME_LEN);

    int i = 0;
    do {
        // Determine temp file name
        genRandomString(buffer, TMP_FILE_NAME_LEN);
        outPath = (conf::TMP_PATH / buffer).string();
    } while (++i < TMP_FILE_MAX_RETRIES && std::filesystem::exists(outPath));

    // Return true if the filename is available
    return !std::filesystem::exists(outPath);
}

// Attempts to remove the temp file, returns true if successful
bool removeTempFile(const std::string& tmpPath) {
    try { // Attempt to remove
        std::filesystem::remove(tmpPath);
    } catch (...) {
        ERROR_LOG << "Failed to remove temp file: " << tmpPath << std::endl;
        return false;
    }
    return true;
}

#undef TMP_FILE_MAX_RETRIES
#undef TMP_FILE_NAME_LEN