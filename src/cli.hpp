#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#include "conf/conf.hpp"
#include "http/server.hpp"

void handleCLICommands(const std::string& buf, std::atomic<bool>& isExiting, std::vector<std::shared_ptr<http::Server>>& serversVec) {
    // Clean exit
    if (buf == "EXIT") {
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
        std::cout << "> Exit: Exit Mercury\n"
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