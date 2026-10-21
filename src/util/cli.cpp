#include "cli.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "../conf/conf.hpp"
#include "../http/stats.hpp"
#include "../http/tls.hpp"

#ifdef _WIN32
    // Only used in Windows builds for canonicalizing the path to the PHP init script
    #include "../io/file_tools.hpp"
#endif

#define SLEEP_BETWEEN_CLI std::this_thread::sleep_for(std::chrono::milliseconds(200))

#define MAX_HISTORY_LEN 50
#define KEY_UP    922
#define KEY_DOWN  923
#define KEY_LEFT  924
#define KEY_RIGHT 925
#define KEY_DELETE 926

// Enables raw terminal input mode
#ifdef __linux__
    int getKey() {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1) return -1; // No input

        if (c == '\x1b') { // escape sequence
            unsigned char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
            if (seq[0] == '[') {
                if (seq[1] >= 'A' && seq[1] <= 'D') {
                    switch (seq[1]) {
                        case 'A': return KEY_UP;
                        case 'B': return KEY_DOWN;
                        case 'C': return KEY_RIGHT;
                        case 'D': return KEY_LEFT;
                    }
                } else if (seq[1] == '3') {
                    read(STDIN_FILENO, &seq[2], 1); // Should be '~'
                    return KEY_DELETE;
                }
            }
            return '\x1b';
        }

        return c;
    }

    // Updates the current line
    void showLine(const std::string& line) {
        std::cout << "\33[2K\r< " << line << std::flush;
    }

    // Gets the next line from the terminal
    void readNextLine(std::atomic<bool>& isExiting, std::vector<std::string>& history, int& historyIndex) {
        if (history.size() > MAX_HISTORY_LEN) history.erase(history.begin());

        // Push empty 
        history.push_back("");
        historyIndex = static_cast<int>(history.size()) - 1;

        int c, cursor = 0;
        while (!isExiting) {
            if ((c = getKey()) == -1) continue;

            switch (c) {
                case 3: // Ctrl+C (SIGINT)
                    std::cout << '\n' << std::flush;
                    isExiting = true;
                    return;
                case 12: // Ctrl+L (clear terminal and redraw line)
                    std::cout << "\033[2J\033[H" << std::flush;
                    showLine(history[historyIndex]);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
                case '\n': // Update history
                    if (historyIndex != static_cast<int>(history.size()) - 1)
                        history.pop_back();
                    std::cout << '\n' << std::flush;
                    return;
                case KEY_UP: // Previous command
                    if (historyIndex == 0) break;
                    showLine(history[--historyIndex]);
                    cursor = history[historyIndex].size();
                    break;
                case KEY_DOWN: // Next command
                    if (historyIndex >= static_cast<int>(history.size()) - 1) break;
                    showLine(history[++historyIndex]);
                    cursor = history[historyIndex].size();
                    break;
                case KEY_RIGHT:
                    if (cursor == static_cast<int>(history[historyIndex].size())) break;
                    ++cursor;
                    std::cout << "\033[1C" << std::flush;
                    break;
                case KEY_LEFT:
                    if (cursor == 0) break;
                    --cursor;
                    std::cout << "\033[1D" << std::flush;
                    break;
                case KEY_DELETE:
                    if (history[historyIndex].empty() || cursor >= static_cast<int>(history[historyIndex].size())) break;
                    history[historyIndex].erase(history[historyIndex].cbegin() + cursor);
                    showLine(history[historyIndex]);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
                case 127: // Backspace
                    if (history[historyIndex].empty()) break;
                    history[historyIndex].erase(history[historyIndex].cbegin() + cursor - 1);
                    --cursor;
                    showLine(history[historyIndex]);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
                default:
                    if (!isprint(c)) break;
                    history[historyIndex].insert(history[historyIndex].cbegin() + cursor, c);
                    ++cursor;
                    showLine(history[historyIndex]);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
            }
        }
    }
#endif

// Used to format boolean as on/off string
static const char* formatOnOff(const bool b) {
    return b ? "on" : "off";
}

