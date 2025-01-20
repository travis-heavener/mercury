#include "http_server.hpp"

void HTTPServer::kill() {
    if (!close(this->sock))
        std::cout << "Socket closed.\n";

    if (!close(this->c_sock))
        std::cout << "Client socket closed.\n";
}

// Initialize the socket
int HTTPServer::init() {
    // Open the socket
    this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (this->sock < 0) {
        std::cerr << "Failed to open socket on port " << this->port << ".\n";
        return SOCKET_FAILURE;
    }

    // Bind the host address
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(this->port);
    addr.sin_addr.s_addr = inet_addr(this->host.c_str());

    if (bind(this->sock, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket.\n";
        return BIND_FAILURE;
    }

    // Listen to socket
    if (listen(this->sock, MAX_BACKLOG) < 0) {
        std::cerr << "Failed to listen to socket.\n";
        return LISTEN_FAILURE;
    }

    std::cout << "Listening to " << this->host << " on port " << this->port << ".\n";
    return 0;
}

void HTTPServer::handleReqs() {
    // Accept requests from clients
    struct sockaddr_in clientAddr;
    struct in_addr clientIP;
    socklen_t clientLen;
    char clientIPStr[INET_ADDRSTRLEN];

    while ( (this->c_sock = accept(this->sock, (struct sockaddr*)&clientAddr, &clientLen)) >= 0 ) {
        // Read buffer
        recv(this->c_sock, this->readBuffer, sizeof(this->readBuffer), 0);
        clientIP = ((struct sockaddr_in*)&clientAddr)->sin_addr;
        inet_ntop( AF_INET, &clientIP, clientIPStr, INET_ADDRSTRLEN );

        // Parse request
        HTTPRequest req( this->readBuffer, clientIPStr );
        std::cout << req.getMethodStr() << ' ' << req.getIPStr() << ' ' << req.getPathStr() << '\n';

        // Return response
        std::string resBuffer;
        this->genResponse(resBuffer, req);
        send(this->c_sock, resBuffer.c_str(), resBuffer.size(), 0);

        // Close client socket
        close(this->c_sock);
    }
}

void HTTPServer::genResponse(std::string& buffer, const HTTPRequest& req) {
    std::string statusLine, headers, body;

    // Load preset headers
    loadConfHeaders(headers);

    // Switch on method
    switch (req.getMethod()) {
        case HTTP_METHOD::GET: {
            // Lookup file
            std::string path = (std::filesystem::current_path() / ("public" + req.getPathStr())).string();

            if (!doesFileExist(path)) {
                // Update body to error document
                statusLine = "HTTP/1.1 404 Not Found";

                // If the request allows HTML, return an HTML display
                if (req.isMIMEAccepted("text/html")) {
                    headers += "Content-Type: text/html";
                    loadErrorDoc(404, "Not Found", body);
                }
                break;
            }

            // Base case, file does exist
            File file = lookupFile(path);

            // Check accepted MIMES
            if (!req.isMIMEAccepted(file.MIME)) {
                // File does not match requested MIME
                statusLine = "HTTP/1.1 406 Not Acceptable";

                // If the request allows HTML, return an HTML display
                if (req.isMIMEAccepted("text/html")) {
                    headers += "Content-Type: text/html";
                    loadErrorDoc(406, "Not Acceptable", body);
                }
                break;
            }

            // MIME matches, resolve resource
            if (file.loadToBuffer(body) == IO_FAILURE) {
                statusLine = "HTTP/1.1 500 Internal Server Error";

                if (req.isMIMEAccepted("text/html")) {
                    headers += "Content-Type: text/html";
                    loadErrorDoc(500, "Internal Server Error", body);
                }
            } else {
                // Body loaded successfully
                statusLine = "HTTP/1.1 200 OK";
                headers += "Content-Type: " + file.MIME;
            }
            break;
        }
        default: {
            statusLine = "HTTP/1.1 405 Method Not Allowed";
            headers += "Content-Type: text/html";
            loadErrorDoc(405, "Method Not Allowed", body);
            break;
        }
    }

    // Compile output buffer
    buffer = statusLine + '\n' + headers + "\n\n" + body;
}