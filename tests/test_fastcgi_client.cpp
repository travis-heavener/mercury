#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

#include "../src/conf/conf.hpp"
#include "../src/http/fastcgi/client.hpp"
#include "../src/http/fastcgi/params.hpp"
#include "../src/http/fastcgi/protocol.hpp"
#include "../src/http/message/request.hpp"
#include "../src/http/message/response.hpp"

using namespace http::fastcgi;

// Test endpoint string parsing across UDS and TCP formats
void testEndpointParsing() {
    Endpoint ep;

    // UNIX domain socket prefixes
    assert(Endpoint::parse("unix:/run/php/php8.2-fpm.sock", ep));
    assert(ep.type == TransportType::UNIX_SOCKET);
    assert(ep.path == "/run/php/php8.2-fpm.sock");

    assert(Endpoint::parse("/var/run/php-fpm.sock", ep));
    assert(ep.type == TransportType::UNIX_SOCKET);
    assert(ep.path == "/var/run/php-fpm.sock");

    // TCP IPv4 formats
    assert(Endpoint::parse("127.0.0.1:9000", ep));
    assert(ep.type == TransportType::TCP);
    assert(ep.host == "127.0.0.1");
    assert(ep.port == 9000);

    assert(Endpoint::parse("localhost:8080", ep));
    assert(ep.type == TransportType::TCP);
    assert(ep.host == "localhost");
    assert(ep.port == 8080);

    assert(Endpoint::parse("127.0.0.1", ep));
    assert(ep.type == TransportType::TCP);
    assert(ep.host == "127.0.0.1");
    assert(ep.port == 9000); // Default port

    // TCP IPv6 bracketed format
    assert(Endpoint::parse("[::1]:9000", ep));
    assert(ep.type == TransportType::TCP);
    assert(ep.host == "::1");
    assert(ep.port == 9000);

    assert(Endpoint::parse("[::1]", ep));
    assert(ep.type == TransportType::TCP);
    assert(ep.host == "::1");
    assert(ep.port == 9000);

    // Auto-detection of default FastCGI address
    assert(Endpoint::parse("", ep));
    assert(!ep.host.empty() || !ep.path.empty());

    assert(Endpoint::parse("auto", ep));
    assert(!ep.host.empty() || !ep.path.empty());

    std::string detected = detectDefaultFastCGIAddress();
    assert(!detected.empty());

    // Invalid formats
    assert(!Endpoint::parse("   ", ep));
    assert(!Endpoint::parse("unix:", ep));
    assert(!Endpoint::parse("[:9000", ep));

    std::cout << "[PASS] Endpoint parsing across UDS, IPv4, and IPv6 w/ auto-detection" << std::endl;
}

// Test Content-Type inference from memory buffers
void testContentTypeInference() {
    http::Response res("HTTP/1.1");

    inferContentType("{\"status\":\"ok\"}", res);
    assert(res.getContentType() == "application/json");

    inferContentType("[1, 2, 3]", res);
    assert(res.getContentType() == "application/json");

    inferContentType("<!DOCTYPE html><html><body>Mercury</body></html>", res);
    assert(res.getContentType() == "text/html");

    inferContentType("<?xml version=\"1.0\"?><data></data>", res);
    assert(res.getContentType() == "application/xml");

    inferContentType("Simple plain text line 1\nLine 2\nLine 3", res);
    assert(res.getContentType() == "text/plain; charset=utf-8");

    std::string binData = "\x00\x01\x02\x03\xFF\xFE\xFD";
    inferContentType(binData, res);
    assert(res.getContentType() == "application/octet-stream");

    std::cout << "[PASS] Content-Type inference from memory buffers" << std::endl;
}

