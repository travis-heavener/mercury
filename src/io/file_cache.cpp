#include "file_cache.hpp"

#include <fstream>

#include "../conf/conf.hpp"

#include "file.hpp"

namespace io {

    /**********************************************************/
    /******************** LIFECYCLE METHODS *******************/
    /**********************************************************/

    // Used to retrieve global singleton instance
    FileCache& FileCache::getInstance() {
        static FileCache instance;
        return instance;
    }

    /**********************************************************/
    /********************* CACHE ACCESSORS ********************/
    /**********************************************************/

    // Used to populate File from cache if entry exists and passes freshness check
    bool FileCache::tryGet(const std::string& uri, File& outFile) {
        // Acquire shared read lock for fast cache lookup
        std::shared_lock<std::shared_mutex> readLock(this->mutex);
        const auto it = this->entries.find(uri);
        if (it == this->entries.end()) return false;

        std::shared_ptr<CachedFile> pEntry = it->second;
        if (!pEntry) return false;

        // Check entry age against revalidation interval
        const auto now = std::chrono::steady_clock::now();
        const auto lastVal = pEntry->lastValidated.load(std::memory_order_relaxed);
        const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVal).count();

        if (ageMs >= static_cast<int64_t>(conf::FILE_CACHE_REVALIDATION_INTERVAL)) {
            // Drop shared read lock before performing blocking filesystem stat check
            readLock.unlock();

            // Recheck freshness in case another thread already revalidated entry
            const auto recheckVal = pEntry->lastValidated.load(std::memory_order_relaxed);
            const auto recheckAgeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - recheckVal).count();
            if (recheckAgeMs >= static_cast<int64_t>(conf::FILE_CACHE_REVALIDATION_INTERVAL)) {
                // Query filesystem metadata for freshness verification
                FileMetadata meta;
                if (!queryFileMetadata(pEntry->absoluteResourcePath, meta) ||
                    !meta.exists ||
                    !meta.isRegularFile ||
                    meta.lastModTime != pEntry->lastModTime ||
                    meta.lastModTimeNsec != pEntry->lastModTimeNsec ||
                    meta.size != pEntry->fileSize ||
                    meta.mode != pEntry->mode ||
                    (meta.inode != 0 && meta.inode != pEntry->inode)) {
                    // Invalidate stale or modified cache entry under exclusive write lock
                    std::unique_lock<std::shared_mutex> writeLock(this->mutex);
                    const auto delIt = this->entries.find(uri);
                    if (delIt != this->entries.end() && delIt->second == pEntry) {
                        if (pEntry->pContent) {
                            const size_t sz = pEntry->pContent->size();
                            this->totalContentBytes = (this->totalContentBytes >= sz) ? (this->totalContentBytes - sz) : 0;
                        }
                        this->entries.erase(delIt);
                    }
                    return false;
                }

                // Update revalidation timestamp on verified fresh entry
                pEntry->lastValidated.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
            }
        }

        // Record cache hit for LFU tracking
        ++pEntry->accessCount;

        // Populate caller file attributes from cached record
        outFile.exists = pEntry->exists;
        outFile.isLinked = pEntry->isLinked;
        outFile.isDirectory = pEntry->isDirectory;
        outFile.ioFailure = pEntry->ioFailure;
        outFile.fileSize = pEntry->fileSize;
        outFile.MIME = pEntry->MIME;
        outFile.absoluteResourcePath = pEntry->absoluteResourcePath;
        outFile.decodedURIWithoutPathInfo = pEntry->decodedURIWithoutPathInfo;
        outFile.queryString = pEntry->queryString;
        outFile.phpPathInfo = "";
        outFile.hasModTime = pEntry->hasModTime;
        outFile.lastModTime = pEntry->lastModTime;
        outFile.lastModTimeNsec = pEntry->lastModTimeNsec;
        outFile.lastModGMT = pEntry->lastModGMT;
        outFile.pCachedContent = pEntry->pContent;

        return true;
    }

    // Used to insert or refresh cached file entry w/ optional in-memory payload
    void FileCache::put(File& file, const std::string& uri) {
        // Bypass cache for non-existent paths, symlinks, directories, or failed IO
        if (!file.exists || file.isLinked || file.isDirectory || file.ioFailure) return;

        // Bypass cache if caching disabled by zero capacity ceiling
        if (conf::MAX_FILE_CACHE_ENTRIES == 0) return;

        // Query filesystem metadata to capture permissions and inode for stale detection
        FileMetadata meta;
        if (!queryFileMetadata(file.absoluteResourcePath, meta) || !meta.exists || !meta.isRegularFile)
            return;

        // Read file payload into memory if within single-file cache threshold
        std::shared_ptr<const std::string> pContent;
        if (conf::MAX_FILE_CACHE_TOTAL_SIZE > 0 && file.fileSize <= conf::MAX_CACHEABLE_FILE_SIZE) {
            std::ifstream in(file.absoluteResourcePath, std::ios::binary);
            if (in.is_open()) {
                std::string content;
                content.resize(file.fileSize);
                in.read(content.data(), static_cast<std::streamsize>(file.fileSize));
                if (in.gcount() == static_cast<std::streamsize>(file.fileSize)) {
                    pContent = std::make_shared<const std::string>(std::move(content));
                }
            }
        }

        // Allocate cached file descriptor
        auto pEntry = std::make_shared<CachedFile>();
        pEntry->uri = uri;
        pEntry->absoluteResourcePath = file.absoluteResourcePath;
        pEntry->decodedURIWithoutPathInfo = file.decodedURIWithoutPathInfo;
        pEntry->queryString = file.queryString;
        pEntry->MIME = file.MIME;
        pEntry->lastModGMT = file.getLastModifiedGMT();
        pEntry->fileSize = file.fileSize;
        pEntry->lastModTime = file.getLastModTime();
        pEntry->lastModTimeNsec = file.getLastModTimeNsec();
        pEntry->mode = meta.mode;
        pEntry->inode = meta.inode;
        pEntry->exists = file.exists;
        pEntry->isLinked = file.isLinked;
        pEntry->isDirectory = file.isDirectory;
        pEntry->ioFailure = file.ioFailure;
        pEntry->hasModTime = file.hasLastModTime();
        pEntry->pContent = pContent;
        pEntry->lastValidated.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
        pEntry->accessCount.store(1, std::memory_order_relaxed);

        // Bind shared buffer to caller file handle
        file.pCachedContent = pContent;

        const size_t contentSize = pContent ? pContent->size() : 0;

        // Acquire exclusive write lock for insertion and eviction
        std::unique_lock<std::shared_mutex> lock(this->mutex);

        // Evict least frequently used entries when exceeding capacity or byte limits
        while (!this->entries.empty() && (this->entries.size() >= conf::MAX_FILE_CACHE_ENTRIES ||
               (this->totalContentBytes + contentSize > conf::MAX_FILE_CACHE_TOTAL_SIZE))) {
            auto oldestIt = this->entries.begin();
            uint32_t minAccess = oldestIt->second->accessCount.load(std::memory_order_relaxed);
            for (auto it = this->entries.begin(); it != this->entries.end(); ++it) {
                const uint32_t count = it->second->accessCount.load(std::memory_order_relaxed);
                if (count < minAccess) {
                    minAccess = count;
                    oldestIt = it;
                }
            }
            if (oldestIt->second->pContent) {
                const size_t sz = oldestIt->second->pContent->size();
                this->totalContentBytes = (this->totalContentBytes >= sz) ? (this->totalContentBytes - sz) : 0;
            }
            this->entries.erase(oldestIt);
        }

        // Subtract displaced payload bytes if entry already exists
        const auto existingIt = this->entries.find(uri);
        if (existingIt != this->entries.end() && existingIt->second->pContent) {
            const size_t sz = existingIt->second->pContent->size();
            this->totalContentBytes = (this->totalContentBytes >= sz) ? (this->totalContentBytes - sz) : 0;
        }

        // Store new entry and update aggregate memory footprint
        this->entries[uri] = pEntry;
        this->totalContentBytes += contentSize;
    }

    // Used to evict all cached entries on server shutdown
    void FileCache::clear() {
        // Acquire exclusive write lock and flush entries
        std::unique_lock<std::shared_mutex> lock(this->mutex);
        this->entries.clear();
        this->totalContentBytes = 0;
    }

}
