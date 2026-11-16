#include "metrics.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>

#ifdef _WIN32
    #include "../winheader.hpp"
    #include <psapi.h>
#else
    #include <fstream>
    #include <sys/resource.h>
    #include <unistd.h>
#endif

namespace util {

    // Map internal HTTP method enum to metric method enum
    MetricMethod toMetricMethod(const http::METHOD method) {
        switch (method) {
            case http::METHOD::GET:     return MetricMethod::Get;
            case http::METHOD::POST:    return MetricMethod::Post;
            case http::METHOD::HEAD:    return MetricMethod::Head;
            case http::METHOD::PUT:     return MetricMethod::Put;
            case http::METHOD::DEL:     return MetricMethod::Delete;
            case http::METHOD::OPTIONS: return MetricMethod::Options;
            case http::METHOD::PATCH:   return MetricMethod::Patch;
            default:                    return MetricMethod::Other;
        }
    }

    // Map raw HTTP method string to metric method enum
    MetricMethod stringToMetricMethod(std::string_view methodStr) {
        if (methodStr == "GET")     return MetricMethod::Get;
        if (methodStr == "POST")    return MetricMethod::Post;
        if (methodStr == "HEAD")    return MetricMethod::Head;
        if (methodStr == "PUT")     return MetricMethod::Put;
        if (methodStr == "DELETE")  return MetricMethod::Delete;
        if (methodStr == "OPTIONS") return MetricMethod::Options;
        if (methodStr == "TRACE")   return MetricMethod::Trace;
        if (methodStr == "PATCH")   return MetricMethod::Patch;
        return MetricMethod::Other;
    }

    // Categorize HTTP status code into Prometheus status family
    MetricStatusFamily toStatusFamily(const int statusCode) {
        if (statusCode >= 200 && statusCode < 300) return MetricStatusFamily::Status2xx;
        if (statusCode >= 300 && statusCode < 400) return MetricStatusFamily::Status3xx;
        if (statusCode >= 400 && statusCode < 500) return MetricStatusFamily::Status4xx;
        if (statusCode >= 500 && statusCode < 600) return MetricStatusFamily::Status5xx;
        return MetricStatusFamily::StatusOther;
    }

    // Convert metric method enum to uppercase string representation
    std::string_view metricMethodToString(const MetricMethod method) {
        switch (method) {
            case MetricMethod::Get:     return "GET";
            case MetricMethod::Post:    return "POST";
            case MetricMethod::Head:    return "HEAD";
            case MetricMethod::Put:     return "PUT";
            case MetricMethod::Delete:  return "DELETE";
            case MetricMethod::Options: return "OPTIONS";
            case MetricMethod::Trace:   return "TRACE";
            case MetricMethod::Patch:   return "PATCH";
            default:                    return "OTHER";
        }
    }

    // Convert status family enum to string representation
    std::string_view metricStatusFamilyToString(const MetricStatusFamily family) {
        switch (family) {
            case MetricStatusFamily::Status2xx:   return "2xx";
            case MetricStatusFamily::Status3xx:   return "3xx";
            case MetricStatusFamily::Status4xx:   return "4xx";
            case MetricStatusFamily::Status5xx:   return "5xx";
            default:                              return "other";
        }
    }

    // Convert handler enum to string representation
    std::string_view metricHandlerToString(const MetricHandler handler) {
        switch (handler) {
            case MetricHandler::Static:  return "static";
            case MetricHandler::Cgi:     return "cgi";
            case MetricHandler::FastCgi: return "fastcgi";
            default:                     return "unknown";
        }
    }

    // Retrieve global singleton instance
    MetricsRegistry& MetricsRegistry::getInstance() {
        static MetricsRegistry instance;
        return instance;
    }

    // Initialize metrics registry
    MetricsRegistry::MetricsRegistry() : startTime(std::chrono::steady_clock::now()) { reset(); }

    // Record incoming request count by method
    void MetricsRegistry::recordRequest(const MetricMethod method) {
        const size_t idx = static_cast<size_t>(method);
        if (idx < static_cast<size_t>(MetricMethod::Count))
            methodCounters[idx].val.fetch_add(1, std::memory_order_relaxed);
    }

