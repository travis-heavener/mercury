#include "metrics.hpp"

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
    MetricsRegistry::MetricsRegistry() { reset(); }

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

    // Reset all metric counters and gauges to zero
    void MetricsRegistry::reset() {
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
    }

}
