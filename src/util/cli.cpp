#include "cli.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

#include "../conf/conf.hpp"
#include "../http/telemetry/stats.hpp"
#include "../http/tls/tls.hpp"

#ifdef __linux__
    #include <poll.h>
#endif

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
#define KEY_WORD_LEFT 927
#define KEY_WORD_RIGHT 928
#define KEY_WORD_BACKSPACE 929
#define KEY_WORD_DELETE 930

// Enables raw terminal input mode
#ifdef __linux__
    int getKey() {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1) return -1;
        if (c == 8 || c == 23) return KEY_WORD_BACKSPACE; // Ctrl+Backspace / Ctrl+W
        if (c != '\x1b') return c;

        // Bound escape parsing so lone Escape or incomplete sequences cannot block input
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
        auto readMore = [&](unsigned char& byte) {
            pollfd input{STDIN_FILENO, POLLIN, 0};
            while (true) {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                if (remaining <= 0) return false;
                const int ready = poll(&input, 1, static_cast<int>(remaining));
                if (ready < 0 && errno == EINTR) continue;
                return ready > 0 && (input.revents & POLLIN) && read(STDIN_FILENO, &byte, 1) == 1;
            }
        };

        if (!readMore(c)) return '\x1b';
        if (c == 127 || c == 8) return KEY_WORD_BACKSPACE;
        if (c == 'd') return KEY_WORD_DELETE; // Escape+d word deletion
        if (c != '[' && c != 'O') return '\x1b';

        // Consume complete CSI/SS3 sequence, including unsupported modifier keys
        std::string seq;
        while (readMore(c)) {
            seq += static_cast<char>(c);
            if (c >= 0x40 && c <= 0x7e) break;
            if (c < 0x20 || c > 0x3f || seq.size() >= 64) return '\x1b';
        }

        if (seq == "A") return KEY_UP;
        if (seq == "B") return KEY_DOWN;
        if (seq == "C") return KEY_RIGHT;
        if (seq == "D") return KEY_LEFT;
        if (seq == "3~") return KEY_DELETE;
        if (seq == "1;5D" || seq == "5D") return KEY_WORD_LEFT;
        if (seq == "1;5C" || seq == "5C") return KEY_WORD_RIGHT;
        if (seq == "3;5~" || seq == "3^") return KEY_WORD_DELETE;
        if (seq == "8;5u" || seq == "127;5u" || seq == "27;5;8~" || seq == "27;5;127~")
            return KEY_WORD_BACKSPACE;
        return '\x1b';
    }

    // Used to find previous whitespace-delimited word boundary
    static int previousWord(const std::string& line, int cursor) {
        while (cursor > 0 && std::isspace(static_cast<unsigned char>(line[cursor - 1]))) --cursor;
        while (cursor > 0 && !std::isspace(static_cast<unsigned char>(line[cursor - 1]))) --cursor;
        return cursor;
    }

    // Used to find next whitespace-delimited word boundary
    static int nextWord(const std::string& line, int cursor) {
        const int end = static_cast<int>(line.size());
        while (cursor < end && std::isspace(static_cast<unsigned char>(line[cursor]))) ++cursor;
        while (cursor < end && !std::isspace(static_cast<unsigned char>(line[cursor]))) ++cursor;
        return cursor;
    }

    // Updates the current line
    void showLine(const std::string& line) {
        std::cout << "\33[2K\r< " << line << std::flush;
    }

    // Used to complete command or supported argument at cursor
    static void completeLine(std::string& line, int& cursor, bool repeatedTab) {
        static const std::vector<std::string> commands{
            "?", "cert", "clear", "cls", "conf", "config", "donate", "exit", "fund",
            "help", "info", "logs", "metrics", "mime", "mimes", "php:init", "phpinit",
            "ping", "pwd", "q", "quit", "routes", "rules", "stats", "status", "tail",
            "tls", "uptime", "ver", "version"
        };
        static const std::vector<std::string> logTargets{"access", "error"};

        // Locate whole token while matching only prefix before cursor
        auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)); };
        int start = cursor, end = cursor;
        while (start > 0 && !isSpace(line[start - 1])) --start;
        while (end < static_cast<int>(line.size()) && !isSpace(line[end])) ++end;

        std::istringstream before(line.substr(0, start));
        std::string command, argument;
        const std::vector<std::string>* pCandidates = &commands;
        if (before >> command) {
            strToLower(command);
            if ((command != "logs" && command != "tail") || (before >> argument)) return;
            pCandidates = &logTargets;
        }

        std::string prefix = line.substr(start, cursor - start);
        strToLower(prefix);
        std::vector<std::string> matches;
        for (const auto& candidate : *pCandidates) {
            if (candidate.starts_with(prefix)) matches.push_back(candidate);
        }
        if (matches.empty()) return;

        // Extend shared prefix or finish unique match without disturbing following arguments
        std::string completion = matches.front();
        for (const auto& match : matches) {
            size_t shared = 0;
            while (shared < completion.size() && shared < match.size() && completion[shared] == match[shared])
                ++shared;
            completion.resize(shared);
        }
        if (matches.size() == 1 || completion.size() > prefix.size()) {
            line.replace(start, end - start, completion);
            cursor = start + static_cast<int>(completion.size());
            if (matches.size() == 1 && cursor == static_cast<int>(line.size())) {
                line += ' ';
                ++cursor;
            }
        } else if (repeatedTab) {
            std::cout << '\n';
            for (size_t i = 0; i < matches.size(); ++i) {
                if (i > 0) std::cout << "  ";
                std::cout << matches[i];
            }
            std::cout << '\n';
        }

        showLine(line);
        std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
    }

    // Gets the next line from the terminal
    void readNextLine(std::atomic<bool>& isExiting, std::vector<std::string>& history, int& historyIndex) {
        if (history.size() > MAX_HISTORY_LEN) history.erase(history.begin());

        // Push empty 
        history.push_back("");
        historyIndex = static_cast<int>(history.size()) - 1;

        int c, cursor = 0;
        bool previousTab = false;
        while (!isExiting) {
            if ((c = getKey()) == -1) continue;
            const bool repeatedTab = c == '\t' && previousTab;
            previousTab = c == '\t';

            switch (c) {
                case '\t':
                    completeLine(history[historyIndex], cursor, repeatedTab);
                    break;
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
                case KEY_WORD_LEFT:
                    cursor = previousWord(history[historyIndex], cursor);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
                case KEY_WORD_RIGHT:
                    cursor = nextWord(history[historyIndex], cursor);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
                case KEY_WORD_BACKSPACE: {
                    const int start = previousWord(history[historyIndex], cursor);
                    history[historyIndex].erase(start, cursor - start);
                    cursor = start;
                    showLine(history[historyIndex]);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
                }
                case KEY_WORD_DELETE: {
                    const int end = nextWord(history[historyIndex], cursor);
                    history[historyIndex].erase(cursor, end - cursor);
                    showLine(history[historyIndex]);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
                }
                case KEY_DELETE:
                    if (history[historyIndex].empty() || cursor >= static_cast<int>(history[historyIndex].size())) break;
                    history[historyIndex].erase(history[historyIndex].cbegin() + cursor);
                    showLine(history[historyIndex]);
                    std::cout << "\033[" << (cursor + 3) << 'G' << std::flush;
                    break;
                case 127: // Backspace
                    if (cursor == 0) break;
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

struct LogTailOptions {
    bool errorLog = false;
    size_t lineCount = 10;
};

// Used to validate complete log selector and signed decimal count
static LogTailOptions parseLogTailOptions(std::string args) {
    LogTailOptions options;
    trimString(args);

    // Recognize complete selector token before parsing remaining count
    std::string selector = args.substr(0, args.find_first_of(" \t"));
    strToUpper(selector);
    if (selector == "ACCESS" || selector == "ERROR") {
        options.errorLog = selector == "ERROR";
        args.erase(0, selector.size());
        trimString(args);
    }

    std::string_view count(args);
    if (count.empty()) return options;
    if (count.front() == '+') count.remove_prefix(1);
    if (count.empty() || count.front() == '+') return options;

    // Reject overflow, nonpositive values, and trailing characters before unsigned conversion
    long long parsed = 0;
    const auto result = std::from_chars(count.data(), count.data() + count.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != count.data() + count.size() || parsed <= 0)
        return options;
    options.lineCount = static_cast<size_t>(std::min(parsed, 1000LL));
    return options;
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
                  << "    FastCGI:                  " << formatOnOff(conf::IS_FASTCGI_ENABLED)
                  << (conf::IS_FASTCGI_ENABLED ? (" (" + conf::FASTCGI_ADDRESS + ")") : "") << '\n'
                  << "  [Security & Logging]\n"
                  << "    Client Security Mode:     " << formatSecurityMode(conf::CLIENT_SECURITY_MODE) << '\n'
                  << "    Access Log File:          " << conf::ACCESS_LOG_FILE.string() << '\n'
                  << "    Error Log File:           " << conf::ERROR_LOG_FILE.string()
                  << std::endl;
    } else if (cmd == "DONATE" || cmd == "FUND") {
        std::cout << "> Love Mercury? Consider supporting this project:\n"
            "     https://wowtravis.com/support" << std::endl;
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
        const auto options = parseLogTailOptions(args);
        readLogTail(options.errorLog ? conf::ERROR_LOG_FILE : conf::ACCESS_LOG_FILE, options.lineCount);
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
        std::cout << "> Available Console Commands:\n"
            "  clear, cls                  Clear terminal screen\n"
            "  config, conf                View loaded server configuration\n"
            "  donate, fund                Show optional donation link\n"
            "  exit, quit, q               Exit Mercury\n"
            "  help, ?                     List available commands\n"
            "  logs, tail [file] [n]       View recent access or error logs\n"
            "  mimes, mime [query]         List or search configured MIME types\n"
            "  php:init, phpinit           Initialize platform-specific PHP\n"
            "  ping                        Test CLI responsiveness (Pong!)\n"
            "  pwd                         Print document root directory\n"
            "  routes, rules               View Match, Redirect, and Rewrite rules\n"
            "  stats, status, metrics      View live traffic, requests, and utilization\n"
            "  tls, cert                   View TLS configuration and certificate status\n"
            "  version, ver                View Mercury version and capabilities"
            << std::endl;
    } else if (cmd == "PHP:INIT" || cmd == "PHPINIT") {
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
                command = "sudo apt-get update -y >/dev/null && sudo apt-get install php-cgi php-fpm -y";
            } else if (std::system("command -v pacman >/dev/null 2>&1") == 0) {
                command = "sudo pacman --noconfirm -Syu >/dev/null && sudo pacman --noconfirm -S php-cgi php-fpm";
            } else if (std::system("command -v dnf >/dev/null 2>&1") == 0) {
                command = "sudo dnf makecache -y >/dev/null && sudo dnf install -y php-cgi php-fpm";
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