    // Record HTTP response status code
    void MetricsRegistry::recordStatus(const int statusCode) {
        recordStatusFamily(toStatusFamily(statusCode));
    }

    // Record HTTP response status code family
    void MetricsRegistry::recordStatusFamily(const MetricStatusFamily family) {
        const size_t idx = static_cast<size_t>(family);
        if (idx < static_cast<size_t>(MetricStatusFamily::Count))
            statusCounters[idx].val.fetch_add(1, std::memory_order_relaxed);
    }

    // Record request routing handler
    void MetricsRegistry::recordHandler(const MetricHandler handler) {
        const size_t idx = static_cast<size_t>(handler);
        if (idx < static_cast<size_t>(MetricHandler::Count))
            handlerCounters[idx].val.fetch_add(1, std::memory_order_relaxed);
    }

    // Record completed request across all dimensions
    void MetricsRegistry::recordCompletedRequest(MetricMethod method, int status, MetricHandler handler) {
        const size_t m = static_cast<size_t>(method), s = static_cast<size_t>(toStatusFamily(status)), h = static_cast<size_t>(handler);
        if (m < static_cast<size_t>(MetricMethod::Count) && h < static_cast<size_t>(MetricHandler::Count))
            completedCounters[m][s][h].val.fetch_add(1, std::memory_order_relaxed);
    }

    // Retrieve completed request count for dimension combination
    uint64_t MetricsRegistry::getCompletedRequestCount(MetricMethod method, MetricStatusFamily status, MetricHandler handler) const {
        const size_t m = static_cast<size_t>(method), s = static_cast<size_t>(status), h = static_cast<size_t>(handler);
        if (m >= static_cast<size_t>(MetricMethod::Count) || s >= static_cast<size_t>(MetricStatusFamily::Count) || h >= static_cast<size_t>(MetricHandler::Count)) return 0;
        return completedCounters[m][s][h].val.load(std::memory_order_relaxed);
    }

    // Record incoming ingress bytes read
    void MetricsRegistry::recordBytesRead(const size_t bytes) { totalBytesRead.val.fetch_add(bytes, std::memory_order_relaxed); }

    // Record outgoing egress bytes written
    void MetricsRegistry::recordBytesSent(const size_t bytes) { totalBytesSent.val.fetch_add(bytes, std::memory_order_relaxed); }

    // Increment active and cumulative connection counts
    void MetricsRegistry::recordConnectionOpened() {
        activeConnections.val.fetch_add(1, std::memory_order_relaxed);
        totalConnections.val.fetch_add(1, std::memory_order_relaxed);
    }

    // Decrement active connection count
    void MetricsRegistry::recordConnectionClosed() { activeConnections.val.fetch_sub(1, std::memory_order_relaxed); }

    // Increment active thread pool worker count
    void MetricsRegistry::recordWorkerStarted() { activeWorkers.val.fetch_add(1, std::memory_order_relaxed); }

    // Decrement active thread pool worker count
    void MetricsRegistry::recordWorkerFinished() { activeWorkers.val.fetch_sub(1, std::memory_order_relaxed); }

    // Set active worker count gauge
    void MetricsRegistry::setActiveWorkers(const int64_t count) { activeWorkers.val.store(count, std::memory_order_relaxed); }

    // Map observed latency duration to discrete histogram bucket index
    inline size_t getLatencyBucketIndex(const double durationSeconds) {
        for (size_t i = 0; i < LATENCY_FINITE_BUCKETS; ++i) {
            if (durationSeconds <= LATENCY_BUCKET_UPPER_BOUNDS[i])
                return i;
        }
        return LATENCY_FINITE_BUCKETS;
    }

