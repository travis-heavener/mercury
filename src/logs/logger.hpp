#ifndef __LOGGER_HPP
#define __LOGGER_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <ostream>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#define ACCESS_LOG Logger::getInstance()(true)
#define ERROR_LOG Logger::getInstance()(false)

// Fwd dec.
class Logger;
std::string genTimestamp();

// Log stream class
using StreamManipulator = std::ostream& (*)(std::ostream&);
class LoggerStream {
    public:
        LoggerStream(Logger& logger, const bool isAccess) : logger(logger), isAccess(isAccess) {};

        template<typename T>
        LoggerStream& operator<<(const T& val) {
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
        Logger(const Logger&) = delete; // Prevent copies
        void operator=(const Logger&) = delete; // Prevent copies

        inline LoggerStream operator()(const bool isAccess) { return LoggerStream(*this, isAccess); }

        // Queues access logs
        inline void queueAccessLog(const std::string& log) {
            std::lock_guard<std::mutex> lock(accessQueueMutex);
            accessQueue.push(log);
            cv.notify_one();
        }

        // Queues error logs
        inline void queueErrorLog(const std::string& log) {
            std::lock_guard<std::mutex> lock(errorQueueMutex);
            errorQueue.push(log);
            cv.notify_one();
        }

        void threadWrite();
    private:
        std::queue<std::string> accessQueue;
        std::queue<std::string> errorQueue;

        std::atomic<bool> isExited;
        std::condition_variable cv;

        std::mutex accessQueueMutex;
        std::mutex errorQueueMutex;
        std::mutex writeMutex;

        std::thread thread;
};

#endif