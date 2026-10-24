#include "stats.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace http {

    // Used to format byte count into human-readable string
    static std::string formatBytes(uint64_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int idx = 0;
        double val = static_cast<double>(bytes);
        while (val >= 1024.0 && idx < 4) {
            val /= 1024.0;
            ++idx;
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(idx == 0 ? 0 : 2) << val << ' ' << units[idx];
        return oss.str();
    }

    // Used to format duration into human-readable string
    static std::string formatDuration(std::chrono::seconds totalSecs) {
        const uint64_t s = totalSecs.count();
        const uint64_t days = s / 86400;
        const uint64_t hours = (s % 86400) / 3600;
        const uint64_t minutes = (s % 3600) / 60;
        const uint64_t seconds = s % 60;

        std::ostringstream oss;
        if (days > 0) oss << days << "d ";
        if (days > 0 || hours > 0) oss << hours << "h ";
        if (days > 0 || hours > 0 || minutes > 0) oss << minutes << "m ";
        oss << seconds << 's';
        return oss.str();
    }

    ServerStats& ServerStats::getInstance() {
        static ServerStats instance;
        return instance;
    }

    ServerStats::ServerStats() : startTime(std::chrono::steady_clock::now()) {}

    // Record incoming request count
    void ServerStats::recordRequest() {
        totalRequests.fetch_add(1, std::memory_order_relaxed);
    }

    // Categorize response status and increment metric counter
    void ServerStats::recordStatusCode(const int status) {
        if (status >= 200 && status < 300)
            status2xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 300 && status < 400)
            status3xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 400 && status < 500)
            status4xx.fetch_add(1, std::memory_order_relaxed);
        else if (status >= 500 && status < 600)
            status5xx.fetch_add(1, std::memory_order_relaxed);
        else
            statusOther.fetch_add(1, std::memory_order_relaxed);
    }

    // Record incoming ingress byte count
    void ServerStats::recordBytesRead(const size_t bytes) {
        totalBytesRead.fetch_add(bytes, std::memory_order_relaxed);
    }

    // Record outgoing egress byte count
    void ServerStats::recordBytesSent(const size_t bytes) {
        totalBytesSent.fetch_add(bytes, std::memory_order_relaxed);
    }

    // Format uptime duration from start timestamp
    std::string ServerStats::formatUptime() const {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);
        return formatDuration(elapsed);
    }

    // Generate formatted statistics summary report
    std::string ServerStats::formatStats(const size_t usedThreads, const size_t totalThreads, const size_t pendingConnections, const size_t activeClients) const {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);

        // Load atomic metric counters
        const uint64_t reqs = getTotalRequests();
        const uint64_t c2xx = getStatus2xx();
        const uint64_t c3xx = getStatus3xx();
        const uint64_t c4xx = getStatus4xx();
        const uint64_t c5xx = getStatus5xx();
        const uint64_t cOther = getStatusOther();

        // Calculate distribution percentages
        const double threadPct = totalThreads > 0 ? (static_cast<double>(usedThreads) / totalThreads * 100.0) : 0.0;
        const double p2xx = reqs > 0 ? (static_cast<double>(c2xx) / reqs * 100.0) : 0.0;
        const double p3xx = reqs > 0 ? (static_cast<double>(c3xx) / reqs * 100.0) : 0.0;
        const double p4xx = reqs > 0 ? (static_cast<double>(c4xx) / reqs * 100.0) : 0.0;
        const double p5xx = reqs > 0 ? (static_cast<double>(c5xx) / reqs * 100.0) : 0.0;

        // Construct formatted report
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "> Server Statistics:\n"
            << "  Uptime:              " << formatDuration(elapsed) << '\n'
            << "  Active Connections:  " << activeClients << '\n'
            << "  Worker Threads:      " << usedThreads << '/' << totalThreads << " (" << threadPct << "% usage, " << pendingConnections << " queued)\n"
            << "  Total Requests:      " << reqs << '\n'
            << "  Status Distribution: 2xx: " << c2xx << " (" << p2xx << "%) | "
            << "3xx: " << c3xx << " (" << p3xx << "%) | "
            << "4xx: " << c4xx << " (" << p4xx << "%) | "
            << "5xx: " << c5xx << " (" << p5xx << "%)";
        if (cOther > 0)
            oss << " | Other: " << cOther;
        oss << '\n'
            << "  Traffic:             Rx: " << formatBytes(getBytesRead()) << " | Tx: " << formatBytes(getBytesSent());

        return oss.str();
    }

}

