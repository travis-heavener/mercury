#ifndef __FILE_CACHE_HPP
#define __FILE_CACHE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "file_tools.hpp"

class File;

namespace io {

    /**********************************************************/
    /********************* DATA STRUCTURES ********************/
    /**********************************************************/

    // Retains cached static file metadata and optional in-memory body
    struct CachedFile {
        std::string uri; // Request URI key
        std::string absoluteResourcePath; // Resolved absolute filesystem path
        std::string decodedURIWithoutPathInfo; // Canonical decoded URI stripped of PATH_INFO
        std::string queryString; // Preserved query parameters
        std::string MIME; // Cached MIME content type
        std::string lastModGMT; // Formatted RFC 7231 Last-Modified timestamp
        size_t fileSize = 0; // File payload size in bytes
        std::time_t lastModTime = 0; // POSIX modification timestamp in seconds
        uint64_t lastModTimeNsec = 0; // Nanosecond component of modification timestamp
        uint32_t mode = 0; // File permission bitmask
        uint64_t inode = 0; // Filesystem inode or Windows file index
        bool exists = false; // Filesystem existence flag
        bool isLinked = false; // Symlink indicator
        bool isDirectory = false; // Directory indicator
        bool ioFailure = false; // File access or query failure flag
        bool hasModTime = false; // Modification timestamp availability flag
        std::shared_ptr<const std::string> pContent; // Immutable shared buffer for in-memory file payload
        std::atomic<std::chrono::steady_clock::time_point> lastValidated{}; // Monotonic clock timestamp of last stat check
        std::atomic<uint32_t> accessCount{0}; // Access frequency counter for LFU eviction
    };

    /**********************************************************/
    /********************* CACHE MANAGER **********************/
    /**********************************************************/

    // Thread-safe in-memory cache for static files and metadata
    class FileCache {
        public:
            // Used to retrieve global singleton instance
            static FileCache& getInstance();

            // Used to populate File from cache if entry exists and passes freshness check
            bool tryGet(const std::string& uri, File& outFile);

            // Used to insert or refresh cached file entry w/ optional in-memory payload
            void put(File& file, const std::string& uri);

            // Used to evict all cached entries on server shutdown
            void clear();

        private:
            FileCache() = default;
            ~FileCache() = default;
            FileCache(const FileCache&) = delete;
            FileCache& operator=(const FileCache&) = delete;

            mutable std::shared_mutex mutex; // Protects entries map and totalContentBytes
            std::unordered_map<std::string, std::shared_ptr<CachedFile>> entries; // URI to cached file record map
            size_t totalContentBytes = 0; // Aggregate payload bytes currently held in memory
    };

}

#endif
