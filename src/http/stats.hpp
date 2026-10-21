#ifndef __HTTP_STATS_HPP
#define __HTTP_STATS_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace http {

    class ServerStats {
        public:
            // Used to retrieve global singleton instance
            static ServerStats& getInstance();

            // Initialize server stats and record process start time
            ServerStats();

            // Record incoming request count
            void recordRequest();

            // Categorize and record HTTP response status code
            void recordStatusCode(const int status);

            // Record incoming bytes read from socket
            void recordBytesRead(const size_t bytes);

            // Record outgoing bytes written to socket
            void recordBytesSent(const size_t bytes);

            // Format comprehensive statistics report string
            std::string formatStats(const size_t usedThreads, const size_t totalThreads, const size_t pendingConnections, const size_t activeClients) const;

            // Format server uptime into human-readable duration
            std::string formatUptime() const;

            // Direct metric getters
            inline uint64_t getTotalRequests() const { return totalRequests.load(std::memory_order_relaxed); }
            inline uint64_t getBytesRead() const { return totalBytesRead.load(std::memory_order_relaxed); }
            inline uint64_t getBytesSent() const { return totalBytesSent.load(std::memory_order_relaxed); }
            inline uint64_t getStatus2xx() const { return status2xx.load(std::memory_order_relaxed); }
            inline uint64_t getStatus3xx() const { return status3xx.load(std::memory_order_relaxed); }
            inline uint64_t getStatus4xx() const { return status4xx.load(std::memory_order_relaxed); }
            inline uint64_t getStatus5xx() const { return status5xx.load(std::memory_order_relaxed); }
            inline uint64_t getStatusOther() const { return statusOther.load(std::memory_order_relaxed); }

        private:
            std::chrono::steady_clock::time_point startTime; // Process start timestamp
            std::atomic<uint64_t> totalRequests{0};          // Cumulative HTTP request count
            std::atomic<uint64_t> totalBytesRead{0};         // Cumulative ingress bytes read
            std::atomic<uint64_t> totalBytesSent{0};         // Cumulative egress bytes written
            std::atomic<uint64_t> status2xx{0};              // 2xx Success status count
            std::atomic<uint64_t> status3xx{0};              // 3xx Redirection status count
            std::atomic<uint64_t> status4xx{0};              // 4xx Client error status count
            std::atomic<uint64_t> status5xx{0};              // 5xx Server error status count
            std::atomic<uint64_t> statusOther{0};            // Non-standard or informational status count
    };

}

#endif

