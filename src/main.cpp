#include <signal.h>
#include <string>
#include <thread>

#include "http/server.hpp"
#include "http/server-ipv6.hpp"
#include "util/toolbox.hpp"

HTTP::Server* pServer = nullptr;
HTTP::ServerV6* pServerV6 = nullptr;
HTTP::Server* pTLSServer = nullptr;
HTTP::ServerV6* pTLSServerV6 = nullptr;

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
    pServer = new HTTP::Server(conf::PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER, false);
    pServerV6 = new HTTP::ServerV6(conf::PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER, false);

    if (conf::USE_TLS) {
        pTLSServer = new HTTP::Server(conf::TLS_PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER, true);
        pTLSServerV6 = new HTTP::ServerV6(conf::TLS_PORT, conf::MAX_REQUEST_BACKLOG, conf::MAX_REQUEST_BUFFER, true);
    }

    // Print welcome banner
    printWelcomeBanner();

    #define m_ret { m_exit(); return 1; }
    if (pServer != nullptr && pServer->init() < 0) m_ret;
    if (pServerV6 != nullptr && pServerV6->init() < 0) m_ret;

    if (pTLSServer != nullptr && pTLSServer->init() < 0) m_ret;
    if (pTLSServerV6 != nullptr && pTLSServerV6->init() < 0) m_ret;

    // Accept client responses (blocks main thread)
    std::thread t_ipv4([&]() { pServer->acceptLoop(); });
    std::thread t_ipv6([&]() { pServerV6->acceptLoop(); });

    if (conf::USE_TLS) {
        std::thread t_ipv4TLS([&]() { pTLSServer->acceptLoop(); });
        std::thread t_ipv6TLS([&]() { pTLSServerV6->acceptLoop(); });
        t_ipv4TLS.join();
        t_ipv6TLS.join();
    }

    t_ipv6.join();
    t_ipv4.join();

    // Clean up
    m_exit();
    return 0;
}