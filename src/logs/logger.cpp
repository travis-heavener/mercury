#include "logger.hpp"

#ifdef _WIN32
    #include "../winheader.hpp"
#else
    #include <arpa/inet.h>
#endif

#include <charconv>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <openssl/hmac.h>
#include <sstream>

#include "../conf/conf.hpp"
#include "../http/message/request.hpp"

Logger::Logger() : isExited(false) {
    this->thread = std::thread(&Logger::threadWrite, this);
}

Logger::~Logger() {
    stop();
}

void Logger::stop() {
    std::lock_guard<std::mutex> stopLock(stopMutex);
    // Join & close thread
    {
        std::lock_guard<std::mutex> lock(writeMutex);
        isExited = true;
    }

    // Drain queued records before closing the log handles below.
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
    std::queue<std::string> localAccess;
    std::queue<std::string> localError;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(writeMutex);
            cv.wait(lock, [this]() {
                return !accessQueue.empty() || !errorQueue.empty() || isExited;
            });

            // Swap queues under lock
            std::swap(localAccess, accessQueue);
            std::swap(localError, errorQueue);
        }

        // Write logs outside writeMutex
        while (!localAccess.empty()) {
            if (conf::accessLogHandle.is_open())
                conf::accessLogHandle << localAccess.front() << '\n';
            localAccess.pop();
        }
        if (conf::accessLogHandle.is_open())
            conf::accessLogHandle.flush();

        while (!localError.empty()) {
            if (conf::errorLogHandle.is_open())
                conf::errorLogHandle << localError.front() << '\n';
            localError.pop();
        }
        if (conf::errorLogHandle.is_open())
            conf::errorLogHandle.flush();

        // Handle thread closure
        if (isExited) break;
    }
}

LoggerStream& LoggerStream::operator<<(StreamManipulator manip) {
    if (conf::DISABLE_LOGS) return *this;
    if (manip == static_cast<StreamManipulator>(std::endl)) {
        if (isAccess && conf::ACCESS_LOG_FORMAT == LOG_FORMAT_JSON) {
            std::string log = "{\"timestamp\":\"" + genIso8601Timestamp() + "\",\"level\":\"info\",\"event\":\"message\",\"message\":\"" + escapeJsonString(buffer.str()) + "\"}";
            logger.queueAccessLog(std::move(log));
        } else if (!isAccess && conf::ERROR_LOG_FORMAT == LOG_FORMAT_JSON) {
            std::string log = "{\"timestamp\":\"" + genIso8601Timestamp() + "\",\"level\":\"error\",\"event\":\"error\",\"message\":\"" + escapeJsonString(buffer.str()) + "\"}";
            logger.queueErrorLog(std::move(log));
        } else {
            // Queue to logger
            std::string log = genTimestamp() + buffer.str();
            if (isAccess) {
                logger.queueAccessLog(std::move(log));
            } else {
                logger.queueErrorLog(std::move(log));
            }
        }

        buffer.str(""); // Wipe buffer
        buffer.clear(); // Clear error flags
    }
    return *this;
}

// Append escaped JSON, preserving valid UTF-8 and encoding invalid bytes as Unicode escapes
static void appendJsonString(std::string& out, std::string_view input) {
    constexpr char HEX[] = "0123456789abcdef";
    auto escapeByte = [&](unsigned char c) {
        out += "\\u00";
        out += HEX[c >> 4];
        out += HEX[c & 15];
    };
    for (size_t i = 0; i < input.size(); ++i) {
        const auto c = static_cast<unsigned char>(input[i]);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    escapeByte(c);
                } else if (c < 0x80) {
                    out += static_cast<char>(c);
                } else {
                    // Reject overlong sequences, surrogates, and code points above U+10FFFF
                    const size_t length = c >= 0xc2 && c <= 0xdf ? 2 :
                                          c >= 0xe0 && c <= 0xef ? 3 :
                                          c >= 0xf0 && c <= 0xf4 ? 4 : 0;
                    bool valid = length != 0 && length <= input.size() - i;
                    for (size_t j = 1; valid && j < length; ++j) {
                        const auto next = static_cast<unsigned char>(input[i + j]);
                        valid = next >= 0x80 && next <= 0xbf;
                        if (j == 1) {
                            if (c == 0xe0 && next < 0xa0) valid = false;
                            if (c == 0xed && next >= 0xa0) valid = false;
                            if (c == 0xf0 && next < 0x90) valid = false;
                            if (c == 0xf4 && next >= 0x90) valid = false;
                        }
                    }

                    // Append character
                    if (valid) {
                        out.append(input.substr(i, length));
                        i += length - 1;
                    } else {
                        escapeByte(c);
                    }
                }
                break;
        }
    }
}

