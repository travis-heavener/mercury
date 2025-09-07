#ifndef __FILE_TOOLS_HPP
#define __FILE_TOOLS_HPP

#include <random>

#include "../pch/common.hpp"
#include "../logs/logger.hpp"

#ifdef _WIN32
    #include "../winheader.hpp"
#endif

// Including conf.hpp technically is a cyclical header but it doesn't introduce any errors
// Needed for conf::DOCUMENT_ROOT
#include "../conf/conf.hpp"

#define NOT_SYMLINK 0
#define IS_SYMLINK 1
#define FILE_NOT_EXIST 2
#define INTERNAL_ERROR 3

bool doesFileExist(const std::string&, const bool);
bool doesDirectoryExist(const std::string&, const bool);
int isSymlinked(const std::filesystem::path&);

std::filesystem::path resolveCanonicalPath(const std::filesystem::path& path);

// Creates the immediate directory for log files if missing, will silently fail
void createLogDirectoryIfMissing(const std::filesystem::path& path);

// Creates and returns the full, absolute path to a new tmp file, returning true if successful
bool createTempFile(std::string& outPath);

// Attempts to remove the temp file, returns true if successful
bool removeTempFile(const std::string& tmpPath);

#endif