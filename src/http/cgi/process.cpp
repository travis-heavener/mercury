#include "process.hpp"

#ifdef _WIN32
    #include "../../winheader.hpp"
#else
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include <map>

#include "../../conf/conf.hpp"
#include "../../logs/logger.hpp"
#include "../../util/string_tools.hpp"
#include "../../util/toolbox.hpp"

// Platform specifics
namespace http::cgi {

    // Lazily infer Content-Type
    void inferContentType(const std::string& body, Response& res) {
        if (body[0] == '{' || body[0] == '[') { // JSON
            res.setContentType("application/json");
        } else if (body[0] == '<') { // HTML or XML
            res.setContentType(body.find("<html") != std::string::npos ? "text/html" : "application/xml");
        } else if (isMostlyAscii(body)) { // plaintext
            res.setContentType("text/plain; charset=utf-8");
        } else { // BLOB
            res.setContentType("application/octet-stream");
        }
    }

    // Take platform independent input and load the env block
    void loadEnvBlock(env_block_t& envBlock, const File& file, const Request& req) {
        // Collect all envs
        std::map<std::string, std::string, std::less<>> envsMap;

        const std::string* authHeader = req.getHeader("Authorization");
        if (authHeader != nullptr) {
            const size_t authHeaderSpaceIndex = authHeader->find(' ');
            envsMap["AUTH_TYPE"] = authHeader->substr(0, authHeaderSpaceIndex);
            envsMap["REMOTE_USER"] = authHeaderSpaceIndex == std::string::npos ?
                "" : authHeader->substr(authHeaderSpaceIndex+1);
        } else {
            envsMap["AUTH_TYPE"] = envsMap["REMOTE_USER"] = "";
        }

        const size_t contentLen = req.getBody().size();
        if (contentLen > 0) {
            envsMap["CONTENT_LENGTH"] = std::to_string(contentLen);

            const std::string* contentTypeHeader = req.getHeader("Content-Type");
            if (contentTypeHeader != nullptr)
                envsMap["CONTENT_TYPE"] = *contentTypeHeader;
        }

        envsMap["GATEWAY_INTERFACE"] = "CGI/1.1";
        envsMap["PATH_INFO"] = envsMap["PATH_TRANSLATED"] = "";
        envsMap["QUERY_STRING"] = file.queryStr;

        envsMap["REMOTE_ADDR"] = envsMap["REMOTE_HOST"] = req.getIPStr();
        envsMap["REMOTE_IDENT"] = "";

        envsMap["REQUEST_METHOD"] = req.getMethodStr() == "HEAD" ? "GET" : req.getMethodStr(); // Map HEAD to GET
        envsMap["REQUEST_URI"] = file.rawPath;

        envsMap["SCRIPT_FILENAME"] = file.path;
        envsMap["SCRIPT_NAME"] = file.rawPath; // File.rawPath ignores query string

        const std::string* hostHeader = req.getHeader("Host");
        envsMap["SERVER_NAME"] = hostHeader == nullptr ? "" : *hostHeader;
        envsMap["SERVER_PORT"] = std::to_string( req.usesHTTPS() ? conf::TLS_PORT : conf::PORT );
        envsMap["SERVER_PROTOCOL"] = req.getVersion();
        envsMap["SERVER_SOFTWARE"] = "Mercury/" + conf::VERSION.substr(9); // Skip "Mercury v"

        envsMap["HTTPS"] = req.usesHTTPS() ? "1" : "";
        envsMap["REDIRECT_STATUS"] = "200";
        envsMap["DOCUMENT_ROOT"] = conf::DOCUMENT_ROOT.string();

        // Filter through headers
        for (auto& [key, val] : req.getHeaders()) {
            std::string cgiKey = "HTTP_" + key; 
            for (char& c : cgiKey) if (c == '-') c = '_';
            strToUpper(cgiKey);

            // Ignore Authorization header (already passed)
            if (cgiKey != "HTTP_AUTHORIZATION" && cgiKey != "HTTP_CONTENT_LENGTH" && cgiKey != "HTTP_CONTENT_TYPE")
                envsMap[cgiKey] = val;
        }

        // Platform dependent, create env block
        #ifdef _WIN32 // Windows
            // Stitch double-null terminated env block
            envBlock.clear();

            for (auto& [key, val] : envsMap) {
                const std::wstring wkey(key.begin(), key.end());
                const std::wstring wval(val.begin(), val.end());

                envBlock.insert(envBlock.end(), wkey.begin(), wkey.end());
                envBlock.push_back(L'=');
                envBlock.insert(envBlock.end(), wval.begin(), wval.end());
                envBlock.push_back('\0');
            }

            // Final double-null terminator
            if (envBlock.empty() || envBlock.back() != '\0')
                envBlock.push_back('\0');

            envBlock.push_back('\0');
        #else // Linux
            envBlock.clear();
            for (auto& [key, val] : envsMap)
                envBlock.push_back(key + "=" + val);
        #endif
    }