// Escape JSON string contents without surrounding quotes
std::string escapeJsonString(std::string_view input) {
    std::string out;

    // Reserve input size plus small allowance for escaping
    out.reserve(input.size() + 16);
    appendJsonString(out, input);
    return out;
}

// Generate RFC 3339 / ISO 8601 UTC timestamp with millisecond precision
std::string genIso8601Timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = system_clock::to_time_t(now);
    std::tm gmt{};
    #ifdef _WIN32
        gmtime_s(&gmt, &timer);
    #else
        gmtime_r(&timer, &gmt);
    #endif
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  gmt.tm_year + 1900, gmt.tm_mon + 1, gmt.tm_mday,
                  gmt.tm_hour, gmt.tm_min, gmt.tm_sec,
                  static_cast<int>(ms.count()));
    return std::string(buf);
}

// Emit structured JSON access log entry
void logJsonAccess(const std::string& method, const std::string& clientIP, const std::string& uri, int status, const std::string& version, double latencyMs, size_t bytesSent, const std::string& userAgent, const std::string& referer) {
    if (conf::DISABLE_LOGS) return;

    // Reserve space for fields and typical JSON overhead
    std::string log;
    log.reserve(256 + clientIP.size() + method.size() + uri.size() + version.size() + userAgent.size() + referer.size());

    // Start entry with timestamp
    log = "{\"timestamp\":\"";
    log += genIso8601Timestamp();
    log += status >= 500 ? "\",\"level\":\"error\",\"event\":\"request" :
           status >= 400 ? "\",\"level\":\"warn\",\"event\":\"request" :
                           "\",\"level\":\"info\",\"event\":\"request";

    // Close preceding string and append escaped field
    auto field = [&](std::string_view key, std::string_view value) {
        log += "\",\"";
        log += key;
        log += "\":\"";
        appendJsonString(log, value);
    };
    field("client_ip", clientIP);
    field("method", method);
    field("uri", uri);

    // Append status and HTTP version
    log += "\",\"status\":";
    log += std::to_string(status);
    log += ",\"http_version\":\"";
    appendJsonString(log, version);
    log += "\",\"latency_ms\":";

    // Format finite latency without locale-dependent decimal separators
    const double validLatency = (std::isfinite(latencyMs) && latencyMs >= 0.0) ? latencyMs : 0.0;
    char number[64];
    if (validLatency <= static_cast<double>(std::numeric_limits<uint64_t>::max() / 100)) {
        const auto totalHundredths = static_cast<uint64_t>(validLatency * 100.0 + 0.5);
        const auto whole = totalHundredths / 100;
        const auto frac = totalHundredths % 100;
        const auto wholeResult = std::to_chars(number, number + sizeof(number) - 4, whole);
        if (wholeResult.ec == std::errc()) {
            char* ptr = wholeResult.ptr;
            *ptr++ = '.';
            *ptr++ = static_cast<char>('0' + (frac / 10));
            *ptr++ = static_cast<char>('0' + (frac % 10));
            log.append(number, ptr);
        } else {
            log += "0.00";
        }
    } else {
        log += "0.00";
    }

    // Append remaining escaped fields and queue entry
    log += ",\"bytes_sent\":" + std::to_string(bytesSent);
    log += ",\"user_agent\":\"";
    appendJsonString(log, userAgent);
    field("referer", referer);
    log += "\"}";
    Logger::getInstance().queueAccessLog(std::move(log));
}

