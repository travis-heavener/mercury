#include "ratelimit.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "../logs/logger.hpp"

namespace util {

    // Returns singleton instance
    RateLimiter& RateLimiter::getInstance() {
        static RateLimiter instance;
        return instance;
    }

    RateLimiter::RateLimiter() = default;

    // Used to configure rate limit parameters
    void RateLimiter::configure(const bool enabled, const double rate, const double burst) {
        std::unique_lock lock(this->configMutex);
        this->enabled = enabled;
        this->rate = rate > 0.0 ? rate : 1.0;
        this->burst = burst >= 1.0 ? burst : 1.0;
        this->hasWhitelist.store(!this->whitelist.empty(), std::memory_order_release);
    }

    // Used to register whitelisted CIDR or IP
    void RateLimiter::addWhitelist(const conf::SanitizedIP& ip) {
        std::unique_lock lock(this->configMutex);
        this->whitelist.push_back(ip);
        this->hasWhitelist.store(true, std::memory_order_release);
    }

    // Used to reset rate limiter state and purge bucket shards
    void RateLimiter::clear() {
        std::unique_lock lock(this->configMutex);
        this->enabled = false;
        this->rate = 100.0;
        this->burst = 20.0;
        this->whitelist.clear();
        this->hasWhitelist.store(false, std::memory_order_release);

        // Clear every bucket shard under lock
        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            std::unique_lock shardLock(this->shards[i].mtx);
            this->shards[i].buckets.clear();
            this->shards[i].lastCleanup = std::chrono::steady_clock::time_point{};
        }
    }

    // Returns true if candidate IP matches any whitelisted range
    bool RateLimiter::isWhitelisted(const std::string& clientIP) const {
        // Fast-path bypass when no whitelist entries exist
        if (!this->hasWhitelist.load(std::memory_order_relaxed)) return false;

        std::shared_lock lock(this->configMutex);
        if (this->whitelist.empty()) return false;

        try {
            // Parse candidate IP into binary representation
            conf::SanitizedIP candidate = conf::parseSanitizedClientIP(clientIP);
            for (const auto& allowed : this->whitelist)
                if (conf::doesAddressFitCIDR(allowed, candidate)) return true;
        } catch (...) {
            // Treat unparseable IPs as non-whitelisted
            return false;
        }

        return false;
    }

    // Used to check if request is permitted under rate limit, consuming one token if allowed
    bool RateLimiter::checkAndConsume(const std::string& clientIP, const bool isDNT, double& retryAfter) {
        if (!this->enabled) return true;

        // Skip rate limiting for whitelisted IPs
        if (this->isWhitelisted(clientIP)) return true;

        // Derive bucket key respecting ClientSecurityMode (GPC/DNT privacy hash or subnet mask)
        const std::string key = formatClientIP(clientIP, isDNT);

        // Map key to shard bucket container
        const size_t shardIndex = std::hash<std::string>{}(key) % NUM_SHARDS;
        RateLimitShard& shard = this->shards[shardIndex];

        const auto now = std::chrono::steady_clock::now();

        std::unique_lock lock(shard.mtx);

        // Periodically sweep idle buckets (every 60 seconds)
        if (shard.lastCleanup == std::chrono::steady_clock::time_point{}) {
            shard.lastCleanup = now;
        } else if (std::chrono::duration_cast<std::chrono::seconds>(now - shard.lastCleanup).count() >= 60) {
            shard.lastCleanup = now;
            for (auto it = shard.buckets.begin(); it != shard.buckets.end(); ) {
                const double idleSeconds = std::chrono::duration<double>(now - it->second.lastReplenish).count();
                // Evict buckets idle for > 60s since they have fully replenished
                if (idleSeconds >= 60.0) {
                    it = shard.buckets.erase(it);
                } else {
                    ++it;
                }
            }
        }

        auto it = shard.buckets.find(key);
        if (it == shard.buckets.end()) {
            // Evict arbitrary entry if shard capacity is exhausted to bound memory
            if (shard.buckets.size() >= MAX_BUCKETS_PER_SHARD)
                shard.buckets.erase(shard.buckets.begin());

            // Initialize new bucket with burst allowance minus consumed token
            TokenBucket bucket;
            bucket.tokens = this->burst - 1.0;
            bucket.lastReplenish = now;
            shard.buckets.emplace(key, bucket);
            return true;
        }

        TokenBucket& bucket = it->second;

        // Replenish tokens based on elapsed monotonic time
        const double elapsed = std::chrono::duration<double>(now - bucket.lastReplenish).count();
        bucket.tokens = std::min(this->burst, bucket.tokens + elapsed * this->rate);
        bucket.lastReplenish = now;

        // Allow request if sufficient token quota exists
        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            return true;
        }

        // Insufficient tokens; compute retry-after in whole seconds without deducting tokens
        const double deficit = 1.0 - bucket.tokens;
        retryAfter = std::max(1.0, std::ceil(deficit / this->rate));
        return false;
    }

}