    // Handles a one-off, CGI request
    void handlePHPRequest(const File& file, const Request& req, Response& res) {
        // Generate env block
        env_block_t envBlock;
        loadEnvBlock(envBlock, file, req);

        // Create Process
        Process process(envBlock);
        if (!process.hasSucceeded()) {
            res.setStatus(502); // Bad Gateway
            return;
        }

        // Send data to Process
        std::string cgiResp;
        process.send(req.getBody(), cgiResp);

        // Separate headers from body
        size_t pos = cgiResp.find("\r\n\r\n");
        if (pos == std::string::npos)
            pos = cgiResp.find("\n\n");

        // Read response
        if (pos != std::string::npos) {
            const std::string headersStr = cgiResp.substr(0, pos);
            const std::string bodyStr = cgiResp.substr(pos + 4);

            // Parse headers
            std::unordered_set<std::string> headers;
            splitStringUnique(headers, headersStr, '\n', true);
            for (std::string line : headers) {
                // Trim carriage return
                if (line.back() == '\r')
                    line.pop_back();

                // Set default status
                res.setStatus(200);

                // Parse headers
                const size_t colonIndex = line.find(':');
                if (colonIndex != std::string::npos) {
                    std::string key = line.substr(0, colonIndex);
                    std::string val = line.substr(colonIndex + 1);

                    trimString(key); trimString(val);
                    formatHeaderCasing(key);

                    if (key == "Status")
                        res.setStatus(std::stoi(val));
                    else
                        res.setHeader(key, val);
                }
            }

            // Set body
            res.setBodyStream( std::unique_ptr<IBodyStream>( new MemoryStream(bodyStr) ) );

            // Force set Content-Length
            res.setHeader("Content-Length", std::to_string(bodyStr.size()));

            // Check Content-Type
            if (bodyStr.size() == 0)
                res.clearHeader("Content-Type"); // Clear if no content
            else if (res.getContentType() == "text/html; charset=UTF-8")
                inferContentType(bodyStr, res); // Infer if default MIME is set
        } else {
            // Fallback if no headers, just return body
            if (cgiResp.size() == 0) {
                res.setStatus(204);
                res.setHeader("Content-Length", "0");
            } else {
                res.setStatus(200);
                res.setBodyStream( std::unique_ptr<IBodyStream>( new MemoryStream(cgiResp) ) );
                res.setHeader("Content-Length", std::to_string(cgiResp.size()));
                inferContentType(cgiResp, res);
            }
        }
    }

    /************************************************************************/
    /*                          PLATFORM SPECIFICS                          */
    /************************************************************************/

    #ifdef _WIN32 // Windows piping functions
        // Writes string to handle
        bool writeToPipe(HANDLE hPipe, const std::string& data) {
            DWORD written = 0;
            return WriteFile(hPipe, data.c_str(), (DWORD)data.size(), &written, NULL) && written == data.size();
        }