    // Record request duration into discrete differential bucket counter
    void MetricsRegistry::recordRequestDuration(const double durationSeconds) {
        if (!trackLatency.load(std::memory_order_relaxed) || !std::isfinite(durationSeconds) || durationSeconds < 0.0 ||
            durationSeconds * 1000000.0 >= static_cast<double>(std::numeric_limits<uint64_t>::max())) return;

        const size_t bucketIdx = getLatencyBucketIndex(durationSeconds);
        const uint64_t micros = static_cast<uint64_t>(durationSeconds * 1000000.0);

        // Order writer entry, data updates, and completion against snapshot reads
        latencyWriters.val.fetch_add(1, std::memory_order_seq_cst);
        latencySumMicroseconds.val.fetch_add(micros, std::memory_order_seq_cst);
        rawLatencyBuckets[bucketIdx].val.fetch_add(1, std::memory_order_seq_cst);
        latencyVersion.val.fetch_add(1, std::memory_order_seq_cst);
        latencyWriters.val.fetch_sub(1, std::memory_order_seq_cst);
    }

    // Retry snapshots overlapping observations; never stall request writers
    std::optional<LatencySnapshot> MetricsRegistry::getLatencySnapshot() const {
        constexpr size_t MAX_SNAPSHOT_ATTEMPTS = 128;
        for (size_t attempt = 0; attempt < MAX_SNAPSHOT_ATTEMPTS; ++attempt) {
            const auto version = latencyVersion.val.load(std::memory_order_seq_cst);
            if (latencyWriters.val.load(std::memory_order_seq_cst) != 0) {
                std::this_thread::yield();
                continue;
            }
            LatencySnapshot snap{};
            const auto micros = latencySumMicroseconds.val.load(std::memory_order_seq_cst);
            uint64_t cumulative = 0;
            for (size_t i = 0; i < LATENCY_TOTAL_BUCKETS; ++i) {
                cumulative += rawLatencyBuckets[i].val.load(std::memory_order_seq_cst);
                snap.buckets[i] = cumulative;
            }
            // Reject both unfinished writers and writers completed during reads
            if (latencyWriters.val.load(std::memory_order_seq_cst) == 0 &&
                latencyVersion.val.load(std::memory_order_seq_cst) == version) {
                snap.count = cumulative;
                snap.sumSeconds = static_cast<double>(micros) / 1000000.0;
                return snap;
            }
            std::this_thread::yield();
        }
        return std::nullopt;
    }

    // Check whether latency tracking is enabled
    bool MetricsRegistry::isTrackLatencyEnabled() const {
        return trackLatency.load(std::memory_order_relaxed);
    }

    // Set latency tracking enabled state
    void MetricsRegistry::setTrackLatency(const bool enabled) {
        trackLatency.store(enabled, std::memory_order_relaxed);
    }

    // Retrieve request count by method
    uint64_t MetricsRegistry::getMethodCount(const MetricMethod method) const {
        const size_t idx = static_cast<size_t>(method);
        return idx < static_cast<size_t>(MetricMethod::Count)
            ? methodCounters[idx].val.load(std::memory_order_relaxed) : 0;
    }

    // Retrieve request count by status family
    uint64_t MetricsRegistry::getStatusFamilyCount(const MetricStatusFamily family) const {
        const size_t idx = static_cast<size_t>(family);
        return idx < static_cast<size_t>(MetricStatusFamily::Count)
            ? statusCounters[idx].val.load(std::memory_order_relaxed) : 0;
    }

    // Retrieve request count by handler
    uint64_t MetricsRegistry::getHandlerCount(const MetricHandler handler) const {
        const size_t idx = static_cast<size_t>(handler);
        return idx < static_cast<size_t>(MetricHandler::Count)
            ? handlerCounters[idx].val.load(std::memory_order_relaxed) : 0;
    }

    // Retrieve cumulative ingress bytes
    uint64_t MetricsRegistry::getBytesRead() const { return totalBytesRead.val.load(std::memory_order_relaxed); }

    // Retrieve cumulative egress bytes
    uint64_t MetricsRegistry::getBytesSent() const { return totalBytesSent.val.load(std::memory_order_relaxed); }

    // Retrieve active connection count gauge
    int64_t MetricsRegistry::getActiveConnections() const { return activeConnections.val.load(std::memory_order_relaxed); }

