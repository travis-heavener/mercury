#ifndef __LOGGER_HPP
#define __LOGGER_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <ostream>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#define ACCESS_LOG Logger::getInstance()(true)
#define ERROR_LOG Logger::getInstance()(false)

// Fwd dec.
class Logger;
std::string genTimestamp();
std::string genIso8601Timestamp();
std::string escapeJsonString(std::string_view input);

namespace http {
    class Request;
}

void logJsonAccess(const http::Request& request, int status, double latencyMs, size_t bytesSent);
void logJsonShutdown();
void logJsonListen(unsigned int port, bool ipv4, bool tls, std::string_view message);
void logJsonStartup(std::string_view version, std::string_view message);
void logJsonPrivilegeDrop(std::string_view user, unsigned long long uid, unsigned long long gid);
void logJsonFastcgiStart(std::string_view address);
void logJsonAccess(const std::string& method, const std::string& clientIP, const std::string& uri,
    int status, const std::string& version, double latencyMs, size_t bytesSent,
    const std::string& userAgent = "", const std::string& referer = "");

namespace conf {
    extern bool DISABLE_LOGS;
}

// Log stream class
using StreamManipulator = std::ostream& (*)(std::ostream&);
class LoggerStream {
    public:
        LoggerStream(Logger& logger, const bool isAccess) : logger(logger), isAccess(isAccess) {};

        template<typename T>
        LoggerStream& operator<<(const T& val) {
            if (conf::DISABLE_LOGS) return *this;
            buffer << val;
            return *this;
        }

        LoggerStream& operator<<(StreamManipulator);

    private:
        std::ostringstream buffer;
        Logger& logger;
        bool isAccess;
};

// Log singleton class
class Logger {
    public:
        // Singleton handling
        inline static Logger& getInstance() {
            static Logger inst;
            return inst;
        };
        Logger();
        ~Logger();
        void stop();
        Logger(const Logger&) = delete; // Prevent copies
        void operator=(const Logger&) = delete; // Prevent copies

        inline LoggerStream operator()(const bool isAccess) { return LoggerStream(*this, isAccess); }

        static constexpr size_t MAX_LOG_QUEUE_SIZE = 10000;

        // Queues access logs
        inline void queueAccessLog(std::string log) {
            if (conf::DISABLE_LOGS) return;
            std::lock_guard<std::mutex> lock(writeMutex);
            if (isExited) return;
            if (accessQueue.size() >= MAX_LOG_QUEUE_SIZE)
                accessQueue.pop();
            accessQueue.push(std::move(log));
            cv.notify_one();
        }

        // Queues error logs
        inline void queueErrorLog(std::string log) {
            if (conf::DISABLE_LOGS) return;
            std::lock_guard<std::mutex> lock(writeMutex);
            if (isExited) return;
            if (errorQueue.size() >= MAX_LOG_QUEUE_SIZE)
                errorQueue.pop();
            errorQueue.push(std::move(log));
            cv.notify_one();
        }

        void threadWrite();
    private:
        std::queue<std::string> accessQueue;
        std::queue<std::string> errorQueue;

        std::atomic<bool> isExited;
        std::condition_variable cv;

        std::mutex writeMutex;
        std::mutex stopMutex;

        std::thread thread;
};

// Helper method for formatting client IPs based on the ClientSecurityMode config node
std::string formatClientIP(const std::string&, const bool isDNT);

#endif
