#include "logger.hpp"

#ifdef _WIN32
    #include "../winheader.hpp"
#else
    #include <arpa/inet.h>
#endif

#include <cstring>
#include <iomanip>
#include <openssl/hmac.h>
#include <sstream>

#include "../conf/conf.hpp"

Logger::Logger() : isExited(false) {
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

// Helper method for formatting client IPs based on the ClientSecurityMode config node
std::string formatClientIP(const std::string& ipStr, const bool isDNT) {
    const int secMode = conf::CLIENT_SECURITY_MODE;

    // Check if IP needs to be masked
    if (secMode == CLIENT_SEC_MASKED || ( secMode == CLIENT_SEC_GPC_MASKED && isDNT )) {
        // Mask lower bits
        char buffer[INET6_ADDRSTRLEN] = {0};
        conf::SanitizedIP sip = conf::parseSanitizedClientIP( ipStr );

        if (sip.family == conf::IPFamily::IPv4) { // Mask lowest 8 bits
            sip.bytes[3] = 0;
            inet_ntop(AF_INET, sip.bytes, buffer, INET6_ADDRSTRLEN);
        } else { // Mask lowest 80 bits
            std::memset(sip.bytes + 6, 0, 10);
            inet_ntop(AF_INET6, sip.bytes, buffer, INET6_ADDRSTRLEN);
        }

        // Return new string
        return std::string(buffer);
    }

    // Check if IP needs to be anonymized
    if (secMode == CLIENT_SEC_HASHED || ( secMode == CLIENT_SEC_GPC_HASHED && isDNT )) {
        // Anonymize (hash) all requests
        conf::SanitizedIP sip = conf::parseSanitizedClientIP( ipStr );
        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned int len = 0;

        size_t byteLen = (sip.family == conf::IPFamily::IPv4) ? 4 : 16;

        // Apply HMAC hash
        HMAC(
            EVP_sha256(),
            conf::IP_HASH_SALT.data(), conf::IP_HASH_SALT.size(),
            sip.bytes, byteLen,
            result, &len
        );

        // Convert to hex
        std::ostringstream oss;
        for (unsigned int i = 0; i < len; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(result[i]);
        }

        // Return hash
        return oss.str();
    }

    // Otherwise, cleartext
    return ipStr;
}