    // Retrieve cumulative accepted connection count
    uint64_t MetricsRegistry::getTotalConnections() const { return totalConnections.val.load(std::memory_order_relaxed); }

    // Retrieve active thread pool worker count gauge
    int64_t MetricsRegistry::getActiveWorkers() const { return activeWorkers.val.load(std::memory_order_relaxed); }

    // Retrieve resident set size in bytes
    size_t MetricsRegistry::getProcessRssBytes() const {
        #ifdef _WIN32
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
                return static_cast<size_t>(pmc.WorkingSetSize);
            return 0;
        #else
            std::ifstream statm("/proc/self/statm");
            if (statm.is_open()) {
                size_t size = 0, resident = 0;
                if (statm >> size >> resident) {
                    const long pageSize = sysconf(_SC_PAGESIZE);
                    if (pageSize > 0)
                        return resident * static_cast<size_t>(pageSize);
                }
            }
            return 0;
        #endif
    }

    // Retrieve cumulative user and system CPU time in seconds
    double MetricsRegistry::getProcessCpuSeconds() const {
        #ifdef _WIN32
            FILETIME ftCreation, ftExit, ftKernel, ftUser;
            if (GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                ULARGE_INTEGER kernelTime, userTime;
                kernelTime.LowPart = ftKernel.dwLowDateTime;
                kernelTime.HighPart = ftKernel.dwHighDateTime;
                userTime.LowPart = ftUser.dwLowDateTime;
                userTime.HighPart = ftUser.dwHighDateTime;
                return static_cast<double>(kernelTime.QuadPart + userTime.QuadPart) * 1e-7;
            }
            return 0.0;
        #else
            struct rusage usage{};
            if (getrusage(RUSAGE_SELF, &usage) == 0) {
                const double userSec = static_cast<double>(usage.ru_utime.tv_sec) +
                    static_cast<double>(usage.ru_utime.tv_usec) / 1e6;
                const double sysSec = static_cast<double>(usage.ru_stime.tv_sec) +
                    static_cast<double>(usage.ru_stime.tv_usec) / 1e6;
                return userSec + sysSec;
            }
            return 0.0;
        #endif
    }

