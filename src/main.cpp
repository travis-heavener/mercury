#include <atomic>
#include <iostream>
#include <signal.h>

#include "conf/conf.hpp"
#include "http/server.hpp"
#include "http/server-ipv6.hpp"
#include "logs/logger.hpp"
#include "http/version_checker.hpp"
#include "util/string_tools.hpp"

// Global termination flag (atomic)
std::atomic<bool> isExiting{false};

std::vector<std::shared_ptr<http::Server>> serversVec;

/******************** SIGNAL HANDLERS & CLEANUP ********************/

void catchSig(int) { isExiting.store(true); }

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

        // Ignore SIGPIPE
        signal(SIGPIPE, SIG_IGN);
    #endif
}

void cleanExit() {
    #ifdef _WIN32
        WSACleanup();
    #endif

    // Log process closure
    ACCESS_LOG << "Process killed successfully." << std::endl;

    // Cleanup config resources
    conf::cleanupConfig();
}

void awaitExitCin() {
    // Otherwise, read from cin
    std::string buf;
    std::getline(std::cin, buf);
    trimString(buf); strToUpper(buf);

    // Clean exit
    if (buf == "EXIT")
        isExiting.store(true);
}

/******************** WELCOME BANNER METHODS ********************/

void printCenteredVersion() {
    const int leftPadding = (34 - conf::VERSION.length()) / 2;
    const int rightPadding = 34 - conf::VERSION.length() - leftPadding;

    std::cout << '|' << std::string(leftPadding, ' ') << conf::VERSION << std::string(rightPadding, ' ') << '|' << '\n';
}

void printWelcomeBanner() {
    std::cout << "------------------------------------" << std::endl;
    printCenteredVersion();
    std::cout << "|           ...........            |" << std::endl <<
                 "|         \"Exit\" to close.         |" << std::endl <<
                 "------------------------------------" << std::endl;
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
    if (conf::loadConfig() == CONF_FAILURE) {
        cleanExit();
        return 1;
    }

    // Check for new version at startup
    if (conf::CHECK_LATEST_RELEASE) {
        const std::string latestVersion = fetchLatestVersion();
        try {
            if (latestVersion.length() > 0 && conf::isVersionOutdated(latestVersion))
                std::cout << "Update available! (" << latestVersion.substr(8) << ")\nSee https://wowtravis.com/mercury" << std::endl;
        } catch (std::invalid_argument&) {
            std::cout << "Failed to compare local and remote versions!" << std::endl;
        }
    }

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
    if (conf::SHOW_WELCOME_BANNER)
        printWelcomeBanner();

    // Accept client responses
    std::vector<std::thread> threads;
    for (auto& server : serversVec)
        threads.emplace_back(std::thread([server]() { server->acceptLoop(); }));

    // Wait for "exit" in cin (sets isExiting if "exit" is found)
    while (!isExiting.load())
        awaitExitCin();

    /******* Reached if closing the program *******/
    std::cout << "\nShutting down..." << std::endl;

    // Kill the server
    for (auto& server : serversVec)
        server->kill();

    // Join all threads
    for (std::thread& t : threads)
        t.join();

    // Clean up
    cleanExit();
    return 0;
}