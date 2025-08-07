#include "file_tools.hpp"

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