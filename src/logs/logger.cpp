#include "logger.hpp"

#include <iomanip>

#include "../pch/common.hpp"
#include "../conf/conf.hpp"

Logger::Logger() : isExited(false) {
    // Create thread
    this->thread = std::thread(&Logger::threadWrite, this);
}

Logger::~Logger() {
    // Join & close thread
    {
        std::lock_guard<std::mutex> lock(writeMutex);
        isExited = true;
    }

    // Flush queues (logs closed by config handler)
    cv.notify_one();

    if (thread.joinable())
        thread.join();

    // Close log file handles
    if (conf::accessLogHandle.is_open())
        conf::accessLogHandle.close();

    if (conf::errorLogHandle.is_open())
        conf::errorLogHandle.close();
}

LoggerStream Logger::operator()(const bool isAccess) {
    return LoggerStream(*this, isAccess);
}

// Queues access logs
void Logger::queueAccessLog(const std::string& log) {
    std::lock_guard<std::mutex> lock(accessQueueMutex);
    accessQueue.push(log);
    cv.notify_one();
}

// Queues error logs
void Logger::queueErrorLog(const std::string& log) {
    std::lock_guard<std::mutex> lock(errorQueueMutex);
    errorQueue.push(log);
    cv.notify_one();
}

// Writes from each queue to log files
void Logger::threadWrite() {
    while (true) {
        std::unique_lock<std::mutex> lock(writeMutex);
        cv.wait(lock, [this]() {
            return !accessQueue.empty() || !errorQueue.empty() || isExited;
        });

        // Write logs
        while (!accessQueue.empty()) {
            conf::accessLogHandle << accessQueue.front() << std::endl;
            accessQueue.pop();
        }

        while (!errorQueue.empty()) {
            conf::errorLogHandle << errorQueue.front() << std::endl;
            errorQueue.pop();
        }

        // Handle thread closure
        if (isExited) break;
    }
}

LoggerStream& LoggerStream::operator<<(StreamManipulator manip) {
    if (manip == static_cast<StreamManipulator>(std::endl)) {
        // Queue to logger
        const std::string log = genTimestamp() + buffer.str();
        if (isAccess) {
            logger.queueAccessLog(log);
        } else {
            logger.queueErrorLog(log);
        }

        buffer.str(""); // Wipe buffer
        buffer.clear(); // Clear error flags
    }
    return *this;
}

std::string genTimestamp() {
    // Cast timestamp
    using namespace std::chrono;
    auto tp = system_clock::to_time_t(system_clock::now());

    // Read output
    std::stringstream ss;
    ss << std::put_time(std::localtime(&tp), "[%m/%d/%y, %I:%M:%S %p] ");
    return ss.str();
}