// Used to format byte count into compact human-readable string
static std::string formatByteSize(const size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int idx = 0;
    double val = static_cast<double>(bytes);
    while (val >= 1024.0 && idx < 3) {
        val /= 1024.0;
        ++idx;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(idx == 0 ? 0 : 2) << val << ' ' << units[idx];
    return oss.str();
}

// Used to stringify client security mode
static const char* formatSecurityMode(const int mode) {
    switch (mode) {
        case CLIENT_SEC_MINIMAL:    return "Minimal";
        case CLIENT_SEC_GPC_MASKED: return "GPCMasked";
        case CLIENT_SEC_GPC_HASHED: return "GPCHashed";
        case CLIENT_SEC_MASKED:     return "Masked";
        case CLIENT_SEC_HASHED:     return "Hashed";
        default:                    return "Unknown";
    }
}

// Used to read and print last N lines of log file safely
static void readLogTail(const std::filesystem::path& path, size_t lineCount) {
    if (path.empty()) {
        std::cout << "> Log file is not configured" << std::endl;
        return;
    }

    // Clamp line count within safe bounds
    if (lineCount == 0) lineCount = 10;
    if (lineCount > 1000) lineCount = 1000;

    // Open log file in binary mode positioned at end
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "> Failed to open log file (" << path.string() << ")" << std::endl;
        return;
    }

    // Check for empty log file
    const std::streampos fileSize = file.tellg();
    if (fileSize <= 0) {
        std::cout << "> Log file is empty (" << path.string() << ")" << std::endl;
        return;
    }

    // Scan backwards from end of file for newlines
    size_t linesFound = 0;
    std::streampos pos = fileSize;
    constexpr size_t CHUNK_SIZE = 4096;
    std::vector<char> buffer(CHUNK_SIZE);

    // Skip trailing newline if present to prevent blank line
    file.seekg(pos - std::streampos(1));
    char lastChar = 0;
    if (file.get(lastChar) && lastChar == '\n') {
        pos -= std::streampos(1);
        if (pos > 0) {
            file.seekg(pos - std::streampos(1));
            if (file.get(lastChar) && lastChar == '\r')
                pos -= std::streampos(1);
        }
    }
    file.clear();

    // Read chunks backwards until required line count is found
    while (pos > 0 && linesFound < lineCount) {
        const size_t bytesToRead = (pos >= static_cast<std::streampos>(CHUNK_SIZE))
            ? CHUNK_SIZE : static_cast<size_t>(pos);
        pos -= static_cast<std::streampos>(bytesToRead);
        file.clear();
        file.seekg(pos);
        file.read(buffer.data(), bytesToRead);
        const size_t bytesRead = file.gcount();

        // Count newlines from right to left
        for (size_t i = bytesRead; i > 0; --i) {
            if (buffer[i - 1] == '\n') {
                if (++linesFound == lineCount) {
                    pos += static_cast<std::streampos>(i);
                    break;
                }
            }
        }
    }

    // Seek to calculated file offset and output lines
    file.clear();
    file.seekg(pos);
    std::string line;
    size_t printed = 0;
    std::cout << "> Log output (" << path.string() << "):\n";
    while (std::getline(file, line) && printed < lineCount) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::cout << "  " << line << '\n';
        ++printed;
    }
    std::cout << std::flush;
}