// Emit structured privilege drop event
void logJsonPrivilegeDrop(std::string_view user, unsigned long long uid, unsigned long long gid) {
    if (conf::DISABLE_LOGS) return;
    const std::string message = "Dropped root privileges to user " + std::string(user) + " (UID: " + std::to_string(uid) + ", GID: " + std::to_string(gid) + ").";
    std::string log = "{\"timestamp\":\"" + genIso8601Timestamp() + "\",\"level\":\"info\",\"event\":\"privilege_drop\",\"user\":\"";
    appendJsonString(log, user);
    log += "\",\"uid\":" + std::to_string(uid) + ",\"gid\":" + std::to_string(gid) + ",\"message\":\"";
    appendJsonString(log, message);
    log += "\"}";
    Logger::getInstance().queueAccessLog(std::move(log));
}

// Emit structured FastCGI startup event
void logJsonFastcgiStart(std::string_view address) {
    if (conf::DISABLE_LOGS) return;
    std::string log = "{\"timestamp\":\"" + genIso8601Timestamp() + "\",\"level\":\"info\",\"event\":\"fastcgi_start\",\"socket\":\"";
    appendJsonString(log, address);
    log += "\",\"message\":\"FastCGI: started private PHP-FPM at ";
    appendJsonString(log, address);
    log += "\"}";
    Logger::getInstance().queueAccessLog(std::move(log));
}

// Emit structured shutdown event
void logJsonShutdown() {
    if (conf::DISABLE_LOGS) return;
    Logger::getInstance().queueAccessLog("{\"timestamp\":\"" + genIso8601Timestamp() + "\",\"level\":\"info\",\"event\":\"shutdown\",\"message\":\"Process killed successfully.\"}");
}

// Emit structured listener event
void logJsonListen(unsigned int port, bool ipv4, bool tls, std::string_view message) {
    if (conf::DISABLE_LOGS) return;
    std::string log = "{\"timestamp\":\"" + genIso8601Timestamp() + "\",\"level\":\"info\",\"event\":\"listen\",\"port\":" + std::to_string(port);
    log += ipv4 ? ",\"protocol\":\"tcp4\"" : ",\"protocol\":\"tcp6\"";
    log += tls ? ",\"tls\":true,\"message\":\"" : ",\"tls\":false,\"message\":\"";
    appendJsonString(log, message);
    log += "\"}";
    Logger::getInstance().queueAccessLog(std::move(log));
}

// Emit structured startup event
void logJsonStartup(std::string_view version, std::string_view message) {
    if (conf::DISABLE_LOGS) return;
    if (version.starts_with("Mercury v")) version.remove_prefix(9);
    std::string log = "{\"timestamp\":\"" + genIso8601Timestamp() + "\",\"level\":\"info\",\"event\":\"startup\",\"version\":\"";
    appendJsonString(log, version);
    log += "\",\"message\":\"";
    appendJsonString(log, message);
    log += "\"}";
    Logger::getInstance().queueAccessLog(std::move(log));
}

// Extract request metadata and emit structured JSON access log
void logJsonAccess(const http::Request& request, int status, double latencyMs, size_t bytesSent) {
    if (conf::DISABLE_LOGS) return;

    std::string clientIP = formatClientIP(request.getIPStr(), request.isDNT());
    std::string method = request.getMethodStr();
    const std::string& uri = request.getPaths().rawPathFromRequest;
    const std::string& version = request.getVersion();
    std::string userAgent = request.getHeader("User-Agent").value_or("");
    std::string referer = request.getHeader("Referer").value_or("");

    logJsonAccess(method, clientIP, uri, status, version, latencyMs, bytesSent, userAgent, referer);
}

std::string genTimestamp() {
    // Cast timestamp
    using namespace std::chrono;
    auto tp = system_clock::to_time_t(system_clock::now());

    // Read output
    std::stringstream ss;
    std::tm localTime{};
    #ifdef _WIN32
        localtime_s(&localTime, &tp);
    #else
        localtime_r(&tp, &localTime);
    #endif
    ss << std::put_time(&localTime, "[%m/%d/%y, %I:%M:%S %p] ");
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
