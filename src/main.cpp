#include <signal.h>
#include <string>
#include <thread>
#include <vector>

#include "http/server.hpp"
#include "http/server-ipv6.hpp"
#include "util/toolbox.hpp"
#include "logs/logger.hpp"

std::vector<HTTP::Server*> serversVec;

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
    for (HTTP::Server* pServer : serversVec) {
        pServer->kill(); // Kill the server
        delete pServer;
    }

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
    if (conf::IS_IPV4_ENABLED)
        serversVec.push_back(new HTTP::Server(conf::PORT, false));

    if (conf::IS_IPV6_ENABLED)
        serversVec.push_back(new HTTP::ServerV6(conf::PORT, false));

    if (conf::USE_TLS) {
        if (conf::IS_IPV4_ENABLED)
            serversVec.push_back(new HTTP::Server(conf::TLS_PORT, true));

        if (conf::IS_IPV6_ENABLED)
            serversVec.push_back(new HTTP::ServerV6(conf::TLS_PORT, true));
    }

    // Print welcome banner
    printWelcomeBanner();

    // Remove servers that fail to start
    for (auto itr = serversVec.begin(); itr != serversVec.end(); (void)itr) {
        if ((*itr)->init() > 0) {
            // Free server
            (*itr)->kill(); // Kill the server
            delete *itr;

            // Erase from vector
            itr = serversVec.erase( itr );
            continue; // Skip increment
        }
        ++itr; // Base case, increment as usual
    }

    if (serversVec.size() == 0) { // All failed to start, exit
        m_exit();
        return 1;
    }

    // Accept client responses (blocks main thread)
    std::vector<std::thread> threads;
    for (HTTP::Server* pServer : serversVec)
        threads.push_back( std::thread([p = pServer]() { p->acceptLoop(); }) );

    // Join all threads
    for (std::thread& t : threads)
        t.join();

    // Clean up
    m_exit();
    return 0;
}