void handleCLICommands(const std::string& buf, std::atomic<bool>& isExiting, std::vector<std::shared_ptr<http::Server>>& serversVec) {
    // Separate command and arguments
    std::string cmd = buf;
    std::string args;
    const size_t spacePos = buf.find(' ');
    if (spacePos != std::string::npos) {
        cmd = buf.substr(0, spacePos);
        args = buf.substr(spacePos + 1);
        trimString(args);
    }

    // Handle terminal clear
    if (cmd == "CLEAR" || cmd == "CLS") {
        std::cout << "\033[2J\033[H" << std::flush;
    } else if (cmd == "CONFIG" || cmd == "CONF") {
        // Format index files into comma-separated list
        std::string indexFilesStr;
        for (size_t i = 0; i < conf::INDEX_FILES.size(); ++i) {
            if (i > 0) indexFilesStr += ", ";
            indexFilesStr += conf::INDEX_FILES[i];
        }

        std::cout << "> Active Server Configuration:\n"
                  << "  [Core & Network]\n"
                  << "    DocumentRoot:             " << conf::DOCUMENT_ROOT.string() << '\n'
                  << "    HTTP Port:                " << conf::PORT
                  << " (IPv4: " << formatOnOff(conf::IS_IPV4_ENABLED)
                  << ", IPv6: " << formatOnOff(conf::IS_IPV6_ENABLED) << ")\n"
                  << "    TLS Port:                 " << conf::TLS_PORT
                  << " (TLS: " << formatOnOff(conf::USE_TLS) << ")\n"
                  << "    Legacy HTTP (1.0/0.9):    " << formatOnOff(conf::ENABLE_LEGACY_HTTP) << "\n"
                  << "  [Connection & Limits]\n"
                  << "    Keep-Alive:               " << formatOnOff(conf::IS_KEEP_ALIVE_ENABLED)
                  << " (timeout: " << conf::KEEP_ALIVE_TIMEOUT << "s, max: " << conf::MAX_KEEP_ALIVE_REQUESTS << " reqs)\n"
                  << "    Max Backlog:              " << conf::MAX_REQUEST_BACKLOG << '\n'
                  << "    Max Request Line:         " << conf::MAX_REQUEST_LINE_LENGTH << " B\n"
                  << "    Buffer Size (Req/Res):    " << formatByteSize(conf::REQUEST_BUFFER_SIZE)
                  << " / " << formatByteSize(conf::RESPONSE_BUFFER_SIZE) << '\n'
                  << "    Max Body Size (Req/Res):  " << formatByteSize(conf::MAX_REQUEST_BODY)
                  << " / " << formatByteSize(conf::MAX_RESPONSE_BODY) << '\n'
                  << "    Min Compression Size:     " << conf::MIN_COMPRESSION_SIZE << " B\n"
                  << "  [Content & CGI]\n"
                  << "    Index Files:              " << (indexFilesStr.empty() ? "(None)" : indexFilesStr) << '\n'
                  << "    Custom Status Docs:       " << formatOnOff(conf::ENABLE_CUSTOM_STATUS_DOCS) << '\n'
                  << "    PHP-CGI:                  " << formatOnOff(conf::IS_PHP_ENABLED) << '\n'
                  << "  [Security & Logging]\n"
                  << "    Client Security Mode:     " << formatSecurityMode(conf::CLIENT_SECURITY_MODE) << '\n'
                  << "    Access Log File:          " << conf::ACCESS_LOG_FILE.string() << '\n'
                  << "    Error Log File:           " << conf::ERROR_LOG_FILE.string()
                  << std::endl;
    } else if (cmd == "DONATE") {
        std::cout << "> Love Mercury? Consider supporting this project:\n"
            "     https://buymeacoffee.com/travis.heavener" << std::endl;
    } else if (cmd == "EXIT" || cmd == "QUIT" || cmd == "Q") {
        isExiting.store(true);
    } else if (cmd == "STATS" || cmd == "METRICS" || cmd == "STATUS" || cmd == "INFO" || cmd == "UPTIME") {
        // Aggregate thread pool and active connection metrics
        size_t usedThreads = 0, totalThreads = 0, pendingConnections = 0;
        size_t activeClients = 0;
        for (auto& pServer : serversVec) {
            if (!pServer) continue;
            pServer->getUsageInfo(usedThreads, totalThreads, pendingConnections);
            activeClients += pServer->getActiveClientCount();
        }

        // Print formatted server statistics report
        std::cout << http::ServerStats::getInstance().formatStats(usedThreads, totalThreads, pendingConnections, activeClients) << std::endl;
    } else if (cmd == "ROUTES" || cmd == "RULES") {
        // Print active routing directives
        std::cout << "> Configured Routing Directives:\n";
        std::cout << "  [Match Rules] (" << conf::matchConfigs.size() << ")\n";
        if (conf::matchConfigs.empty()) {
            std::cout << "    (None)\n";
        } else {
            size_t idx = 0;
            for (const auto& pMatch : conf::matchConfigs) {
                if (!pMatch) continue;
                std::cout << "    #" << (++idx) << ": Pattern: \"" << pMatch->getPattern() << "\"\n";
                if (pMatch->getAccessControl()) {
                    std::cout << "        Access: " << (pMatch->getAccessControl()->isDenyAll() ? "deny all" : "allow all");
                    const auto& exceptions = pMatch->getAccessControl()->getExceptions();
                    if (!exceptions.empty())
                        std::cout << " (" << exceptions.size() << " exception" << (exceptions.size() > 1 ? "s" : "") << ")";
                    std::cout << '\n';
                }
                std::cout << "        Directory Indexes: " << (pMatch->showDirectoryIndexes() ? "on" : "off") << '\n';
                const auto& headers = pMatch->getHeaders();
                if (!headers.empty()) {
                    std::cout << "        Headers:\n";
                    for (const auto& [name, val] : headers)
                        std::cout << "          " << name << ": " << val << '\n';
                }
            }
        }

        // Print configured redirect rules
        std::cout << "  [Redirect Rules] (" << conf::redirectRules.size() << ")\n";
        if (conf::redirectRules.empty()) {
            std::cout << "    (None)\n";
        } else {
            size_t idx = 0;
            for (const auto& pRedir : conf::redirectRules) {
                if (!pRedir) continue;
                std::cout << "    #" << (++idx) << ": \"" << pRedir->getPatternStr() << "\" -> \""
                          << pRedir->getTarget() << "\" (" << pRedir->getStatus() << ")\n";
            }
        }

        // Print configured rewrite rules
        std::cout << "  [Rewrite Rules] (" << conf::rewriteRules.size() << ")\n";
        if (conf::rewriteRules.empty()) {
            std::cout << "    (None)\n";
        } else {
            size_t idx = 0;
            for (const auto& pRewrite : conf::rewriteRules) {
                if (!pRewrite) continue;
                std::cout << "    #" << (++idx) << ": \"" << pRewrite->getPatternStr() << "\" -> \""
                          << pRewrite->getTarget() << "\"\n";
            }
        }
        std::cout << std::flush;
    } else if (cmd == "TLS" || cmd == "CERT") {
        // Print TLS status and X.509 certificate metadata
        http::printTLSInfo();
    } else if (cmd == "LOGS" || cmd == "TAIL") {
        // Parse target log file and line count arguments
        std::filesystem::path targetFile = conf::ACCESS_LOG_FILE;
        size_t lineCount = 10;
        if (!args.empty()) {
            std::string sub = args;
            strToUpper(sub);
            if (sub.starts_with("ERROR")) {
                targetFile = conf::ERROR_LOG_FILE;
                std::string rest = args.substr(5);
                trimString(rest);
                if (!rest.empty()) {
                    try { lineCount = std::stoul(rest); } catch (...) {}
                }
            } else if (sub.starts_with("ACCESS")) {
                targetFile = conf::ACCESS_LOG_FILE;
                std::string rest = args.substr(6);
                trimString(rest);
                if (!rest.empty()) {
                    try { lineCount = std::stoul(rest); } catch (...) {}
                }
            } else {
                try { lineCount = std::stoul(args); } catch (...) {}
            }
        }
        readLogTail(targetFile, lineCount);
    } else if (cmd == "MIMES" || cmd == "MIME") {
        // Collect and sort MIME mappings alphabetically by extension
        std::vector<std::pair<std::string, std::string>> sortedMimes(conf::MIMES.begin(), conf::MIMES.end());
        std::sort(sortedMimes.begin(), sortedMimes.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        if (args.empty()) {
            // Display all configured MIME types
            std::cout << "> Configured MIME Types (" << sortedMimes.size() << " mappings):\n";
            for (const auto& [ext, mime] : sortedMimes) {
                std::cout << "    ." << std::left << std::setw(12) << ext << " -> " << mime << '\n';
            }
        } else {
            // Filter MIME mappings by search query
            std::string query = args;
            strToLower(query);
            if (query.starts_with('.'))
                query = query.substr(1);

            std::vector<std::pair<std::string, std::string>> matches;
            for (const auto& entry : sortedMimes) {
                std::string extLower = entry.first;
                std::string mimeLower = entry.second;
                strToLower(extLower);
                strToLower(mimeLower);
                if (extLower.find(query) != std::string::npos || mimeLower.find(query) != std::string::npos)
                    matches.push_back(entry);
            }

            if (matches.empty()) {
                std::cout << "> No MIME mappings found matching \"" << args << "\"" << std::endl;
            } else {
                std::cout << "> Matching MIME Types (" << matches.size() << " match"
                          << (matches.size() > 1 ? "es" : "") << "):\n";
                for (const auto& [ext, mime] : matches) {
                    std::cout << "    ." << std::left << std::setw(12) << ext << " -> " << mime << '\n';
                }
            }
        }
        std::cout << std::flush;
    } else if (cmd == "VERSION" || cmd == "VER") {
        // Print version and protocol capabilities
        std::cout << "> " << (conf::VERSION.empty() ? "Mercury" : conf::VERSION) << '\n'
                  << "  Protocols:   HTTP/1.1 (default), HTTP/1.0, HTTP/0.9\n"
                  << "  Encryption:  TLSv1.3 (OpenSSL)" << std::endl;
    } else if (cmd == "PING") {
        std::cout << "> Pong!" << std::endl;
    } else if (cmd == "PWD") {
        std::cout << conf::DOCUMENT_ROOT.string() << std::endl;
    } else if (cmd == "HELP" || cmd == "?") {
        std::cout << "> Clear: Clears the terminal window (alias: cls)\n"
            "  Config: View active server configuration directives (alias: conf)\n"
            "  Donate: Shows optional donation URL\n"
            "  Exit: Exit Mercury (aliases: quit, q)\n"
            "  Help: List available commands (alias: ?)\n"
            "  Logs: View recent access or error logs (e.g. \"logs [access|error] [n]\") (alias: tail)\n"
            "  Mimes: View or search configured MIME type mappings (e.g. \"mimes [query]\") (alias: mime)\n"
            "  PHPInit: Initializes platform-specific PHP\n"
            "  Ping: Pong!\n"
            "  Pwd: Prints the document root\n"
            "  Routes: View active Match, Redirect, and Rewrite rules (alias: rules)\n"
            "  Stats: View live traffic, request count, and HTTP status distribution (aliases: status, info, metrics, uptime)\n"
            "  TLS: View TLS configuration and certificate status (alias: cert)\n"
            "  Version: View Mercury version and protocol capabilities (alias: ver)"
            << std::endl;
    } else if (cmd == "PHPINIT") {
        #ifdef _WIN32 // Windows specific
            std::cout << "> Running `conf/setup_php.ps1`" << std::endl;
            std::string scriptPath;
            try {
                scriptPath = resolveCanonicalPath("conf/setup_php.ps1").string();
            } catch (...) {
                std::cout << "> Failed to canonicalize the path to `conf/setup_php.ps1`" << std::endl;
                return;
            }

            // Generate command
            const std::string cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + scriptPath + "\"";
            int rc = -1;
            STARTUPINFOA si;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi;
            ZeroMemory(&pi, sizeof(pi));

            // Invoke CreateProcess
            std::vector<char> cmdLine(cmd.begin(), cmd.end());
            cmdLine.push_back('\0');

            if (CreateProcessA( NULL, cmdLine.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi )) {
                // Wait for process to finish
                WaitForSingleObject(pi.hProcess, INFINITE);

                DWORD exitCode = 0;
                rc = GetExitCodeProcess(pi.hProcess, &exitCode) ? static_cast<int>(exitCode) : -1;

                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            } else {
                // Failed to create process
                rc = static_cast<int>(GetLastError());
            }
        #else // Linux specific
            std::string command;
            if (std::system("command -v apt-get >/dev/null 2>&1") == 0) {
                command = "sudo apt-get update -y >/dev/null && sudo apt-get install php-cgi -y";
            } else if (std::system("command -v pacman >/dev/null 2>&1") == 0) {
                command = "sudo pacman --noconfirm -Syu >/dev/null && sudo pacman --noconfirm -S php-cgi";
            } else if (std::system("command -v dnf >/dev/null 2>&1") == 0) {
                command = "sudo dnf makecache -y >/dev/null && sudo dnf install -y php-cgi";
            }

            int rc = 1;
            if (!command.empty()) {
                std::cout << "> Running `" << command << '`' << std::endl;
                rc = std::system(command.c_str());
            } else {
                std::cout << "> Failed to detect system package manager" << std::endl;
            }
        #endif

        if (rc == 0) {
            std::cout << "> PHP initialized successfully." << std::endl;
            if (!conf::IS_PHP_ENABLED)
                std::cout << "PHP is currently disabled, set EnablePHPCGI to `on` in your mercury.conf and restart Mercury for these changes to take effect." << std::endl;
        } else {
            std::cout << "> Failed to initialize PHP, exited with code " << rc << std::endl;
        }
    } else if (!isExiting) {
        std::cout << "> Unknown command, try \"help\"" << std::endl;
    }
}

void awaitCLI(std::atomic<bool>& isExiting, std::vector<std::shared_ptr<http::Server>>& serversVec) {
    #ifdef _WIN32
        // Check if connected to a terminal
        {
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD mode;
            if (!GetConsoleMode(hIn, &mode)) {
                while (!isExiting)
                    SLEEP_BETWEEN_CLI;
                return;
            }
        }
    #else
        // Check if stdin is a terminal on Linux
        if (!isatty(STDIN_FILENO)) {
            while (!isExiting)
                SLEEP_BETWEEN_CLI;
            return;
        }

        TermiosGuard tg;
    #endif

    std::vector<std::string> history;
    history.reserve(MAX_HISTORY_LEN + 1);
    while (!isExiting) {
        std::cout << "< " << std::flush;

        // Read next command
        #ifdef _WIN32
            std::string buf;
            if (!std::getline(std::cin, buf)) {
                if (isExiting || std::cin.eof()) {
                    std::cout << std::endl;
                    return;
                }

                std::cin.clear();
                continue;
            }
        #else
            int historyIndex;
            readNextLine(isExiting, history, historyIndex);

            // Handle Ctrl+C event before CLI commands (to ignore whatever's typed)
            if (isExiting) return;

            std::string buf = history[historyIndex];
        #endif

        // Trim the buffer
        trimString(buf); strToUpper(buf);
        if (buf.empty()) {
            #ifdef __linux__
                history.erase(history.cbegin() + historyIndex);
            #endif
            continue;
        }

        // Handle the CLI commands
        handleCLICommands(buf, isExiting, serversVec);

        if (isExiting) return;
    }
}
