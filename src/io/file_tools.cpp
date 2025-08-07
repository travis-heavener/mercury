#include "file_tools.hpp"

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
    const fs::path relativeToRoot = fs::relative(absolutePath, canonicalRoot);
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
        const std::wstring prefix = L"\\\\?\\";
        if (resolvedPathStr.compare(0, prefix.length(), prefix) == 0)
            resolvedPathStr = resolvedPathStr.substr(prefix.length());

        return std::filesystem::path(resolvedPathStr);
    #else
        // Linux passthru
        return std::filesystem::canonical(path);
    #endif
}