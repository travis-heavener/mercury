#include "client.hpp"

#ifndef _WIN32
    #include <netdb.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

#include "../../conf/conf.hpp"
#include "../../logs/logger.hpp"
#include "../../util/string_tools.hpp"
#include "params.hpp"
#include "protocol.hpp"

// Define MSG_NOSIGNAL fallback for platforms without flag support
#ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0
#endif

namespace http::fastcgi {

    /*******************************************************************/
    /********************* ENDPOINT PARSER & HELPERS *******************/
    /*******************************************************************/

    #ifndef _WIN32
        // Used to parse PHP version tuple from socket filename (e.g. php8.3-fpm.sock -> (8, 3))
        static std::pair<int, int> extractPhpVersion(std::string_view filename) {
            // Locate PHP prefix marker
            size_t phpPos = filename.find("php");
            if (phpPos == std::string_view::npos) return {0, 0};

            // Locate dot separator following major version
            size_t start = phpPos + 3;
            size_t dotPos = filename.find('.', start);
            if (dotPos == std::string_view::npos) return {0, 0};

            int major = 0;
            int minor = 0;
            try {
                // Parse major and minor version numbers
                major = std::stoi(std::string(filename.substr(start, dotPos - start)));
                size_t end = dotPos + 1;
                while (end < filename.size() && std::isdigit(static_cast<unsigned char>(filename[end])))
                    ++end;
                if (end > dotPos + 1)
                    minor = std::stoi(std::string(filename.substr(dotPos + 1, end - (dotPos + 1))));
            } catch (...) {
                return {0, 0};
            }
            return {major, minor};
        }
    #endif

    // Used to detect default PHP FastCGI address, selecting highest PHP version on system
    std::string detectDefaultFastCGIAddress() {
        #ifndef _WIN32
            // Candidate directory paths containing PHP-FPM sockets on Linux distributions
            const std::string candidateDirs[] = {
                "/run/php",
                "/var/run/php",
                "/run/php-fpm",
                "/var/run/php-fpm"
            };

            std::vector<std::pair<std::pair<int, int>, std::string>> socketCandidates;

            // Scan candidate directories for active UNIX sockets
            for (const std::string& dirPath : candidateDirs) {
                std::error_code ec;
                if (!std::filesystem::exists(dirPath, ec) || !std::filesystem::is_directory(dirPath, ec))
                    continue;

                for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
                    if (ec) break;
                    std::string filename = entry.path().filename().string();
                    if (!filename.ends_with(".sock")) continue;

                    // Parse version tuple
                    std::pair<int, int> ver = extractPhpVersion(filename);
                    socketCandidates.push_back({ver, "unix:" + entry.path().string()});
                }
            }

            if (!socketCandidates.empty()) {
                // Sort descending so highest / most recent version comes first
                std::sort(socketCandidates.begin(), socketCandidates.end(),
                          [](const auto& a, const auto& b) {
                              return a.first > b.first;
                          });
                return socketCandidates.front().second;
            }

            // Check well-known distro socket paths without version numbers
            const std::string wellKnownSockets[] = {
                "/run/php/php-fpm.sock",
                "/run/php-fpm/www.sock",
                "/run/php-fpm/php-fpm.sock",
                "/var/run/php-fpm.sock"
            };
            for (const std::string& sockPath : wellKnownSockets) {
                std::error_code ec;
                if (std::filesystem::exists(sockPath, ec))
                    return "unix:" + sockPath;
            }
        #endif

