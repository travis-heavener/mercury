#include <signal.h>
#include <string>

#include "http/server.hpp"
#include "toolbox.hpp"

#define DEFAULT_PORT 9220

HTTP::Server* pServer = nullptr;

void catchSig(int s) {
    std::cerr << "\nIntercepted exit signal " << s << ", closing...\n";

    if (pServer != nullptr)
        pServer->kill();
}

void initSigHandler() {
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = catchSig;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
}

int main(int argc, char* argv[]) {
    const int PORT = (argc >= 2) ? std::atoi(argv[1]) : DEFAULT_PORT;
    const std::string HOST = "0.0.0.0";

    // Configure interrupt handler
    initSigHandler();

    // Load aux resources
    if (loadResources() == IO_FAILURE) {
        std::cerr << "Failed to load additional resources.\n";
        return 1;
    }

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