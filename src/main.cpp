#include <iostream>
#include <signal.h>

#include "pch/common.hpp"
#include "conf/conf.hpp"
#include "http/server.hpp"
#include "http/server-ipv6.hpp"
#include "logs/logger.hpp"
#include "http/version_checker.hpp"

std::vector<std::shared_ptr<http::Server>> serversVec;

/******************** SIGNAL HANDLERS & CLEANUP ********************/

void cleanExit(); // Fwd declaration
void catchSig(int s) {
    std::cout << "\nIntercepted exit signal " << s << ", closing...\n";
    cleanExit();
    exit(0);
}

void initSigHandler() {
    #ifdef _WIN32
        signal(SIGINT, catchSig);
        signal(SIGABRT, catchSig);
        signal(SIGTERM, catchSig);
    #else
        struct sigaction sigIntHandler;

        sigIntHandler.sa_handler = catchSig;
        sigemptyset(&sigIntHandler.sa_mask);
        sigIntHandler.sa_flags = 0;

        sigaction(SIGINT, &sigIntHandler, NULL);
    #endif
}

void cleanExit() {
    for (auto pServer : serversVec) {
        pServer->kill(); // Kill the server
    }

    #ifdef _WIN32
        WSACleanup();
    #endif

    // Log process closure
    ACCESS_LOG << "Process killed successfully." << std::endl;

    // Cleanup config resources
    cleanupConfig();
}

/******************** WELCOME BANNER METHODS ********************/

void printCenteredVersion() {
    const int leftPadding = (34 - conf::VERSION.length()) / 2;
    const int rightPadding = 34 - conf::VERSION.length() - leftPadding;

    std::cout << '|' << std::string(leftPadding, ' ') << conf::VERSION << std::string(rightPadding, ' ') << '|' << '\n';
}

void printWelcomeBanner() {
    std::cout << "------------------------------------\n";
    printCenteredVersion();
    std::cout << "|           ...........            |\n"
                 "|         Ctrl+C to close.         |\n"
                 "------------------------------------\n";
    ACCESS_LOG << conf::VERSION << " started successfully." << std::endl;
}

/******************** ENTRY POINT ********************/

int main() {
    // Initialize Winsock API
    #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::cerr << "Failed to initialize Winsock API.\n";
            return 1;
        }
    #endif

    // Configure interrupt handler
    initSigHandler();

    // Load config files
    if (loadConfig() == CONF_FAILURE) {
        cleanExit();
        return 1;
    }

    // Check for new version at startup
    const std::string latestVersion = fetchLatestVersion();
    if (latestVersion.length() > 0 && latestVersion != conf::VERSION)
        std::cout << "Update available! (" << latestVersion.substr(8) << ")\nSee https://wowtravis.com/mercury" << std::endl;

    // Init server
    if (conf::IS_IPV4_ENABLED)
        serversVec.emplace_back(std::make_shared<http::Server>(conf::PORT, false));

    if (conf::IS_IPV6_ENABLED)
        serversVec.emplace_back(std::make_shared<http::ServerV6>(conf::PORT, false));

    if (conf::USE_TLS) {
        if (conf::IS_IPV4_ENABLED)
            serversVec.emplace_back(std::make_shared<http::Server>(conf::TLS_PORT, true));

        if (conf::IS_IPV6_ENABLED)
            serversVec.emplace_back(std::make_shared<http::ServerV6>(conf::TLS_PORT, true));
    }

    // Remove servers that fail to start
    for (auto itr = serversVec.begin(); itr != serversVec.end(); (void)itr) {
        if ((*itr)->init() != 0) {
            // Free server
            (*itr)->kill(); // Kill the server

            // Erase from vector
            itr = serversVec.erase( itr );
            continue; // Skip increment
        }
        ++itr; // Base case, increment as usual
    }

    if (serversVec.size() == 0) { // All failed to start, exit
        cleanExit();
        return 1;
    }

    // Print welcome banner
    printWelcomeBanner();

    // Accept client responses
    std::vector<std::thread> threads;
    for (auto& server : serversVec)
        threads.emplace_back(std::thread([server]() { server->acceptLoop(); }));

    // Join all threads
    for (std::thread& t : threads)
        t.join();

    // Clean up
    cleanExit();
    return 0;
}