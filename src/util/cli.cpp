#include "cli.hpp"

#include <iostream>

#include "../conf/conf.hpp"

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
                case 12: // Ctrl+L (clear)
                    std::cout << "\033[2J\033[H" << std::flush;
                    return;
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

void handleCLICommands(const std::string& buf, std::atomic<bool>& isExiting, std::vector<std::shared_ptr<http::Server>>& serversVec) {
    // Clean exit
    if (buf == "CLEAR") {
        std::cout << "\033[2J\033[H" << std::flush;
    } else if (buf == "EXIT") {
        isExiting.store(true);
    } else if (buf == "INFO" || buf == "STATUS") {
        // Print usage info
        size_t usedThreads = 0, totalThreads = 0, pendingConnections = 0;
        for (auto& pServer : serversVec)
            pServer->getUsageInfo(usedThreads, totalThreads, pendingConnections);

        std::cout << std::fixed << std::setprecision(1) << "> "
            << std::min(static_cast<double>(usedThreads) / totalThreads * 100, 100.0) << "% usage ("
            << usedThreads << '/' << totalThreads << " threads, " << pendingConnections << " pending connections)"
            << std::endl;
    } else if (buf == "PING") {
        std::cout << "> Pong!" << std::endl;
    } else if (buf == "HELP") {
        std::cout << "> Clear: Clears the terminal window\n"
            "  Exit: Exit Mercury\n"
            "  Help: List available commands\n"
            "  Info: View current utilization\n"
            "  PHPInit: Initializes platform-specific PHP\n"
            "  Ping: ???\n"
            "  Status: View current utilization"
            << std::endl;
    } else if (buf == "PHPINIT") {
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
            std::cout << "> Running `sudo apt install php-cgi -y`" << std::endl;
            const int rc = std::system("sudo apt install php-cgi -y");
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
    #endif

    #ifdef __linux__
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
            SLEEP_BETWEEN_CLI;
            continue;
        }

        // Handle the CLI commands
        handleCLICommands(buf, isExiting, serversVec);

        if (isExiting) return;
    }
}