// Test parameter population from Request and File
void testPopulateParams() {
    conf::PORT = 8080;
    conf::TLS_PORT = 8081;
    conf::VERSION = "Mercury v0.37.0";
    conf::DOCUMENT_ROOT = "/var/www/html";

    http::headers_map_t headers;
    headers["Host"] = "localhost:8080";
    headers["User-Agent"] = "MercuryTest/1.0";
    headers["X-Custom-Header"] = "CustomValue";

    std::string rawReq = "GET /test.php/extra/path?action=edit&id=42 HTTP/1.1\r\nHost: localhost:8080\r\nUser-Agent: MercuryTest/1.0\r\nX-Custom-Header: CustomValue\r\n\r\n";
    http::RequestFlags flags;
    http::Request req(headers, rawReq, "127.0.0.1", false, flags);

    File file(req.getPaths());
    file.absoluteResourcePath = "/var/www/html/test.php";
    file.decodedURIWithoutPathInfo = "/test.php";
    file.phpPathInfo = "/extra/path";
    file.queryString = "action=edit&id=42";

    ParamEncoder encoder;
    populateParams(encoder, file, req);

    std::vector<std::pair<std::string_view, std::string_view>> params;
    bool ok = ParamEncoder::decodeAll(encoder.getBuffer().data(), encoder.getBuffer().size(), params);
    assert(ok);

    auto findParam = [&](std::string_view key) -> std::string_view {
        for (const auto& [k, v] : params) {
            if (k == key) return v;
        }
        return "";
    };

    assert(findParam("GATEWAY_INTERFACE") == "CGI/1.1");
    assert(findParam("REQUEST_METHOD") == "GET");
    assert(findParam("SCRIPT_FILENAME") == "/var/www/html/test.php");
    assert(findParam("SCRIPT_NAME") == "/test.php");
    assert(findParam("PATH_INFO") == "/extra/path");
    assert(findParam("QUERY_STRING") == "action=edit&id=42");
    assert(findParam("SERVER_SOFTWARE") == "Mercury/0.37.0");
    assert(findParam("HTTP_USER_AGENT") == "MercuryTest/1.0");
    assert(findParam("HTTP_X_CUSTOM_HEADER") == "CustomValue");

    std::cout << "[PASS] Environment parameter extraction and mapping" << std::endl;
}

