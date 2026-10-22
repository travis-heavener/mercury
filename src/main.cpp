#include <atomic>
#include <iostream>
#include <signal.h>

#ifndef _WIN32
    #include <sys/stat.h>
#endif

#include "conf/conf.hpp"
#include "http/server.hpp"
#include "http/server-ipv6.hpp"
#include "http/version_checker.hpp"
#include "logs/logger.hpp"
#include "util/cli.hpp"
#include "util/privilege.hpp"

// Global termination flag (atomic)
std::atomic<bool> isExiting{false};

std::vector<std::shared_ptr<http::Server>> serversVec;

/******************** SIGNAL HANDLERS & CLEANUP ********************/

void catchSig(int) {
    std::cout << std::endl;
    isExiting.store(true);
}

#ifdef _WIN32
    BOOL WINAPI consoleHandler(DWORD signal) {
        switch (signal) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_LOGOFF_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                catchSig(0);
                return TRUE;
            default:
                return FALSE;
        }
    }
#endif

void initSigHandler() {
    #ifdef _WIN32
        SetConsoleCtrlHandler(consoleHandler, TRUE);
    #else
        struct sigaction sigIntHandler;

        sigIntHandler.sa_handler = catchSig;
        sigemptyset(&sigIntHandler.sa_mask);
        sigIntHandler.sa_flags = 0;

        sigaction(SIGINT, &sigIntHandler, NULL);
        sigaction(SIGTERM, &sigIntHandler, NULL);

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

/******************** WELCOME BANNER METHODS ********************/

constexpr int BANNER_INNER_WIDTH = 52;

void printBannerBorder(const bool isTop) {
    std::cout << (isTop ? "\u250C" : "\u2514");
    for (int i = 0; i < BANNER_INNER_WIDTH; ++i) std::cout << "\u2500";
    std::cout << (isTop ? "\u2510" : "\u2518") << std::endl;
}

void printBannerLine(const std::string& text) {
    const int len = static_cast<int>(text.length());
    const int leftPadding = std::max(0, (BANNER_INNER_WIDTH - len) / 2);
    const int rightPadding = std::max(0, BANNER_INNER_WIDTH - len - leftPadding);

    std::cout << "\u2502" << std::string(leftPadding, ' ') << text << std::string(rightPadding, ' ') << "\u2502" << std::endl;
}

void printWelcomeBanner() {
    /**
     * Prints the following message:
     * 
     * ┌────────────────────────────────────────────────────┐
     * │                  Mercury vX.X.X                    │
     * └────────────────────────────────────────────────────┘
     */
    printBannerBorder(true);
    printBannerLine(conf::VERSION.empty() ? "Mercury" : conf::VERSION);
    printBannerBorder(false);

    ACCESS_LOG << (conf::VERSION.empty() ? "Mercury" : conf::VERSION) << " started successfully." << std::endl;
}

// Prints a donation banner on startup, if enabled
void printDonationBanner() {
    /**
     * Prints the following message:
     *
     * ┌────────────────────────────────────────────────────┐
     * │  Love Mercury? Consider supporting this project:   │
     * │           https://wowtravis.com/support            │
     * └────────────────────────────────────────────────────┘
     */
    printBannerBorder(true);
    printBannerLine("Love Mercury? Consider supporting this project:");
    printBannerLine("https://wowtravis.com/support");
    printBannerBorder(false);
}

/******************** ENTRY POINT ********************/

int main(int argc, char* argv[]) {
    #ifdef _WIN32
        // Initialize Winsock API
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::cerr << "Failed to initialize Winsock API.\n";
            return 1;
        }

        // Configure UTF-8 console output and enable Virtual Terminal mode
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    #else
        // Set standard umask
        umask(0022);
    #endif

    // Configure interrupt handler
    initSigHandler();

    // Load config files & parse CLI arguments
    const int confStatus = conf::loadConfig(argc, argv);
    if (confStatus == CONF_EXIT) {
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 0;
    }
    if (confStatus == CONF_FAILURE) {
        cleanExit();
        return 1;
    }

    // Check for new version at startup
    if (conf::CHECK_LATEST_RELEASE) {
        const std::string latestVersion = fetchLatestVersion();
        try {
            if (latestVersion.length() > 0 && conf::isVersionOutdated(latestVersion))
                std::cout << "Update available! (" << latestVersion.substr(8) << ")\nVisit https://mercury.wowtravis.com/" << std::endl;
            else if (latestVersion.length() == 0)
                std::cerr << "Failed to fetch latest version!" << std::endl;
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

    // Drop root privileges if running under sudo or doas
    if (!util::privilege::dropPrivileges()) {
        cleanExit();
        return 1;
    }

    // Print welcome banner
    if (conf::SHOW_WELCOME_BANNER)
        printWelcomeBanner();

    // Print donation banner
    if (conf::SHOW_DONATION_BANNER)
        printDonationBanner();

    // Accept client responses
    std::vector<std::thread> threads;
    for (auto& server : serversVec)
        threads.emplace_back(std::thread([server]() { server->acceptLoop(); }));

    // Wait for "exit" in cin (sets isExiting if "exit" is found)
    awaitCLI(isExiting, serversVec);

    /******* Reached if closing the program *******/
    std::cout << "> Shutting down..." << std::endl;

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