        // Reads everything from a pipe until EOF
        void readFromPipe(HANDLE hPipe, std::string& result) {
            char buffer[4096];
            DWORD bytesRead;
            while (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0)
                result.append(buffer, bytesRead);
        }

        // Creates the pipes for the worker
        bool Process::createPipes() {
            // Init security attribs
            SECURITY_ATTRIBUTES sa;
            ZeroMemory(&sa, sizeof(sa));
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = NULL;

            // Create pipes
            if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0)) return false;
            if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0)) return false;

            // Ensure write end of stdin and read end of stdout are not inherited
            SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

            // Return success
            return true;
        }

        // Closes pipes on a worker
        void Process::closePipes() {
            if (stdinRead != NULL) CloseHandle(stdinRead);
            if (stdinWrite != NULL) CloseHandle(stdinWrite);
            if (stdoutRead != NULL) CloseHandle(stdoutRead);
            if (stdoutWrite != NULL) CloseHandle(stdoutWrite);
        }

        // Spawns a new CGI process
        bool Process::spawnCGIProcess(env_block_t& envBlock) {
            // Configure STARTUPINFO to redirect stdin/stdout
            STARTUPINFOW si;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(STARTUPINFOW);
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
            si.hStdOutput = stdoutWrite;
            si.hStdInput = stdinRead;
            si.dwFlags |= STARTF_USESTDHANDLES;

            // Init process information
            ZeroMemory(&procInfo, sizeof(procInfo));

            // Start CGI process
            std::wstring phpCgiPathStr = conf::PHP_CGI_EXE_PATH.wstring();
            if (!CreateProcessW(
                    phpCgiPathStr.data(), NULL, NULL, NULL,
                    TRUE, // Inherit handles
                    CREATE_UNICODE_ENVIRONMENT,
                    envBlock.data(), // Env
                    NULL,
                    &si, &procInfo)) {
                ERROR_LOG << "Failed to spawn PHP CGI worker process" << std::endl;
                return false;
            }

            // Close unneeded pipes
            CloseHandle(stdoutWrite); stdoutWrite = NULL;
            CloseHandle(stdinRead); stdinRead = NULL;

            // Base case, return success
            return true;
        }

        // Close the spawned process
        void Process::closeProcess() {
            WaitForSingleObject(procInfo.hProcess, INFINITE);
            CloseHandle(procInfo.hProcess);
            CloseHandle(procInfo.hThread);
        }

        // Sends the string of text (from loadCGIRequestToString) to the CGI
        void Process::send(const std::string& input, std::string& outBuffer) {
            // Write request body to stdin
            writeToPipe(stdinWrite, input);

            // Close write end to send EOF to CGI
            CloseHandle(stdinWrite);
            stdinWrite = NULL;

            // Read response from CGI
            readFromPipe(stdoutRead, outBuffer);

            // After reading, also close the read end
            CloseHandle(stdoutRead);
            stdoutRead = NULL;
        }
    #else // Linux piping functions
        // Writes string to fd
        static bool writeToPipe(int fd, const std::string& data) {
            ssize_t written = 0, total = 0;

            while (total < (ssize_t)data.size()) {
                written = ::write(fd, data.data() + total, data.size() - total);
                if (written <= 0) return false;
                total += written;
            }

            return true;
        }

        // Reads until EOF from fd
        static void readFromPipe(int fd, std::string& result) {
            char buffer[4096];
            ssize_t bytesRead;

            while ((bytesRead = ::read(fd, buffer, sizeof(buffer))) > 0)
                result.append(buffer, bytesRead);
        }

        // Create pipes for worker
        bool Process::createPipes() {
            int inPipe[2]; // Parent writes -> child reads
            int outPipe[2]; // Child writes -> parent reads

            if (pipe(inPipe) < 0) return false;
            if (pipe(outPipe) < 0) {
                close(inPipe[0]); close(inPipe[1]);
                return false;
            }

            stdinRead = inPipe[0]; stdinWrite = inPipe[1];
            stdoutRead = outPipe[0]; stdoutWrite = outPipe[1];
            return true;
        }

        // Close pipes
        void Process::closePipes() {
            if (stdinRead  >= 0) close(stdinRead);
            if (stdinWrite >= 0) close(stdinWrite);
            if (stdoutRead >= 0) close(stdoutRead);
            if (stdoutWrite>= 0) close(stdoutWrite);

            stdinRead = stdinWrite = stdoutRead = stdoutWrite = -1;
        }

        // Spawn CGI process
        bool Process::spawnCGIProcess(env_block_t& envBlock) {
            int stderrPipe[2]; // separate pipe for STDERR
    if (pipe(stderrPipe) < 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(stderrPipe[0]); close(stderrPipe[1]);
        return false;
    }

            if (pid == 0) { // This is the child
                // Open pipes
                dup2(stdinRead, STDIN_FILENO);
                dup2(stdoutWrite, STDOUT_FILENO);
                dup2(stderrPipe[1], STDERR_FILENO); // Redirect to pipe

                // Close unused fds in child
                close(stdinRead); close(stdoutWrite); close(stdinWrite); close(stdoutRead);
                close(stderrPipe[0]); close(stderrPipe[1]);

                // Prepare argv
                const char* phpCgiPath = "php-cgi"; // Resolved from PATH
                char* const argv[] = { (char*)phpCgiPath, nullptr };

                // Build envp from envBlock
                std::vector<char*> envp;
                for (auto& entry : envBlock)
                    envp.push_back(const_cast<char*>(entry.c_str()));
                envp.push_back(nullptr);

                // execve replaces process
                execvpe(phpCgiPath, argv, envp.data());

                // Exit if execve fails
                ERROR_LOG << "Failed to spawn PHP CGI worker process" << std::endl;
                closePipes(); // Close pipes since this function doesn't return as the child
                _exit(127);
            } else { // This is the Parent
                childPid = pid;

                // Close child-side fds
                close(stdinRead); stdinRead = -1;
                close(stdoutWrite); stdoutWrite = -1;

                // Pipe stderr to stdout
                close(stderrPipe[1]);
                stderrRead = stderrPipe[0];

                return true;
            }
        }

        // Wait for process cleanup
        void Process::closeProcess() {
            if (childPid > 0) {
                int status;
                waitpid(childPid, &status, 0);
                childPid = -1;
            }
        }

        // Send input and read response
        void Process::send(const std::string& input, std::string& outBuffer) {
            writeToPipe(stdinWrite, input);
            close(stdinWrite); stdinWrite = -1;

            readFromPipe(stdoutRead, outBuffer);
            close(stdoutRead); stdoutRead = -1;

            // Concat stderr to stdout
            readFromPipe(stderrRead, outBuffer);
            close(stderrRead); stderrRead = -1;
        }
    #endif

    /***********************************************************************/
    /************************ Process class methods ************************/
    /***********************************************************************/

    Process::Process(env_block_t& envBlock) {
        // Create pipes
        if (!createPipes()) {
            closePipes(); // Close pipes (still open despite failure)
            ERROR_LOG << "Failed to configure PHP CGI worker pipes" << std::endl;
            return;
        }
        
        // Spawn CGI process
        if (!spawnCGIProcess(envBlock)) {
            closePipes(); // Close pipes
            // Error is logged in spawnCGIProcess
            return;
        }

        // Mark the Process as successful
        this->isSuccess = true;
    }

    Process::~Process() {
        // If the Process failed to start, cleanup is already done
        if (!this->isSuccess) return;

        closePipes(); // Close pipes
        closeProcess(); // Close process
    }

}