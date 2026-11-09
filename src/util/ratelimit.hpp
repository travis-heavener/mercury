#ifndef __UTIL_RATELIMIT_HPP
#define __UTIL_RATELIMIT_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../conf/rules/access.hpp"

namespace util {

    // Single token bucket tracking client request quota
    struct TokenBucket {
        double tokens{0.0};
        std::chrono::steady_clock::time_point lastReplenish{};
    };

    // Sharded bucket container to minimize lock contention across worker threads
    struct RateLimitShard {
        mutable std::mutex mtx;
        std::unordered_map<std::string, TokenBucket> buckets;
        std::chrono::steady_clock::time_point lastCleanup{};
    };

    class RateLimiter {
        public:
            // Returns singleton instance
            static RateLimiter& getInstance();

            // Used to configure rate limit parameters
            void configure(const bool enabled, const double rate, const double burst);

            // Used to register whitelisted CIDR or IP
            void addWhitelist(const conf::SanitizedIP& ip);

            // Used to reset rate limiter state and purge bucket shards
            void clear();

            // Used to check if request is permitted under rate limit, consuming one token if allowed
            bool checkAndConsume(const std::string& clientIP, const bool isDNT, double& retryAfter);

            inline bool isEnabled() const { return this->enabled; }
            inline double getRate() const { return this->rate; }
            inline double getBurst() const { return this->burst; }

        private:
            RateLimiter();
            ~RateLimiter() = default;

            RateLimiter(const RateLimiter&) = delete;
            RateLimiter& operator=(const RateLimiter&) = delete;

            bool enabled{false};
            double rate{100.0}; // Allowed requests per second
            double burst{20.0}; // Maximum burst token capacity

            // Whitelist synchronization w/ lock-free fast path
            std::atomic<bool> hasWhitelist{false};
            mutable std::shared_mutex configMutex;
            std::vector<conf::SanitizedIP> whitelist;

            static constexpr size_t NUM_SHARDS = 64;
            static constexpr size_t MAX_BUCKETS_PER_SHARD = 8192;

            std::array<RateLimitShard, NUM_SHARDS> shards;

            // Returns true if candidate IP matches any whitelisted range
            bool isWhitelisted(const std::string& clientIP) const;
    };

}

#endif
