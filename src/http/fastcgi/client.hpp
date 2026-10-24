#ifndef __HTTP_FASTCGI_CLIENT_HPP
#define __HTTP_FASTCGI_CLIENT_HPP

#ifdef _WIN32
    #include "../../winheader.hpp"
#else
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/un.h>
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "../../io/file.hpp"
#include "../streams/body_stream.hpp"
#include "../message/request.hpp"
#include "../message/response.hpp"
#include "params.hpp"
#include "protocol.hpp"

namespace http::fastcgi {

    #ifdef _WIN32
        typedef SOCKET socket_t;
        #define FASTCGI_INVALID_SOCKET INVALID_SOCKET
        #define FASTCGI_SOCKET_ERROR SOCKET_ERROR
        #define fastcgiCloseSocket(s) closesocket(s)
    #else
        typedef int socket_t;
        #define FASTCGI_INVALID_SOCKET (-1)
        #define FASTCGI_SOCKET_ERROR (-1)
        #define fastcgiCloseSocket(s) ::close(s)
    #endif

    // FastCGI transport protocol type
    enum class TransportType {
        TCP,
        UNIX_SOCKET
    };

    // FastCGI connection endpoint descriptor
    struct Endpoint {
        TransportType type = TransportType::TCP;
        std::string   host;
        uint16_t      port = 9000;
        std::string   path;

        // Used to parse FastCGI address string (unix:/path or host:port)
        static bool parse(std::string_view addrStr, Endpoint& out);
    };

    /*******************************************************************/
    /********************* FASTCGI TRANSPORT CLIENT ********************/
    /*******************************************************************/

    class Client {
        public:
            explicit Client(int timeoutSeconds = 10);
            ~Client();

            // Connect to FastCGI endpoint (UDS or TCP)
            bool connect(const Endpoint& endpoint);

            // Send complete FastCGI request (BEGIN, PARAMS, STDIN)
            bool sendRequest(uint16_t requestId, const ParamEncoder& params, std::string_view body);

            // Read FastCGI response and populate Response object
            bool receiveResponse(uint16_t requestId, Response& res);

            // Execute complete FastCGI transaction
            bool execute(const Endpoint& endpoint, const File& file, const Request& req, Response& res);

            // Close connection
            void close();

            // Check if connection is open
            bool isConnected() const;

            // Check if last operation timed out
            bool didTimeout() const;

        private:
            // Low-level socket I/O helpers w/ timeout
            bool sendAll(const void* pData, size_t length);
            bool recvExact(void* pDest, size_t length);
            bool skipBytes(size_t length);

            socket_t sock = FASTCGI_INVALID_SOCKET;
            int timeoutSeconds;
            bool hasTimedOut = false;
    };

    // Used to detect default PHP FastCGI address, selecting highest PHP version on system
    std::string detectDefaultFastCGIAddress();

    // Platform independent FastCGI request handler
    bool handleRequest(const std::string& endpointStr, const File& file, const Request& req, Response& res, int timeoutSeconds = 10);

    // Used to assemble FastCGI environment parameters from File and Request
    void populateParams(ParamEncoder& encoder, const File& file, const Request& req);

    // Used to infer Content-Type from memory buffer
    void inferContentType(std::string_view body, Response& res);

}

#endif
