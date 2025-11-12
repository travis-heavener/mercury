#ifndef __UTIL_CLI_HPP

#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#include "../conf/conf.hpp"
#include "../http/server.hpp"
#include "../io/file_tools.hpp"

// Enables raw terminal input mode
#ifdef __linux__
    #include <termios.h>

    // RAII termios guard
    struct TermiosGuard {
        termios oldTio;

        TermiosGuard() {
            tcgetattr(STDIN_FILENO, &oldTio);

            termios new_tio = oldTio;
            new_tio.c_lflag &= ~(ICANON | ECHO | ISIG); // Disable canonical mode & echo
            new_tio.c_cc[VMIN] = 1;
            new_tio.c_cc[VTIME] = 0;

            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
        }

        ~TermiosGuard() {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldTio);
        }
    };

    // Reads a key from the terminal
    int getKey();

    // Updates the current line
    void showLine(const std::string& line);

    // Gets the next line from the terminal
    void readNextLine(std::atomic<bool>& isExiting, std::vector<std::string>& history, int& historyIndex);
#endif

void handleCLICommands(const std::string& buf, std::atomic<bool>& isExiting, std::vector<std::shared_ptr<http::Server>>& serversVec);
void awaitCLI(std::atomic<bool>& isExiting, std::vector<std::shared_ptr<http::Server>>& serversVec);

#endif