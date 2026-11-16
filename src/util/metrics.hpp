#ifndef __METRICS_REGISTRY_HPP
#define __METRICS_REGISTRY_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "../http/tools.hpp"

namespace util {

    // Metric dimension enums
    enum class MetricMethod : uint8_t {
        Get = 0,
        Post,
        Head,
        Put,
        Delete,
        Options,
        Trace,
        Patch,
        Other,
        Count
    };

    enum class MetricStatusFamily : uint8_t {
        Status2xx = 0,
        Status3xx,
        Status4xx,
        Status5xx,
        StatusOther,
        Count
    };

    enum class MetricHandler : uint8_t {
        Static = 0,
        Cgi,
        FastCgi,
        Count
    };

    // Cacheline-aligned atomic wrappers to prevent false sharing across threads
    struct AlignedCounter {
        alignas(64) std::atomic<uint64_t> val{0};
    };

    struct AlignedGauge {
        alignas(64) std::atomic<int64_t> val{0};
    };

    // Conversion helpers
    MetricMethod toMetricMethod(const http::METHOD method);
    MetricMethod stringToMetricMethod(std::string_view methodStr);
    MetricStatusFamily toStatusFamily(const int statusCode);
    std::string_view metricMethodToString(const MetricMethod method);
    std::string_view metricStatusFamilyToString(const MetricStatusFamily family);
    std::string_view metricHandlerToString(const MetricHandler handler);

    class MetricsRegistry {
        public:
            // Retrieve global singleton instance
            static MetricsRegistry& getInstance();

            MetricsRegistry();
            ~MetricsRegistry() = default;
            MetricsRegistry(const MetricsRegistry&) = delete;
            MetricsRegistry& operator=(const MetricsRegistry&) = delete;

            // Request dimension counters
            void recordRequest(const MetricMethod method);
            void recordStatus(const int statusCode);
            void recordStatusFamily(const MetricStatusFamily family);
            void recordHandler(const MetricHandler handler);
            void recordCompletedRequest(MetricMethod method, int status, MetricHandler handler);
            uint64_t getCompletedRequestCount(MetricMethod method, MetricStatusFamily status, MetricHandler handler) const;
            void recordBytesRead(const size_t bytes);
            void recordBytesSent(const size_t bytes);

            // Connection gauges
            void recordConnectionOpened();
            void recordConnectionClosed();

            // Worker gauges
            void recordWorkerStarted();
            void recordWorkerFinished();
            void setActiveWorkers(const int64_t count);

            // Metric readers for Prometheus exposition
            uint64_t getMethodCount(const MetricMethod method) const;
            uint64_t getStatusFamilyCount(const MetricStatusFamily family) const;
            uint64_t getHandlerCount(const MetricHandler handler) const;
            uint64_t getBytesRead() const;
            uint64_t getBytesSent() const;
            int64_t getActiveConnections() const;
            uint64_t getTotalConnections() const;
            int64_t getActiveWorkers() const;

            // OS process telemetry
            size_t getProcessRssBytes() const;
            double getProcessCpuSeconds() const;
            double getProcessUptimeSeconds() const;

            // Generate Prometheus plain-text exposition payload
            std::string exportPrometheus() const;

            // Reset all metric counters and gauges (used in test fixtures)
            void reset();

        private:
            std::chrono::steady_clock::time_point startTime;

            // Cacheline-isolated counter banks
            AlignedCounter methodCounters[static_cast<size_t>(MetricMethod::Count)];
            AlignedCounter statusCounters[static_cast<size_t>(MetricStatusFamily::Count)];
            AlignedCounter handlerCounters[static_cast<size_t>(MetricHandler::Count)];
            AlignedCounter completedCounters[static_cast<size_t>(MetricMethod::Count)][static_cast<size_t>(MetricStatusFamily::Count)][static_cast<size_t>(MetricHandler::Count)];

            // Bandwidth counters
            AlignedCounter totalBytesRead;
            AlignedCounter totalBytesSent;

            // Connection and worker gauges
            AlignedGauge activeConnections;
            AlignedCounter totalConnections;
            AlignedGauge activeWorkers;
    };

}

#endif