    // Retrieve process uptime in seconds
    double MetricsRegistry::getProcessUptimeSeconds() const {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - startTime).count();
    }

    // Format all registered metrics into Prometheus plain-text exposition format
    std::string MetricsRegistry::exportPrometheus() const {
        std::ostringstream oss;

        // HTTP request totals by method, status family, and handler
        oss << "# HELP mercury_http_requests_total Total number of HTTP requests completed.\n"
            << "# TYPE mercury_http_requests_total counter\n";

        for (size_t m = 0; m < static_cast<size_t>(MetricMethod::Count); ++m) {
            for (size_t s = 0; s < static_cast<size_t>(MetricStatusFamily::Count); ++s) {
                for (size_t h = 0; h < static_cast<size_t>(MetricHandler::Count); ++h) {
                    const uint64_t count = completedCounters[m][s][h].val.load(std::memory_order_relaxed);
                    oss << "mercury_http_requests_total{method=\""
                        << metricMethodToString(static_cast<MetricMethod>(m))
                        << "\",status=\""
                        << metricStatusFamilyToString(static_cast<MetricStatusFamily>(s))
                        << "\",handler=\""
                        << metricHandlerToString(static_cast<MetricHandler>(h))
                        << "\"} " << count << '\n';
                }
            }
        }

        // Request duration histogram
        if (trackLatency.load(std::memory_order_relaxed)) {
            const auto snap = getLatencySnapshot();
            if (!snap) return {};
            oss << "# HELP mercury_http_request_duration_seconds HTTP request duration in seconds.\n"
                << "# TYPE mercury_http_request_duration_seconds histogram\n";
            for (size_t i = 0; i < LATENCY_TOTAL_BUCKETS; ++i) {
                oss << "mercury_http_request_duration_seconds_bucket{le=\""
                    << LATENCY_BUCKET_LABELS[i] << "\"} "
                    << snap->buckets[i] << '\n';
            }
            oss << std::fixed << std::setprecision(6)
                << "mercury_http_request_duration_seconds_sum " << snap->sumSeconds << '\n'
                << std::defaultfloat
                << "mercury_http_request_duration_seconds_count " << snap->count << '\n';
        }

        // Ingress bandwidth
        oss << "# HELP mercury_http_bytes_received_total Cumulative ingress bytes read from sockets.\n"
            << "# TYPE mercury_http_bytes_received_total counter\n"
            << "mercury_http_bytes_received_total " << getBytesRead() << '\n';

        // Egress bandwidth
        oss << "# HELP mercury_http_bytes_sent_total Cumulative egress bytes sent to sockets.\n"
            << "# TYPE mercury_http_bytes_sent_total counter\n"
            << "mercury_http_bytes_sent_total " << getBytesSent() << '\n';

        // Active TCP connections gauge
        oss << "# HELP mercury_active_connections Current number of active client TCP connections.\n"
            << "# TYPE mercury_active_connections gauge\n"
            << "mercury_active_connections " << getActiveConnections() << '\n';

        // Cumulative accepted connections counter
        oss << "# HELP mercury_total_connections Cumulative count of client TCP connections accepted.\n"
            << "# TYPE mercury_total_connections counter\n"
            << "mercury_total_connections " << getTotalConnections() << '\n';

        // Worker threads gauge
        oss << "# HELP mercury_workers_active Number of worker threads currently processing tasks.\n"
            << "# TYPE mercury_workers_active gauge\n"
            << "mercury_workers_active " << getActiveWorkers() << '\n';

        // Resident set size (RSS)
        oss << "# HELP mercury_process_resident_memory_bytes Resident memory size (RSS) in bytes.\n"
            << "# TYPE mercury_process_resident_memory_bytes gauge\n"
            << "mercury_process_resident_memory_bytes " << getProcessRssBytes() << '\n';

        // CPU seconds
        oss << std::fixed << std::setprecision(3);
        oss << "# HELP mercury_process_cpu_seconds_total Total user and system CPU time spent in seconds.\n"
            << "# TYPE mercury_process_cpu_seconds_total counter\n"
            << "mercury_process_cpu_seconds_total " << getProcessCpuSeconds() << '\n';

        // Uptime seconds
        oss << "# HELP mercury_process_uptime_seconds Process uptime in seconds.\n"
            << "# TYPE mercury_process_uptime_seconds gauge\n"
            << "mercury_process_uptime_seconds " << getProcessUptimeSeconds() << '\n';

        return oss.str();
    }

    // Reset all metric counters and gauges to zero
    void MetricsRegistry::reset() {
        startTime = std::chrono::steady_clock::now();
        for (auto& methods : completedCounters)
            for (auto& statuses : methods)
                for (auto& counter : statuses)
                    counter.val.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < static_cast<size_t>(MetricMethod::Count); ++i)
            methodCounters[i].val.store(0, std::memory_order_relaxed);

        for (size_t i = 0; i < static_cast<size_t>(MetricStatusFamily::Count); ++i)
            statusCounters[i].val.store(0, std::memory_order_relaxed);

        for (size_t i = 0; i < static_cast<size_t>(MetricHandler::Count); ++i)
            handlerCounters[i].val.store(0, std::memory_order_relaxed);

        totalBytesRead.val.store(0, std::memory_order_relaxed);
        totalBytesSent.val.store(0, std::memory_order_relaxed);
        activeConnections.val.store(0, std::memory_order_relaxed);
        totalConnections.val.store(0, std::memory_order_relaxed);
        activeWorkers.val.store(0, std::memory_order_relaxed);

        {
            latencyWriters.val.store(0, std::memory_order_relaxed);
            latencyVersion.val.store(0, std::memory_order_relaxed);
            for (size_t i = 0; i < LATENCY_TOTAL_BUCKETS; ++i)
                rawLatencyBuckets[i].val.store(0, std::memory_order_relaxed);
            latencySumMicroseconds.val.store(0, std::memory_order_relaxed);
        }
    }

}
