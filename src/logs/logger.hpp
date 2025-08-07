#ifndef __LOGGER_HPP
#define __LOGGER_HPP

#include <atomic>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <ostream>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#include "../conf/conf.hpp"

#define ACCESS_LOG Logger::getInstance()(true)
#define ERROR_LOG Logger::getInstance()(false)

// Fwd dec.
class LoggerStream;
std::string genTimestamp();

// Log singleton class
class Logger {
    public:
        // Singleton handling
        static Logger& getInstance() {
            static Logger inst;
            return inst;
        };
        Logger();
        ~Logger();
        Logger(const Logger&) = delete; // Prevent copies
        void operator=(const Logger&) = delete; // Prevent copies

        LoggerStream operator()(const bool);

        void queueAccessLog(const std::string&);
        void queueErrorLog(const std::string&);

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

#endif