        // Default TCP loopback fallback (universal on Windows and standalone FPM)
        return "127.0.0.1:9000";
    }

    // Used to parse FastCGI address string
    bool Endpoint::parse(std::string_view addrStr, Endpoint& out) {
        const bool wasEmpty = addrStr.empty();

        // Strip leading and trailing whitespace
        while (!addrStr.empty() && std::isspace(static_cast<unsigned char>(addrStr.front())))
            addrStr.remove_prefix(1);
        while (!addrStr.empty() && std::isspace(static_cast<unsigned char>(addrStr.back())))
            addrStr.remove_suffix(1);

        // Auto-resolve address if explicitly empty or set to "auto"
        std::string resolvedAddr;
        if (wasEmpty || addrStr == "auto") {
            resolvedAddr = detectDefaultFastCGIAddress();
            addrStr = resolvedAddr;
        }

        if (addrStr.empty()) return false;

        // Check for UNIX domain socket prefix or absolute path
        if (addrStr.starts_with("unix:")) {
            out.type = TransportType::UNIX_SOCKET;
            out.path = std::string(addrStr.substr(5));
            return !out.path.empty();
        }
        if (addrStr.starts_with("/")) {
            out.type = TransportType::UNIX_SOCKET;
            out.path = std::string(addrStr);
            return true;
        }

        // Check for IPv6 bracketed format [::1]:9000
        if (addrStr.starts_with("[")) {
            size_t closeBracket = addrStr.find(']');
            if (closeBracket == std::string_view::npos) return false;
            out.host = std::string(addrStr.substr(1, closeBracket - 1));
            out.type = TransportType::TCP;
            out.port = 9000;

            // Parse optional port suffix
            std::string_view rest = addrStr.substr(closeBracket + 1);
            if (rest.starts_with(":")) {
                rest.remove_prefix(1);
                try {
                    out.port = static_cast<uint16_t>(std::stoul(std::string(rest)));
                } catch (...) {
                    return false;
                }
            }
            return true;
        }

        // Standard IPv4 or hostname host:port
        size_t colonPos = addrStr.rfind(':');
        out.type = TransportType::TCP;
        if (colonPos != std::string_view::npos) {
            // Split host and explicit port
            out.host = std::string(addrStr.substr(0, colonPos));
            try {
                out.port = static_cast<uint16_t>(std::stoul(std::string(addrStr.substr(colonPos + 1))));
            } catch (...) {
                return false;
            }
        } else {
            // Default port when omitted
            out.host = std::string(addrStr);
            out.port = 9000;
        }
        return !out.host.empty();
    }

    // Used to infer Content-Type from response payload
    void inferContentType(std::string_view body, Response& res) {
        // Fallback to binary stream
        res.setContentType("application/octet-stream");
        if (body.empty()) return;

        // Inspect first non-whitespace indicator
        char first = body.front();
        if (first == '{' || first == '[') {
            res.setContentType("application/json");
            return;
        }
        if (first == '<') {
            // Distinguish HTML document from generic XML markup
            res.setContentType(body.find("<html") != std::string_view::npos ? "text/html" : "application/xml");
            return;
        }

        // Scan leading sample for printable ASCII characters
        size_t asciiCount = 0;
        size_t checkLen = std::min(body.size(), static_cast<size_t>(4096));
        for (size_t i = 0; i < checkLen; ++i) {
            unsigned char c = static_cast<unsigned char>(body[i]);
            if (c == '\t' || c == '\r' || c == '\n' || (c >= 32 && c <= 126))
                ++asciiCount;
        }

        // Classify as UTF-8 plain text if printable ratio exceeds 95%
        if (static_cast<double>(asciiCount) / checkLen >= 0.95)
            res.setContentType("text/plain; charset=utf-8");
    }

    // Used to assemble FastCGI environment parameters from File and Request
    void populateParams(ParamEncoder& encoder, const File& file, const Request& req) {
        // Parse HTTP Authorization header into credentials
        const std::optional<std::string> authHeader = req.getHeader("Authorization");
        if (authHeader.has_value()) {
            const size_t authSpaceInd = authHeader->find(' ');
            encoder.add("AUTH_TYPE", authHeader->substr(0, authSpaceInd));
            encoder.add("REMOTE_USER", authSpaceInd == std::string::npos ? "" : authHeader->substr(authSpaceInd + 1));
        } else {
            encoder.add("AUTH_TYPE", "");
            encoder.add("REMOTE_USER", "");
        }

        // Set payload length and type when request body exists
        const size_t contentLen = req.getBody().size();
        if (contentLen > 0) {
            encoder.add("CONTENT_LENGTH", std::to_string(contentLen));
            const std::optional<std::string> contentTypeHeader = req.getHeader("Content-Type");
            if (contentTypeHeader.has_value())
                encoder.add("CONTENT_TYPE", *contentTypeHeader);
        }

        // Populate standard CGI and script parameters
        encoder.add("GATEWAY_INTERFACE", "CGI/1.1");
        encoder.add("PATH_INFO", file.phpPathInfo);
        encoder.add("PATH_TRANSLATED", file.phpPathInfo.empty() ? "" : (conf::DOCUMENT_ROOT / file.phpPathInfo.substr(1)).string());
        encoder.add("QUERY_STRING", file.queryString);

        // Populate remote client identifiers
        encoder.add("REMOTE_ADDR", req.getIPStr());
        encoder.add("REMOTE_HOST", req.getIPStr());
        encoder.add("REMOTE_IDENT", "");

        // Map HTTP method (remap HEAD to GET for CGI execution)
        encoder.add("REQUEST_METHOD", req.getMethodStr() == "HEAD" ? "GET" : req.getMethodStr());
        encoder.add("REQUEST_URI", file.decodedURIWithoutPathInfo);

        // Map target script paths
        encoder.add("SCRIPT_FILENAME", file.absoluteResourcePath);
        encoder.add("SCRIPT_NAME", file.decodedURIWithoutPathInfo);

        // Populate server runtime context
        const std::optional<std::string> hostHeader = req.getHeader("Host");
        encoder.add("SERVER_NAME", hostHeader.has_value() ? *hostHeader : "");
        encoder.add("SERVER_PORT", std::to_string(req.usesHTTPS() ? conf::TLS_PORT : conf::PORT));
        encoder.add("SERVER_PROTOCOL", req.getVersion());
        encoder.add("SERVER_SOFTWARE", "Mercury/" + (conf::VERSION.size() > 9 ? conf::VERSION.substr(9) : conf::VERSION));

        // Set security and document root flags
        encoder.add("HTTPS", req.usesHTTPS() ? "1" : "");
        encoder.add("REDIRECT_STATUS", "200");
        encoder.add("DOCUMENT_ROOT", conf::DOCUMENT_ROOT.string());

        // Forward HTTP headers w/ HTTP_ prefix
        for (auto& [key, val] : req.getHeaders()) {
            std::string cgiKey = "HTTP_" + key;
            for (char& c : cgiKey) if (c == '-') c = '_';
            strToUpper(cgiKey);

            // Exclude headers mapped to dedicated CGI variables
            if (cgiKey != "HTTP_AUTHORIZATION" && cgiKey != "HTTP_CONTENT_LENGTH" && cgiKey != "HTTP_CONTENT_TYPE")
                encoder.add(cgiKey, val);
        }
    }

    /*******************************************************************/
    /********************* CLIENT IMPLEMENTATION ***********************/
    /*******************************************************************/

    // Initialize client instance w/ timeout limit
    Client::Client(int timeoutSeconds) : timeoutSeconds(timeoutSeconds) {}

    // Teardown client and release socket descriptor
    Client::~Client() {
        this->close();
    }

    // Close active socket connection
    void Client::close() {
        if (this->sock != FASTCGI_INVALID_SOCKET) {
            #ifdef _WIN32
                shutdown(this->sock, SD_BOTH);
            #else
                shutdown(this->sock, SHUT_RDWR);
            #endif
            fastcgiCloseSocket(this->sock);
            this->sock = FASTCGI_INVALID_SOCKET;
        }
    }

    // Verify socket connection status
    bool Client::isConnected() const {
        return this->sock != FASTCGI_INVALID_SOCKET;
    }

    // Check if previous socket operation timed out
    bool Client::didTimeout() const {
        return this->hasTimedOut;
    }

    // Connect to target FastCGI endpoint via UNIX domain socket or TCP
    bool Client::connect(const Endpoint& endpoint) {
        // Reset socket state prior to connection
        this->close();
        this->hasTimedOut = false;

        if (endpoint.type == TransportType::UNIX_SOCKET) {
            #ifdef _WIN32
                ERROR_LOG << "UNIX domain sockets are not supported on Windows: " << endpoint.path << std::endl;
                return false;
            #else
                // Open UNIX domain stream socket
                this->sock = socket(AF_UNIX, SOCK_STREAM, 0);
                if (this->sock < 0) {
                    ERROR_LOG << "Failed to open UNIX domain socket for FastCGI" << std::endl;
                    return false;
                }

                // Populate sockaddr_un path structure
                struct sockaddr_un sunAddr = {};
                sunAddr.sun_family = AF_UNIX;
                if (endpoint.path.size() >= sizeof(sunAddr.sun_path)) {
                    ERROR_LOG << "FastCGI UNIX socket path exceeds maximum length: " << endpoint.path << std::endl;
                    this->close();
                    return false;
                }
                std::strncpy(sunAddr.sun_path, endpoint.path.c_str(), sizeof(sunAddr.sun_path) - 1);

                // Establish connection to UNIX domain socket
                if (::connect(this->sock, reinterpret_cast<struct sockaddr*>(&sunAddr), sizeof(sunAddr)) < 0) {
                    ERROR_LOG << "Failed to connect to FastCGI UNIX socket (" << endpoint.path << "), errno: " << errno << std::endl;
                    this->close();
                    return false;
                }
            #endif
        } else {
            // TCP socket connection
            struct addrinfo hints = {};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            // Resolve target hostname and port
            struct addrinfo* pAddrInfo = nullptr;
            std::string portStr = std::to_string(endpoint.port);
            if (getaddrinfo(endpoint.host.c_str(), portStr.c_str(), &hints, &pAddrInfo) != 0 || pAddrInfo == nullptr) {
                ERROR_LOG << "Failed to resolve FastCGI host: " << endpoint.host << std::endl;
                return false;
            }

            // Iterate over resolved candidates until connected
            bool connected = false;
            for (struct addrinfo* pCur = pAddrInfo; pCur != nullptr; pCur = pCur->ai_next) {
                #ifdef _WIN32
                    this->sock = socket(pCur->ai_family, pCur->ai_socktype, pCur->ai_protocol);
                    if (this->sock == FASTCGI_INVALID_SOCKET) continue;
                    if (::connect(this->sock, pCur->ai_addr, static_cast<int>(pCur->ai_addrlen)) == 0) {
                        connected = true;
                        break;
                    }
                #else
                    this->sock = socket(pCur->ai_family, pCur->ai_socktype, pCur->ai_protocol);
                    if (this->sock < 0) continue;
                    if (::connect(this->sock, pCur->ai_addr, pCur->ai_addrlen) == 0) {
                        connected = true;
                        break;
                    }
                #endif
                this->close();
            }
            freeaddrinfo(pAddrInfo);

            if (!connected) {
                ERROR_LOG << "Failed to connect to FastCGI TCP endpoint: " << endpoint.host << ":" << endpoint.port << std::endl;
                return false;
            }

            // Set TCP_NODELAY
            int flag = 1;
            setsockopt(this->sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
        }

        // Set socket receive and send timeouts
        #ifdef _WIN32
            DWORD timeoutMs = static_cast<DWORD>(this->timeoutSeconds * 1000);
            setsockopt(this->sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
            setsockopt(this->sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        #else
            struct timeval tv;
            tv.tv_sec = this->timeoutSeconds;
            tv.tv_usec = 0;
            setsockopt(this->sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
            setsockopt(this->sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
        #endif

        return true;
    }

    // Transmit buffer across socket w/ polling timeout guard
    bool Client::sendAll(const void* pData, size_t length) {
        const uint8_t* pBytes = reinterpret_cast<const uint8_t*>(pData);
        size_t totalSent = 0;

        const auto startTime = std::chrono::steady_clock::now();
        const auto timeoutDuration = std::chrono::seconds(this->timeoutSeconds);

        // Loop until entire byte payload is transmitted
        while (totalSent < length) {
            // Verify remaining duration against timeout limit
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed >= timeoutDuration) {
                this->hasTimedOut = true;
                return false;
            }

            int remainingMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeoutDuration - elapsed).count());
            if (remainingMs <= 0) {
                this->hasTimedOut = true;
                return false;
            }

            // Poll socket for write readiness
            struct pollfd pfd;
            pfd.fd = this->sock;
            pfd.events = POLLOUT;
            pfd.revents = 0;

            int pollRc = poll(&pfd, 1, remainingMs);
            if (pollRc < 0) {
                #ifndef _WIN32
                    if (errno == EINTR) continue;
                #endif
                return false;
            }
            if (pollRc == 0) {
                this->hasTimedOut = true;
                return false; // Timeout
            }

            // Transmit pending byte slice
            #ifdef _WIN32
                int n = send(this->sock, reinterpret_cast<const char*>(pBytes + totalSent), static_cast<int>(length - totalSent), 0);
                if (n == FASTCGI_SOCKET_ERROR) return false;
            #else
                ssize_t n = send(this->sock, pBytes + totalSent, length - totalSent, MSG_NOSIGNAL);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    return false;
                }
            #endif

            if (n == 0) return false;
            totalSent += static_cast<size_t>(n);
        }
        return true;
    }

    // Read exact byte count into destination buffer w/ timeout guard
    bool Client::recvExact(void* pDest, size_t length) {
        uint8_t* pBytes = reinterpret_cast<uint8_t*>(pDest);
        size_t totalRead = 0;

        const auto startTime = std::chrono::steady_clock::now();
        const auto timeoutDuration = std::chrono::seconds(this->timeoutSeconds);

        // Loop until requested byte length is satisfied
        while (totalRead < length) {
            // Verify remaining duration against timeout limit
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (elapsed >= timeoutDuration) {
                this->hasTimedOut = true;
                return false;
            }

            int remainingMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeoutDuration - elapsed).count());
            if (remainingMs <= 0) {
                this->hasTimedOut = true;
                return false;
            }

            // Poll socket for read readiness
            struct pollfd pfd;
            pfd.fd = this->sock;
            pfd.events = POLLIN;
            pfd.revents = 0;

            int pollRc = poll(&pfd, 1, remainingMs);
            if (pollRc < 0) {
                #ifndef _WIN32
                    if (errno == EINTR) continue;
                #endif
                return false;
            }
            if (pollRc == 0) {
                this->hasTimedOut = true;
                return false; // Timeout
            }

            // Read available bytes into destination slice
            #ifdef _WIN32
                int n = recv(this->sock, reinterpret_cast<char*>(pBytes + totalRead), static_cast<int>(length - totalRead), 0);
                if (n == FASTCGI_SOCKET_ERROR) return false;
            #else
                ssize_t n = recv(this->sock, pBytes + totalRead, length - totalRead, 0);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    return false;
                }
            #endif

            if (n == 0) return false; // Connection closed prematurely
            totalRead += static_cast<size_t>(n);
        }
        return true;
    }

    // Discard specified number of bytes from stream
    bool Client::skipBytes(size_t length) {
        // Read into scratch buffer to drain socket
        uint8_t scratch[1024];
        size_t remaining = length;
        while (remaining > 0) {
            size_t toRead = std::min(remaining, sizeof(scratch));
            if (!this->recvExact(scratch, toRead)) return false;
            remaining -= toRead;
        }
        return true;
    }

    // Transmit FastCGI request records to backend
    bool Client::sendRequest(uint16_t requestId, const ParamEncoder& params, std::string_view body) {
        // Send FCGI_BEGIN_REQUEST
        FCGI_BeginRequestRecord beginRec = makeBeginRequestRecord(requestId, FCGI_RESPONDER, 0);
        if (!this->sendAll(&beginRec, sizeof(beginRec))) return false;

        // Send FCGI_PARAMS stream (includes terminator)
        std::vector<uint8_t> paramRecords = params.buildRecords(requestId, true);
        if (!this->sendAll(paramRecords.data(), paramRecords.size())) return false;

        // Send FCGI_STDIN body
        if (body.empty()) {
            FCGI_Header stdinTerm = makeHeader(FCGI_STDIN, requestId, 0, 0);
            if (!this->sendAll(&stdinTerm, sizeof(stdinTerm))) return false;
        } else {
            // Stream body payload in aligned record chunks
            size_t offset = 0;
            const size_t bodySize = body.size();
            while (offset < bodySize) {
                size_t chunkSize = std::min(bodySize - offset, FCGI_MAX_ALIGNED_CHUNK);
                uint8_t padding = calculatePadding(chunkSize);

                FCGI_Header header = makeHeader(FCGI_STDIN, requestId, static_cast<uint16_t>(chunkSize), padding);
                if (!this->sendAll(&header, sizeof(header))) return false;
                if (!this->sendAll(body.data() + offset, chunkSize)) return false;

                // Send alignment padding bytes
                if (padding > 0) {
                    uint8_t padBuf[8] = {0};
                    if (!this->sendAll(padBuf, padding)) return false;
                }
                offset += chunkSize;
            }

            // Send empty STDIN terminator
            FCGI_Header stdinTerm = makeHeader(FCGI_STDIN, requestId, 0, 0);
            if (!this->sendAll(&stdinTerm, sizeof(stdinTerm))) return false;
        }

        return true;
    }

    // Receive and parse FastCGI response stream from backend
    bool Client::receiveResponse(uint16_t requestId, Response& res) {
        (void)requestId;
        std::string stdoutBuffer;
        std::string stderrBuffer;
        bool requestEnded = false;
        uint32_t appStatus = 0;
        uint8_t protocolStatus = FCGI_REQUEST_COMPLETE;

        // Read incoming FastCGI records until stream terminates
        while (!requestEnded) {
            FCGI_Header header;
            if (!this->recvExact(&header, sizeof(header))) {
                ERROR_LOG << "Failed to read FastCGI record header" << std::endl;
                if (this->hasTimedOut)
                    res.setStatus(504);
                return false;
            }

            // Verify FastCGI protocol version
            if (header.version != FCGI_VERSION_1) {
                ERROR_LOG << "Invalid FastCGI protocol version: " << static_cast<int>(header.version) << std::endl;
                return false;
            }

            uint16_t recRequestId = getRequestId(header);
            (void)recRequestId;
            uint16_t cLen = getContentLength(header);
            uint8_t pLen = header.paddingLength;

            // Route record payload according to stream type
            if (header.type == FCGI_STDOUT) {
                // Buffer standard output stream
                if (cLen > 0) {
                    size_t prevSize = stdoutBuffer.size();
                    stdoutBuffer.resize(prevSize + cLen);
                    if (!this->recvExact(stdoutBuffer.data() + prevSize, cLen)) return false;
                }
                // Discard padding bytes
                if (pLen > 0 && !this->skipBytes(pLen)) return false;
            } else if (header.type == FCGI_STDERR) {
                // Buffer standard error stream
                if (cLen > 0) {
                    size_t prevSize = stderrBuffer.size();
                    stderrBuffer.resize(prevSize + cLen);
                    if (!this->recvExact(stderrBuffer.data() + prevSize, cLen)) return false;
                }
                // Discard padding bytes
                if (pLen > 0 && !this->skipBytes(pLen)) return false;
            } else if (header.type == FCGI_END_REQUEST) {
                // Parse application exit status and completion code
                FCGI_EndRequestBody endBody;
                if (cLen >= sizeof(endBody)) {
                    if (!this->recvExact(&endBody, sizeof(endBody))) return false;
                    appStatus = getAppStatus(endBody);
                    protocolStatus = endBody.protocolStatus;
                    size_t remainingContent = cLen - sizeof(endBody);
                    if (remainingContent > 0 && !this->skipBytes(remainingContent)) return false;
                } else if (cLen > 0) {
                    if (!this->skipBytes(cLen)) return false;
                }
                // Discard padding bytes
                if (pLen > 0 && !this->skipBytes(pLen)) return false;
                requestEnded = true;
            } else {
                // Drain unhandled record types and padding
                if (cLen > 0 && !this->skipBytes(cLen)) return false;
                if (pLen > 0 && !this->skipBytes(pLen)) return false;
            }
        }

        // Route stderr output to error log
        if (!stderrBuffer.empty()) {
            ERROR_LOG << "FastCGI STDERR: " << stderrBuffer << std::endl;
        }

        // Map protocol completion statuses to HTTP status codes
        if (protocolStatus == FCGI_OVERLOADED) {
            res.setStatus(503);
            return true;
        }
        if (protocolStatus != FCGI_REQUEST_COMPLETE) {
            res.setStatus(502);
            return true;
        }
        if (appStatus != 0 && stdoutBuffer.empty()) {
            res.setStatus(500);
            return true;
        }

        // Parse CGI headers and body from stdout
        size_t headerEnd = stdoutBuffer.find("\r\n\r\n");
        size_t bodyStart = 0;
        bool hasHeaders = false;

        // Identify header block termination
        if (headerEnd != std::string::npos) {
            bodyStart = headerEnd + 4;
            hasHeaders = true;
        } else {
            headerEnd = stdoutBuffer.find("\n\n");
            if (headerEnd != std::string::npos) {
                bodyStart = headerEnd + 2;
                hasHeaders = true;
            }
        }

        res.setStatus(200);

        // Parse individual CGI header lines
        if (hasHeaders) {
            std::string_view headersBlock(stdoutBuffer.data(), headerEnd);
            size_t lineStart = 0;

            while (lineStart < headersBlock.size()) {
                size_t lineEnd = headersBlock.find('\n', lineStart);
                if (lineEnd == std::string_view::npos) lineEnd = headersBlock.size();

                std::string_view line = headersBlock.substr(lineStart, lineEnd - lineStart);
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);

                lineStart = lineEnd + 1;
                if (line.empty()) continue;

                // Split key-value pair and sanitize header names
                size_t colon = line.find(':');
                if (colon != std::string_view::npos) {
                    std::string key(line.substr(0, colon));
                    std::string val(line.substr(colon + 1));
                    trimString(key);
                    trimString(val);
                    formatHeaderCasing(key);

                    // Extract CGI Status override or apply header field
                    if (key == "Status") {
                        try {
                            res.setStatus(static_cast<uint16_t>(std::stoul(val)));
                        } catch (...) {
                            res.setStatus(200);
                        }
                    } else {
                        try {
                            res.setHeader(key, val);
                        } catch (...) { /* Ignore invalid header from CGI script */ }
                    }
                }
            }
        }

        // Extract response body payload
        std::string body = stdoutBuffer.substr(bodyStart);
        const size_t bodySize = body.size();

        res.setHeader("Content-Length", std::to_string(bodySize));

        // Infer Content-Type or handle empty response payload
        if (bodySize == 0) {
            if (res.getStatus() == 200 && stdoutBuffer.empty())
                res.setStatus(204);
            res.clearHeader("Content-Type");
        } else if (res.getContentType() == "text/html; charset=UTF-8" || res.getContentType().empty()) {
            inferContentType(body, res);
        }

        // Attach response payload stream
        res.setBodyStream(std::make_unique<MemoryStream>(std::move(body)));
        return true;
    }

    // Execute complete request-response lifecycle against FastCGI backend
    bool Client::execute(const Endpoint& endpoint, const File& file, const Request& req, Response& res) {
        // Connect to target backend endpoint
        if (!this->connect(endpoint)) {
            res.setStatus(502);
            return false;
        }

        // Encode CGI environment parameters
        ParamEncoder params;
        populateParams(params, file, req);

        // Transmit request records
        uint16_t requestId = FCGI_DEFAULT_REQUEST_ID;
        if (!this->sendRequest(requestId, params, req.getBody())) {
            ERROR_LOG << "Failed to transmit FastCGI request" << std::endl;
            res.setStatus(504);
            this->close();
            return false;
        }

        // Await and parse server response
        if (!this->receiveResponse(requestId, res)) {
            ERROR_LOG << "Failed to receive FastCGI response" << std::endl;
            if (this->hasTimedOut)
                res.setStatus(504);
            else if (res.getStatus() == 200)
                res.setStatus(502);
            this->close();
            return false;
        }

        // Close client connection and release socket
        this->close();
        return true;
    }

    // Used to parse endpoint and execute FastCGI request
    bool handleRequest(const std::string& endpointStr, const File& file, const Request& req, Response& res, int timeoutSeconds) {
        // Parse target FastCGI endpoint configuration
        Endpoint endpoint;
        if (!Endpoint::parse(endpointStr, endpoint)) {
            ERROR_LOG << "Invalid FastCGI endpoint specification: " << endpointStr << std::endl;
            res.setStatus(502);
            return false;
        }

        // Instantiate client and process request
        Client client(timeoutSeconds);
        return client.execute(endpoint, file, req, res);
    }

}