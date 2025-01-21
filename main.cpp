#include <signal.h>
#include <string>

#include "http/server.hpp"
#include "toolbox.hpp"

HTTP::Server* pServer = nullptr;

void catchSig(int s) {
    std::cerr << "\nIntercepted exit signal " << s << ", closing...\n";

    if (pServer != nullptr)
        pServer->kill();

    #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
        WSACleanup();
    #endif
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

    std::cout << "------------------------------------\n"
                 "|          "  VERSION   "          |\n"
                 "|           ...........            |\n"
                 "|         Ctrl+C to close.         |\n"
                 "------------------------------------\n";
}

int main() {
    const std::string HOST = "0.0.0.0";

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
    if (loadConfig() == CONF_FAILURE) return 1;

    // Init server
    pServer = new HTTP::Server(HOST, PORT);

    const int status = pServer->init();
    if (status < 0) return 1;

    // Accept client responses (blocks main thread)
    pServer->handleReqs();

    // Free server
    delete pServer;

    return 0;
}