#include <signal.h>
#include <string>

#include "http/server.hpp"
#include "util/toolbox.hpp"

HTTP::Server* pServer = nullptr;

void m_exit(); // Fwd declaration
void catchSig(int s) {
    std::cout << "\nIntercepted exit signal " << s << ", closing...\n";
    ACCESS_LOG << "Intercepted exit signal " << s << ", closing..." << '\n'; // No endl bc flush happens async w/ signals
    m_exit();
    exit(0);
}

void initSigHandler() {
    #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
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

void printWelcomeBanner() {
    std::cout << "------------------------------------\n"
                 "|          "  VERSION   "          |\n"
                 "|           ...........            |\n"
                 "|         Ctrl+C to close.         |\n"
                 "------------------------------------\n";
    ACCESS_LOG << VERSION " started successfully." << std::endl;
}

void m_exit() {
    if (pServer != nullptr) {
        delete pServer;
        pServer = nullptr;
    }

    #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
        WSACleanup();
    #endif

    // Log process closure
    ACCESS_LOG << "Process killed successfully." << '\n';

    // Cleanup config resources
    cleanupConfig();
}

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
        m_exit();
        return 1;
    }

    // Init server
    pServer = new HTTP::Server(conf::HOST, conf::PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER);

    // Print welcome banner
    printWelcomeBanner();

    if (pServer != nullptr && pServer->init() < 0) {
        m_exit();
        return 1;
    }

    // Accept client responses (blocks main thread)
    if (pServer != nullptr)
        pServer->handleReqs();

    // Clean up
    m_exit();
    return 0;
}