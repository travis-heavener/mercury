#ifndef __FILE_TOOLS_HPP
#define __FILE_TOOLS_HPP

#include <shared_mutex>

#include "../pch/common.hpp"

#define NOT_SYMLINK 0
#define IS_SYMLINK 1
#define FILE_NOT_EXIST 2
#define INTERNAL_ERROR 3

// Windows-only, checks for UNC path after canonicalization
// On Linux, returns false always
inline bool isUNCPath(const std::filesystem::path& path) {
    #ifdef _WIN32
        return path.wstring().find(L"\\\\") == 0;
    #else
        (void)path;
        return false;
    #endif
}

void normalizeBackslashes(std::string& path);

bool doesFileExist(const std::string&, const bool);
bool doesDirectoryExist(const std::string&, const bool);
int isSymlinked(const std::filesystem::path&);

std::filesystem::path resolveCanonicalPath(const std::filesystem::path& path);

// Creates the immediate directory for log files if missing, will silently fail
void createLogDirectoryIfMissing(const std::filesystem::path& path);

// Temp file managers
extern std::unordered_set<std::string> currentTempFiles;
extern std::shared_mutex tempFileSetMutex;

// Creates and returns the full, absolute path to a new tmp file, returning true if successful
bool createTempFile(std::string& outPath);

// Attempts to remove the temp file, returns true if successful
bool removeTempFile(const std::string& tmpPath);

#endif