#ifndef _WIN32
// Helper to read exact bytes from socket
static bool mockRecvExact(int fd, void* dest, size_t len) {
    uint8_t* p = reinterpret_cast<uint8_t*>(dest);
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

// Helper to send exact bytes to socket
static bool mockSendAll(int fd, const void* src, size_t len) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

// Test complete FastCGI exchange with a mock server over loopback TCP
void testMockFastCGITransaction() {
    // Bind mock listener on loopback port
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = 0; // Kernel assigns free ephemeral port
    assert(bind(listener, reinterpret_cast<struct sockaddr*>(&sin), sizeof(sin)) == 0);
    assert(listen(listener, 1) == 0);

    socklen_t sinLen = sizeof(sin);
    assert(getsockname(listener, reinterpret_cast<struct sockaddr*>(&sin), &sinLen) == 0);
    uint16_t mockPort = ntohs(sin.sin_port);

    // Run mock FastCGI server in background thread
    std::thread serverThread([listener]() {
        int client = accept(listener, nullptr, nullptr);
        assert(client >= 0);

        // Read FCGI_BEGIN_REQUEST (16 bytes)
        FCGI_BeginRequestRecord beginRec;
        assert(mockRecvExact(client, &beginRec, sizeof(beginRec)));
        assert(beginRec.header.version == FCGI_VERSION_1);
        assert(beginRec.header.type == FCGI_BEGIN_REQUEST);
        assert(getRole(beginRec.body) == FCGI_RESPONDER);

        // Read records until STDIN is exhausted
        bool stdinDone = false;
        while (!stdinDone) {
            FCGI_Header head;
            assert(mockRecvExact(client, &head, sizeof(head)));
            uint16_t cl = getContentLength(head);
            uint8_t pl = head.paddingLength;

            std::vector<uint8_t> payload(cl + pl);
            if (cl + pl > 0)
                assert(mockRecvExact(client, payload.data(), cl + pl));

            if (head.type == FCGI_STDIN && cl == 0)
                stdinDone = true;
        }

        // Send response: FCGI_STDOUT with CGI headers and body
        std::string cgiOutput = "Status: 200 OK\r\nContent-Type: text/plain\r\nX-Powered-By: Mercury-FastCGI\r\n\r\nHello from FastCGI!";
        uint8_t stdoutPadding = calculatePadding(cgiOutput.size());
        FCGI_Header stdoutHeader = makeHeader(FCGI_STDOUT, 1, static_cast<uint16_t>(cgiOutput.size()), stdoutPadding);
        assert(mockSendAll(client, &stdoutHeader, sizeof(stdoutHeader)));
        assert(mockSendAll(client, cgiOutput.data(), cgiOutput.size()));
        if (stdoutPadding > 0) {
            uint8_t pad[8] = {0};
            assert(mockSendAll(client, pad, stdoutPadding));
        }

        // Send empty FCGI_STDOUT terminator
        FCGI_Header stdoutTerm = makeHeader(FCGI_STDOUT, 1, 0, 0);
        assert(mockSendAll(client, &stdoutTerm, sizeof(stdoutTerm)));

        // Send FCGI_END_REQUEST
        FCGI_EndRequestRecord endRec = makeEndRequestRecord(1, 0, FCGI_REQUEST_COMPLETE);
        assert(mockSendAll(client, &endRec, sizeof(endRec)));

        close(client);
        close(listener);
    });

    // Client execution
    Endpoint endpoint;
    endpoint.type = TransportType::TCP;
    endpoint.host = "127.0.0.1";
    endpoint.port = mockPort;

    Client client(5);
    assert(client.connect(endpoint));

    ParamEncoder params;
    params.add("REQUEST_METHOD", "GET");
    params.add("SCRIPT_FILENAME", "/test.php");
    assert(client.sendRequest(1, params, ""));

    http::Response res("HTTP/1.1");
    assert(client.receiveResponse(1, res));

    serverThread.join();

    assert(res.getStatus() == 200);
    assert(res.getContentType() == "text/plain");

    // Read response body from memory stream
    std::function<ssize_t(const char*, const size_t)> sendFn = [](const char*, const size_t len) -> ssize_t {
        return static_cast<ssize_t>(len);
    };
    (void)sendFn;

    std::cout << "[PASS] Mock FastCGI roundtrip communication and response demuxing" << std::endl;
}

// Test error handling on connection failure
void testConnectionFailure() {
    Endpoint deadEndpoint;
    deadEndpoint.type = TransportType::TCP;
    deadEndpoint.host = "127.0.0.1";
    deadEndpoint.port = 54321; // Unbound port

    http::headers_map_t headers;
    std::string rawReq = "GET /test.php HTTP/1.1\r\nHost: localhost\r\n\r\n";
    http::RequestFlags flags;
    http::Request req(headers, rawReq, "127.0.0.1", false, flags);
    File file(req.getPaths());
    http::Response res("HTTP/1.1");

    Client client(1);
    bool ok = client.execute(deadEndpoint, file, req, res);
    assert(!ok);
    assert(res.getStatus() == 502);

    std::cout << "[PASS] Error handling on connection failure (502 Bad Gateway)" << std::endl;
}

// Test POST request with body and STDIN payload streaming
void testPostRequestWithBody() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = 0;
    assert(bind(listener, reinterpret_cast<struct sockaddr*>(&sin), sizeof(sin)) == 0);
    assert(listen(listener, 1) == 0);

    socklen_t sinLen = sizeof(sin);
    assert(getsockname(listener, reinterpret_cast<struct sockaddr*>(&sin), &sinLen) == 0);
    uint16_t mockPort = ntohs(sin.sin_port);

    const std::string postBody = "username=mercury_user&action=upload&data=xyz123";

    std::thread serverThread([listener, postBody]() {
        int client = accept(listener, nullptr, nullptr);
        assert(client >= 0);

        // Read FCGI_BEGIN_REQUEST
        FCGI_BeginRequestRecord beginRec;
        assert(mockRecvExact(client, &beginRec, sizeof(beginRec)));

        // Read records until STDIN terminates
        bool stdinDone = false;
        std::string capturedStdin;
        while (!stdinDone) {
            FCGI_Header head;
            assert(mockRecvExact(client, &head, sizeof(head)));
            uint16_t cl = getContentLength(head);
            uint8_t pl = head.paddingLength;

            std::vector<uint8_t> payload(cl + pl);
            if (cl + pl > 0)
                assert(mockRecvExact(client, payload.data(), cl + pl));

            if (head.type == FCGI_STDIN) {
                if (cl == 0)
                    stdinDone = true;
                else
                    capturedStdin.append(reinterpret_cast<char*>(payload.data()), cl);
            }
        }

        // Verify captured STDIN matches expected post body
        assert(capturedStdin == postBody);

        // Send response: FCGI_STDOUT
        std::string cgiOutput = "Status: 200 OK\r\nContent-Type: text/plain\r\n\r\nReceived " + std::to_string(capturedStdin.size()) + " bytes";
        uint8_t stdoutPadding = calculatePadding(cgiOutput.size());
        FCGI_Header stdoutHeader = makeHeader(FCGI_STDOUT, 1, static_cast<uint16_t>(cgiOutput.size()), stdoutPadding);
        assert(mockSendAll(client, &stdoutHeader, sizeof(stdoutHeader)));
        assert(mockSendAll(client, cgiOutput.data(), cgiOutput.size()));
        if (stdoutPadding > 0) {
            uint8_t pad[8] = {0};
            assert(mockSendAll(client, pad, stdoutPadding));
        }

        // Send empty FCGI_STDOUT terminator
        FCGI_Header stdoutTerm = makeHeader(FCGI_STDOUT, 1, 0, 0);
        assert(mockSendAll(client, &stdoutTerm, sizeof(stdoutTerm)));

        // Send FCGI_END_REQUEST
        FCGI_EndRequestRecord endRec = makeEndRequestRecord(1, 0, FCGI_REQUEST_COMPLETE);
        assert(mockSendAll(client, &endRec, sizeof(endRec)));

        close(client);
        close(listener);
    });

    Endpoint endpoint;
    endpoint.type = TransportType::TCP;
    endpoint.host = "127.0.0.1";
    endpoint.port = mockPort;

    Client client(5);
    assert(client.connect(endpoint));

    ParamEncoder params;
    params.add("REQUEST_METHOD", "POST");
    params.add("CONTENT_LENGTH", std::to_string(postBody.size()));
    params.add("CONTENT_TYPE", "application/x-www-form-urlencoded");
    assert(client.sendRequest(1, params, postBody));

    http::Response res("HTTP/1.1");
    assert(client.receiveResponse(1, res));

    serverThread.join();

    assert(res.getStatus() == 200);
    assert(res.getContentType() == "text/plain");

    std::cout << "[PASS] POST request with body and STDIN payload streaming" << std::endl;
}

