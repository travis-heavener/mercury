#include "server.hpp"

namespace HTTP {

    void Server::kill() {
        #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            if (this->c_sock != -1 && !closesocket(this->c_sock))
                std::cout << "Client socket closed.\n";

            if (this->sock != -1 && !closesocket(this->sock))
                std::cout << "Socket closed.\n";
        #else
            if (this->c_sock != -1 && !close(this->c_sock))
                std::cout << "Client socket closed.\n";

            if (this->sock != -1 && !close(this->sock))
                std::cout << "Socket closed.\n";
        #endif
    }

    // Initialize the socket
    int Server::init() {
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

    void Server::handleReqs() {
        // Accept requests from clients
        struct sockaddr_in clientAddr;
        struct in_addr clientIP;
        socklen_t clientLen = sizeof(clientAddr);
        char clientIPStr[INET_ADDRSTRLEN];

        while ( (this->c_sock = accept(this->sock, (struct sockaddr*)&clientAddr, &clientLen)) >= 0 ) {
            // Read buffer
            recv(this->c_sock, this->readBuffer, sizeof(this->readBuffer), 0);
            clientIP = ((struct sockaddr_in*)&clientAddr)->sin_addr;

            #if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
                strcpy(clientIPStr, inet_ntoa( clientIP ));
            #else
                inet_ntop( AF_INET, &clientIP, clientIPStr, INET_ADDRSTRLEN );
            #endif

            // Parse request
            Request req( this->readBuffer, clientIPStr );
            std::cout << req.getMethodStr() << ' ' << req.getIPStr() << ' ' << req.getPathStr() << '\n';

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
                // Lookup file
                std::string path = (std::filesystem::current_path() / ("public" + req.getPathStr())).string();

                if (!doesFileExist(path)) {
                    // Update body to error document
                    statusLine = "HTTP/1.1 404 Not Found";

                    // If the request allows HTML, return an HTML display
                    if (req.isMIMEAccepted("text/html")) {
                        resHeaders.insert({"CONTENT-TYPE", "text/html"});
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
                        resHeaders.insert({"CONTENT-TYPE", "text/html"});
                        loadErrorDoc(406, "Not Acceptable", body);
                    }
                    break;
                }

                // MIME matches, resolve resource
                if (file.loadToBuffer(body) == IO_FAILURE) {
                    statusLine = "HTTP/1.1 500 Internal Server Error";

                    if (req.isMIMEAccepted("text/html")) {
                        resHeaders.insert({"CONTENT-TYPE", "text/html"});
                        loadErrorDoc(500, "Internal Server Error", body);
                    }
                } else {
                    // Body loaded successfully
                    statusLine = "HTTP/1.1 200 OK";
                    resHeaders.insert({"CONTENT-TYPE", file.MIME});
                }
                break;
            }
            default: {
                statusLine = "HTTP/1.1 405 Method Not Allowed";
                resHeaders.insert({"CONTENT-TYPE", "text/html"});
                loadErrorDoc(405, "Method Not Allowed", body);
                break;
            }
        }

        // Handle compression
        #if __linux__
            if (req.isEncodingAccepted("deflate") && (resHeaders.find("CONTENT-TYPE") != resHeaders.end() &&
                (resHeaders.find("CONTENT-TYPE")->second.find("text/") == 0 || resHeaders.find("CONTENT-TYPE")->second == "application/json"))) {
                // Create compression buffer
                const size_t bodySize = sizeof(char) * body.size();
                char* buffer = (char*)malloc(bodySize);
                deflateText(body.c_str(), bodySize, buffer, bodySize);

                // Only use compressed body if it's smaller
                if (std::strlen(buffer) < body.size()) {
                    body = buffer;

                    // Append compression header
                    resHeaders.insert({"CONTENT-ENCODING", "deflate"});
                }
                free(buffer);
            }
        #endif

        // Compile output buffer
        resHeaders.insert({"CONTENT-LENGTH", std::to_string(body.size())});
        std::string headers = "";
        for (auto [headerName, value] : resHeaders)
            headers += headerName + ": " + value + '\n';
        buffer = statusLine + '\n' + headers + "\n" + body;
    }

}