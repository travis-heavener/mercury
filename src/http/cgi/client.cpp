#include "client.hpp"

// See https://fastcgi-archives.github.io/FastCGI_Specification.html
#define FCGI_VERSION_1          1
#define FCGI_BEGIN_REQUEST      1
#define FCGI_ABORT_REQUEST      2
#define FCGI_END_REQUEST        3
#define FCGI_PARAMS             4
#define FCGI_STDIN              5
#define FCGI_STDOUT             6
#define FCGI_STDERR             7

#define FCGI_RESPONDER          1

#define FCGI_FLAG_CLOSE         0
#define FCGI_FLAG_KEEP_ALIVE    1

namespace http {

    namespace fcgi {

        bool Client::connectServer() {
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) return false;

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);

            if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
                return false;

            if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0)
                return false;

            return true;
        }

        void Client::close() {
            if (sock >= 0) {
                #ifdef _WIN32
                    closesocket(sock);
                #else
                    ::close(sock);
                #endif
                sock = -1;
            }
        }

        bool Client::sendRecord(uint8_t type, uint16_t requestId, const std::vector<uint8_t>& content) {
            // Create header
            Header header;

            header.version = FCGI_VERSION_1;
            header.type = type;
            header.requestId = htons(requestId);
            header.contentLength = htons(content.size());
            header.paddingLength = 0;
            header.reserved = 0;

            // Copy and send header
            std::vector<char> buffer(sizeof(header) + content.size());
            memcpy(buffer.data(), &header, sizeof(header));
            if (!content.empty())
                memcpy(buffer.data() + sizeof(header), content.data(), content.size());

            return ::send(sock, buffer.data(), buffer.size(), 0) == (ssize_t)buffer.size();
        }

        bool Client::beginRequest(uint16_t requestId) {
            BeginRequestBody body{};
            body.role = htons(FCGI_RESPONDER);
            body.flags = FCGI_FLAG_CLOSE; // FCGI_FLAG_CLOSE or FCGI_FLAG_KEEP_ALIVE

            std::vector<uint8_t> content(sizeof(body));
            memcpy(content.data(), &body, sizeof(body));

            return sendRecord(FCGI_BEGIN_REQUEST, requestId, content);
        }

        std::vector<uint8_t> Client::makeNameValuePair(const std::string& name, const std::string& value) {
            std::vector<uint8_t> buf;
            auto appendLen = [&](size_t len) {
                if (len < 128) {
                    buf.push_back((uint8_t)len);
                } else {
                    uint32_t tmp = htonl(len | 0x80000000);
                    uint8_t* p = (uint8_t*)&tmp;
                    buf.insert(buf.end(), p, p + 4);
                }
            };

            appendLen(name.size());
            appendLen(value.size());

            buf.insert(buf.end(), name.begin(), name.end());
            buf.insert(buf.end(), value.begin(), value.end());

            return buf;
        }

        bool Client::sendParam(const std::string& key, const std::string& value, uint16_t requestId) {
            std::vector<uint8_t> content = makeNameValuePair(key, value);
            return sendRecord(FCGI_PARAMS, requestId, content);
        }

        bool Client::endParams(uint16_t requestId) {
            std::vector<uint8_t> empty;
            return sendRecord(FCGI_PARAMS, requestId, empty);
        }

        bool Client::sendStdin(const std::string& body, uint16_t requestId) {
            std::vector<uint8_t> content(body.begin(), body.end());
            return sendRecord(FCGI_STDIN, requestId, content);
        }

        bool Client::endStdin(uint16_t requestId) {
            std::vector<uint8_t> empty;
            return sendRecord(FCGI_STDIN, requestId, empty);
        }

        std::string Client::readResponse() {
            std::string result;
            char headerBuf[8]; // Size of a FastCGI record header

            // Indefinitely poll response
            while (true) {
                ssize_t n = recv(sock, headerBuf, 8, MSG_WAITALL);
                if (n <= 0) break;

                Header header;
                memcpy(&header, headerBuf, 8);

                // Extract content
                uint16_t contentLen = ntohs(header.contentLength);
                if (contentLen > 0) {
                    std::vector<char> content(contentLen);
                    recv(sock, content.data(), contentLen, MSG_WAITALL);

                    if (header.type == FCGI_STDOUT)
                        result.append(content.begin(), content.end());
                }

                // Handle padding
                if (header.paddingLength > 0) {
                    std::vector<char> pad(header.paddingLength);
                    recv(sock, pad.data(), header.paddingLength, MSG_WAITALL);
                }

                if (header.type == FCGI_END_REQUEST)
                    break;
            }
            return result;
        }

        // Dedicated PHP handler
        void handlePHPRequest(const File& file, const Request& req, Response& res) {
            // Create per-request client
            Client fcgi(FASTCGI_HOST, conf::PHP_FPM_PORT);

            if (!fcgi.connectServer()) {
                res.setStatus(502); // Bad Gateway
                return;
            }

            fcgi.beginRequest();

            // Map request to FastCGI params
            const std::string contentType = req.getHeader("Content-Type") ? *req.getHeader("Content-Type") : "text/plain";

            fcgi.sendParam("DOCUMENT_ROOT", conf::DOCUMENT_ROOT.string());
            fcgi.sendParam("SCRIPT_FILENAME", file.path);
            fcgi.sendParam("REQUEST_METHOD", req.getMethodStr());
            fcgi.sendParam("QUERY_STRING", file.queryStr);
            fcgi.sendParam("SERVER_SOFTWARE", conf::VERSION);
            fcgi.sendParam("GATEWAY_INTERFACE", "CGI/1.1");
            fcgi.sendParam("REQUEST_URI", file.rawPath);
            fcgi.sendParam("CONTENT_TYPE", contentType);
            fcgi.sendParam("SERVER_PROTOCOL", req.getVersion());
            fcgi.sendParam("REMOTE_ADDR", req.getIPStr());
            fcgi.sendParam("CONTENT_LENGTH", std::to_string(req.getBody().size()));

            if (file.MIME != MIME_UNSET)
                fcgi.sendParam("CONTENT_TYPE", file.MIME);

            // Passthru headers
            for (auto& [key, val] : req.getHeaders()) {
                std::string cgiKey = "HTTP_" + key; 
                for (char& c : cgiKey) if (c == '-') c = '_';
                fcgi.sendParam(cgiKey, val);
            }

            fcgi.endParams();

            // Passthru body
            if (!req.getBody().empty())
                fcgi.sendStdin(req.getBody());

            fcgi.endStdin();

            // Read response
            std::string fcgiResp = fcgi.readResponse();

            // Separate headers from body
            auto pos = fcgiResp.find("\r\n\r\n");
            if (pos != std::string::npos) {
                std::string headersStr = fcgiResp.substr(0, pos);
                std::string bodyStr = fcgiResp.substr(pos + 4);

                // Initially set content-type
                res.setHeader("Content-Type", "text/plain");

                // Parse headers
                std::unordered_set<std::string> headers;
                splitStringUnique(headers, headersStr, '\n', true);
                for (const std::string& _line : headers) {
                    std::string line( _line ); // Copy string

                    // Trim carriage return
                    if (line.back() == '\r')
                        line.pop_back();

                    // Set default status
                    res.setStatus(200);

                    // Parse headers
                    size_t colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string key = line.substr(0, colon);
                        std::string val = line.substr(colon + 1);

                        trimString(key);
                        trimString(val);

                        if (key == "Status")
                            res.setStatus(std::stoi(val));
                        else
                            res.setHeader(key, val);

                        if (key == "Status" && std::stoi(val) == 404) {
                            ERROR_LOG << file.path << std::endl;
                        }
                    }
                }

                // Set body
                res.setBody(bodyStr);

                // Force set Content-Length
                res.setHeader("Content-Length", std::to_string(bodyStr.size()));
            } else {
                // Fallback if no headers, just return body
                res.setContentType("text/html");
                res.setStatus(200);
                res.setBody(fcgiResp);
                res.setHeader("Content-Length", std::to_string(fcgiResp.size()));
            }

            fcgi.close();
        }

    }

}