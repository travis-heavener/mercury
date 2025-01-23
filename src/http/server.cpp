#include "server.hpp"

namespace HTTP {

    Server::Server(const std::string& host, const port_t port, const u_short maxBacklog, const u_int maxBufferSize)
                : host(host), port(port), maxBacklog(maxBacklog), maxBufferSize(maxBufferSize) {
        // Create request buffer
        this->readBuffer = new char[this->maxBufferSize];
    };

    void Server::kill() {
        // Close sockets
        #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            #define close_socket closesocket
        #else
            #define close_socket close
        #endif

        if (this->c_sock != -1 && !close_socket(this->c_sock)) {
            ACCESS_LOG << "Client socket closed." << '\n';
            this->c_sock = -1;
        }

        if (this->sock != -1 && !close_socket(this->sock)) {
            ACCESS_LOG << "Socket closed." << '\n';
            this->sock = -1;
        }

        // Free read buffer
        delete[] this->readBuffer;
    }

    // Initialize the socket
    int Server::init() {
        // Open the socket
        this->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (this->sock < 0) {
            ERROR_LOG << "Failed to open socket on port " << this->port << '\n';
            return SOCKET_FAILURE;
        }

        // Init socket opts
        const int optFlag = 1;
        if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optFlag, sizeof(int)) < 0) {
            ERROR_LOG << "Failed to set socket opt SO_REUSEADDR." << '\n';
            return SOCKET_FAILURE;
        }

        #if __linux__
            if (setsockopt(this->sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&optFlag, sizeof(int)) < 0) {
                ERROR_LOG << "Failed to set socket opt SO_REUSEPORT." << '\n';
                return SOCKET_FAILURE;
            }
        #endif

        // Bind the host address
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(this->port);
        addr.sin_addr.s_addr = inet_addr(this->host.c_str());

        if (bind(this->sock, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ERROR_LOG << "Failed to bind socket (errno: " << errno << ')' << '\n';
            return BIND_FAILURE;
        }

        // Listen to socket
        if (listen(this->sock, this->maxBacklog) < 0) {
            ERROR_LOG << "Failed to listen to socket (errno: " << errno << ')' << '\n';
            return LISTEN_FAILURE;
        }

        ACCESS_LOG << "Listening to " << this->host << " on port " << this->port << '.' << '\n';
        return 0;
    }

    void Server::handleReqs() {
        // Accept requests from clients
        struct sockaddr_in clientAddr;
        struct in_addr clientIP;
        socklen_t clientLen = sizeof(clientAddr);
        char clientIPStr[INET_ADDRSTRLEN];

        while ( (this->c_sock = accept(this->sock, (struct sockaddr*)&clientAddr, &clientLen)) >= 0 ) {
            // Clear buffer
            this->clearBuffer();

            // Read buffer
            recv(this->c_sock, this->readBuffer, this->maxBufferSize, 0);
            clientIP = ((struct sockaddr_in*)&clientAddr)->sin_addr;

            #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
                strcpy(clientIPStr, inet_ntoa( clientIP ));
            #else
                inet_ntop( AF_INET, &clientIP, clientIPStr, INET_ADDRSTRLEN );
            #endif

            // Catch any stray packets after the program is killed
            if (std::strlen(this->readBuffer) == 0) return;

            // Parse request
            Request req( this->readBuffer, clientIPStr );
            ACCESS_LOG << req.getMethodStr() << ' ' << req.getIPStr() << ' ' << req.getPathStr() << std::endl; // Flush w/ endl vs newline

            // Return response
            std::string resBuffer;
            this->genResponse(resBuffer, req);
            send(this->c_sock, resBuffer.c_str(), resBuffer.size(), 0);

            // Close client socket
            #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
                closesocket(this->c_sock);
            #else
                close(this->c_sock);
            #endif
        }
    }

    void Server::genResponse(std::string& buffer, const Request& req) {
        std::string statusLine, body;
        std::unordered_map<std::string, std::string> resHeaders;

        // Load preset headers
        loadConfHeaders(resHeaders);

        // Switch on method
        switch (req.getMethod()) {
            case METHOD::GET: {
                const std::string rawPath = req.getPathStr();
                if (rawPath.size() == 0 || rawPath[0] != '/') {
                    statusLine = "HTTP/1.1 400 Bad Request";
                    resHeaders.insert({"ALLOW", ALLOWED_METHODS});

                    // If the request allows HTML, return an HTML display
                    if (req.isMIMEAccepted("text/html")) {
                        resHeaders.insert({"CONTENT-TYPE", "text/html"});
                        loadErrorDoc(400, "Bad Request", body);
                    }
                    break;
                }

                // Lookup file
                File file(rawPath);

                if (!doesFileExist(file.path, true) && !doesDirectoryExist(file.path, true)) {
                    // Update body to error document
                    statusLine = "HTTP/1.1 404 Not Found";

                    // If the request allows HTML, return an HTML display
                    if (req.isMIMEAccepted("text/html")) {
                        resHeaders.insert({"CONTENT-TYPE", "text/html"});
                        loadErrorDoc(404, "Not Found", body);
                    }
                    break;
                }

                // Check accepted MIMES
                if (!req.isMIMEAccepted(file.MIME)) {
                    // File does not match requested MIME
                    statusLine = "HTTP/1.1 406 Not Acceptable";

                    // If the request allows HTML, return an HTML display
                    if (req.isMIMEAccepted("text/html")) {
                        resHeaders.insert({"CONTENT-TYPE", "text/html"});
                        loadErrorDoc(406, "Not Acceptable", body);
                    }
                    break;
                }

                // Attempt to buffer resource
                if (file.loadToBuffer(body) == IO_FAILURE) {
                    statusLine = "HTTP/1.1 500 Internal Server Error";

                    if (req.isMIMEAccepted("text/html")) {
                        resHeaders.insert({"CONTENT-TYPE", "text/html"});
                        loadErrorDoc(500, "Internal Server Error", body);
                    }
                    break;
                }

                // Otherwise, body loaded successfully
                statusLine = "HTTP/1.1 200 OK";
                resHeaders.insert({"CONTENT-TYPE", file.MIME});

                // Load additional headers for body loading
                for (conf::Match* pMatch : conf::matchConfigs) {
                    if (std::regex_match(file.path, pMatch->getPattern())) {
                        // Apply headers
                        for (auto [name, value] : pMatch->getHeaders()) {
                            resHeaders.insert({name, value});
                        }
                    }
                }
                break;
            }
            default: {
                statusLine = "HTTP/1.1 405 Method Not Allowed";
                resHeaders.insert({"ALLOW", ALLOWED_METHODS});

                // If the request allows HTML, return an HTML display
                if (req.isMIMEAccepted("text/html")) {
                    resHeaders.insert({"CONTENT-TYPE", "text/html"});
                    loadErrorDoc(405, "Method Not Allowed", body);
                }
                break;
            }
        }

        // Handle compression
        auto contentTypeHeader = resHeaders.find("CONTENT-TYPE");
        if (contentTypeHeader != resHeaders.end() && (contentTypeHeader->second.find("text/") == 0 || contentTypeHeader->second == "application/json")) {
            // Determine compression method
            if (req.isEncodingAccepted("gzip")) {
                if (compressText(body, COMPRESS_GZIP) == IO_SUCCESS)
                    resHeaders.insert({"CONTENT-ENCODING", "gzip"});
            } else if (req.isEncodingAccepted("deflate")) {
                if (compressText(body, COMPRESS_DEFLATE) == IO_SUCCESS)
                    resHeaders.insert({"CONTENT-ENCODING", "deflate"});
            }
        }

        // Compile output buffer
        resHeaders.insert({"CONTENT-LENGTH", std::to_string(body.size())});
        std::string headers = "";
        for (auto [headerName, value] : resHeaders)
            headers += headerName + ": " + value + '\n';
        buffer = statusLine + '\n' + headers + "\n" + body;
    }

    void Server::clearBuffer() {
        for (u_int i = 0; i < this->maxBufferSize; ++i)
            this->readBuffer[i] = 0;
    }

}