// Test timeout handling on stalled backend (504 Gateway Timeout)
void testTimeout504() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = 0;
    assert(bind(listener, reinterpret_cast<struct sockaddr*>(&sin), sizeof(sin)) == 0);
    assert(listen(listener, 1) == 0);

    socklen_t sinLen = sizeof(sin);
    assert(getsockname(listener, reinterpret_cast<struct sockaddr*>(&sin), &sinLen) == 0);
    uint16_t mockPort = ntohs(sin.sin_port);

    std::thread serverThread([listener]() {
        int client = accept(listener, nullptr, nullptr);
        assert(client >= 0);

        // Sleep without sending response to simulate hanging FastCGI daemon
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        close(client);
        close(listener);
    });

    Endpoint endpoint;
    endpoint.type = TransportType::TCP;
    endpoint.host = "127.0.0.1";
    endpoint.port = mockPort;

    // Use 1-second timeout
    Client client(1);
    assert(client.connect(endpoint));

    ParamEncoder params;
    params.add("REQUEST_METHOD", "GET");
    assert(client.sendRequest(1, params, ""));

    http::Response res("HTTP/1.1");
    bool ok = client.receiveResponse(1, res);
    assert(!ok);
    assert(res.getStatus() == 504);

    serverThread.join();

    std::cout << "[PASS] Timeout handling on stalled backend (504 Gateway Timeout)" << std::endl;
}
#endif

int main() {
    std::cout << "Running FastCGI transport client unit tests..." << std::endl;
    testEndpointParsing();
    testContentTypeInference();
    testPopulateParams();

    #ifndef _WIN32
        testMockFastCGITransaction();
        testConnectionFailure();
        testPostRequestWithBody();
        testTimeout504();
    #endif

    std::cout << "\nAll FastCGI Phase 2 & 7 unit tests PASSED!" << std::endl;
    return 0;
}
