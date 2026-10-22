#ifndef __FILE_TOOLS_HPP
#define __FILE_TOOLS_HPP

#include <ctime>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_set>

#define NOT_SYMLINK 0
#define IS_SYMLINK 1
#define FILE_NOT_EXIST 2
#define INTERNAL_ERROR 3

struct FileMetadata {
    bool exists = false;
    bool isDirectory = false;
    bool isRegularFile = false;
    size_t size = 0;
    std::time_t lastModTime = 0;
};

// Query file stat metadata in single syscall
bool queryFileMetadata(const std::string&, FileMetadata&);

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
int isSymlinked(const std::filesystem::path&, const bool = false);

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