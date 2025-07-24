#include <signal.h>
#include <string>
#include <thread>
#include <vector>

#include "http/server.hpp"
#include "http/server-ipv6.hpp"
#include "util/toolbox.hpp"
#include "logs/logger.hpp"

HTTP::Server* pServer = nullptr;
HTTP::ServerV6* pServerV6 = nullptr;
HTTP::Server* pTLSServer = nullptr;
HTTP::ServerV6* pTLSServerV6 = nullptr;

void m_exit(); // Fwd declaration
void catchSig(int s) {
    std::cout << "\nIntercepted exit signal " << s << ", closing...\n";
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

void m_exit() {
    if (pServer != nullptr) delete pServer;
    if (pTLSServer != nullptr) delete pTLSServer;
    if (pServerV6 != nullptr) delete pServerV6;
    if (pTLSServerV6 != nullptr) delete pTLSServerV6;

    #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
        WSACleanup();
    #endif

    // Log process closure
    ACCESS_LOG << "Process killed successfully." << std::endl;

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
    int totalSockets = 2;
    pServer = new HTTP::Server(conf::PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER, false);
    pServerV6 = new HTTP::ServerV6(conf::PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER, false);

    if (conf::USE_TLS) {
        totalSockets += 2;
        pTLSServer = new HTTP::Server(conf::TLS_PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER, true);
        pTLSServerV6 = new HTTP::ServerV6(conf::TLS_PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER, true);
    }

    // Print welcome banner
    printWelcomeBanner();

    // Track how many servers fail to start
    int serversFailed = 0;
    if (pServer != nullptr && pServer->init() > 0) {
        ++serversFailed;
        pServer = nullptr;
    }

    if (pServerV6 != nullptr && pServerV6->init() > 0) {
        ++serversFailed;
        pServerV6 = nullptr;
    }

    if (pTLSServer != nullptr && pTLSServer->init() > 0) {
        ++serversFailed;
        pTLSServer = nullptr;
    }

    if (pTLSServerV6 != nullptr && pTLSServerV6->init() > 0) {
        ++serversFailed;
        pTLSServerV6 = nullptr;
    }

    if (serversFailed == totalSockets) { // All failed to start, exit
        m_exit();
        return 1;
    }

    // Accept client responses (blocks main thread)
    std::vector<std::thread> threads;
    if (pServer != nullptr)
        threads.push_back( std::thread([p = pServer]() { p->acceptLoop(); }) );

    if (pServerV6 != nullptr)
        threads.push_back( std::thread([p = pServerV6]() { p->acceptLoop(); }) );

    if (pTLSServer != nullptr)
        threads.push_back( std::thread([p = pTLSServer]() { p->acceptLoop(); }) );

    if (pTLSServerV6 != nullptr)
        threads.push_back( std::thread([p = pTLSServerV6]() { p->acceptLoop(); }) );

    // Join all threads
    for (std::thread& t : threads)
        t.join();

    // Clean up
    m_exit();
    return 0;
}