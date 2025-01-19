#include <signal.h>
#include <string>

#include "http_server.hpp"

#define DEFAULT_PORT 9220

HTTPServer* pServer = nullptr;

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

    // Init server
    pServer = new HTTPServer(HOST, PORT);

    const int status = pServer->init();
    if (status < 0) return status;

    // Accept client responses
    pServer->handleReqs();

    // Free server
    delete pServer;

    return 0;
}