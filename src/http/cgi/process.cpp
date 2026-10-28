#include "process.hpp"

#ifdef _WIN32
    #include "../../winheader.hpp"
#else
    #include <cerrno>
    #include <csignal>
    #include <fcntl.h>
    #include <poll.h>
    #include <spawn.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <map>
#include <optional>
#include <vector>

#include "../../conf/conf.hpp"
#include "../../logs/logger.hpp"
#include "../../io/file_tools.hpp"
#include "../../util/string_tools.hpp"
#include "../../util/toolbox.hpp"

#ifndef _WIN32
    extern std::atomic<bool> isExiting;
#endif

// Platform specifics
namespace http::cgi {

    // Lazily infer Content-Type
    void inferContentType(const std::string& filePath, Response& res) {
        // Set default Content-Type
        res.setContentType("application/octet-stream");

        // Attempt to open temp file
        std::ifstream handle( filePath, std::ios::binary );
        if (!handle.is_open()) return;

        // Read in chunks
        const int bufferSize = 4096;
        char buffer[bufferSize] = {0};

        bool isFirstLine = true;
        size_t numAsciiChars = 0;
        size_t numChars = 0;
        while (!handle.eof()) {
            handle.read(buffer, bufferSize);
            size_t bytesRead = handle.gcount();
            numChars += bytesRead;

            // Check first line
            if (isFirstLine) {
                isFirstLine = false;
                if (buffer[0] == '{' || buffer[0] == '[') { // JSON
                    res.setContentType("application/json");
                    handle.close();
                    return;
                } else if (buffer[0] == '<') { // HTML or XML
                    res.setContentType(std::string(buffer, bytesRead).find("<html") != std::string::npos ? "text/html" : "application/xml");
                    return;
                }
            }

            // Otherwise, count ASCII
            numAsciiChars += countAscii(buffer, bytesRead);
        }

        // Evaluate ascii chars
        if (((long double)numAsciiChars / numChars) >= 0.95) // Plaintext
            res.setContentType("text/plain; charset=utf-8");
    }

    // Take platform independent input and load the env block
    void loadEnvBlock(env_block_t& envBlock, const File& file, const Request& req) {
        // Collect all envs
        std::map<std::string, std::string, std::less<>> envsMap;

        const std::optional<std::string> authHeader = req.getHeader("Authorization");
        if (authHeader.has_value()) {
            const size_t authSpaceInd = authHeader->find(' ');
            envsMap["AUTH_TYPE"] = authHeader->substr(0, authSpaceInd);
            envsMap["REMOTE_USER"] = authSpaceInd == std::string::npos ?
                "" : authHeader->substr(authSpaceInd+1);
        } else {
            envsMap["AUTH_TYPE"] = envsMap["REMOTE_USER"] = "";
        }

        const size_t contentLen = req.getBody().size();
        if (contentLen > 0) {
            envsMap["CONTENT_LENGTH"] = std::to_string(contentLen);

            const std::optional<std::string> contentTypeHeader = req.getHeader("Content-Type");
            if (contentTypeHeader.has_value())
                envsMap["CONTENT_TYPE"] = *contentTypeHeader;
        }

        envsMap["GATEWAY_INTERFACE"] = "CGI/1.1";
        envsMap["PATH_INFO"] = file.phpPathInfo;
        envsMap["PATH_TRANSLATED"] = file.phpPathInfo.empty() ? "" : (conf::DOCUMENT_ROOT / file.phpPathInfo.substr(1)).string();
        envsMap["QUERY_STRING"] = file.queryString;

        envsMap["REMOTE_ADDR"] = envsMap["REMOTE_HOST"] = req.getIPStr();
        envsMap["REMOTE_IDENT"] = "";

        envsMap["REQUEST_METHOD"] = req.getMethodStr() == "HEAD" ? "GET" : req.getMethodStr(); // Map HEAD to GET
        envsMap["REQUEST_URI"] = file.decodedURIWithoutPathInfo;

        envsMap["SCRIPT_FILENAME"] = file.absoluteResourcePath;
        envsMap["SCRIPT_NAME"] = file.decodedURIWithoutPathInfo; // File.decodedURIWithoutPathInfo ignores query string

        const std::optional<std::string> hostHeader = req.getHeader("Host");
        envsMap["SERVER_NAME"] = hostHeader.has_value() ? *hostHeader : "";
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
            // Sort Windows environment names case-insensitively as required by CreateProcessW
            struct EnvironmentNameLess {
                bool operator()(const std::wstring& left, const std::wstring& right) const {
                    return _wcsicmp(left.c_str(), right.c_str()) < 0;
                }
            };
            std::map<std::wstring, std::wstring, EnvironmentNameLess> windowsEnvironment;

            // Preserve OS runtime context omitted by CGI-only environment
            // Keep TEMP/TMP consistent so OPcache can initialize shared memory on Windows
            // Read UTF-16 values directly to preserve non-ASCII Windows paths
            for (const wchar_t* pName : {L"SystemRoot", L"WINDIR", L"TEMP", L"TMP", L"PATH",
                                        L"COMSPEC", L"USERPROFILE", L"APPDATA", L"LOCALAPPDATA",
                                        L"PHPRC", L"PHP_INI_SCAN_DIR"}) {
                DWORD capacity = GetEnvironmentVariableW(pName, nullptr, 0);
                while (capacity > 0) {
                    std::wstring value(capacity, L'\0');
                    const DWORD length = GetEnvironmentVariableW(pName, value.data(), capacity);
                    if (length == 0) break;
                    if (length >= capacity) { capacity = length; continue; }
                    value.resize(length);
                    windowsEnvironment[pName] = std::move(value);
                    break;
                }
            }

            // Select CGI config through environment even when Windows CGI ignores -c
            // Keep external scan directories from reloading OPcache into private config
            const auto legacyConfig = conf::PHP_CGI_EXE_PATH.parent_path() / "mercury-cgi.ini";
            std::error_code configError;
            if (std::filesystem::is_regular_file(legacyConfig, configError)) {
                windowsEnvironment[L"PHPRC"] = legacyConfig.wstring();
                windowsEnvironment[L"PHP_INI_SCAN_DIR"] = L"";
            }

            // Overlay request-specific variables without inheriting stale CGI fields
            for (const auto& [key, val] : envsMap)
                windowsEnvironment[std::wstring(key.begin(), key.end())] = std::wstring(val.begin(), val.end());

            // Stitch sorted, double-null-terminated UTF-16 environment block
            envBlock.clear();
            for (const auto& [key, val] : windowsEnvironment) {
                envBlock.insert(envBlock.end(), key.begin(), key.end());
                envBlock.push_back(L'=');
                envBlock.insert(envBlock.end(), val.begin(), val.end());
                envBlock.push_back(L'\0');
            }
            if (envBlock.empty()) envBlock.push_back(L'\0');
            envBlock.push_back(L'\0');
        #else // Linux
            envBlock.clear();
            for (auto& [key, val] : envsMap)
                envBlock.push_back(key + "=" + val);
        #endif
    }

    // Handles a one-off, CGI request
    void handlePHPRequest(const File& file, const Request& req, Response& res) {
        // Create temp file
        std::string tmpPath;
        if (!createTempFile(tmpPath)) {
            res.setStatus(500);
            return;
        }

        std::ofstream tmpOutHandle( tmpPath, std::ios::binary );
        if (!tmpOutHandle.is_open()) {
            ERROR_LOG << "Failed to open temp file: " << tmpPath << std::endl;
            res.setStatus(500);
            removeTempFile(tmpPath);
            return;
        }

        // Generate env block
        env_block_t envBlock;
        loadEnvBlock(envBlock, file, req);

        // Create Process
        Process process(envBlock);
        if (!process.hasSucceeded()) {
            res.setStatus(502); // Bad Gateway
            tmpOutHandle.close();
            removeTempFile(tmpPath);
            return;
        }

        // Send data to Process
        process.send(req.getBody(), tmpOutHandle);

        // Reject failed worker capture instead of treating crash as empty success
        if (!tmpOutHandle) {
            res.setStatus(process.hasTimedOut() ? 504 : 502);
            tmpOutHandle.close();
            removeTempFile(tmpPath);
            return;
        }

        // Read from temp file
        tmpOutHandle.close();
        std::ifstream tmpInHandle( tmpPath, std::ios::binary );
        if (!tmpInHandle.is_open()) {
            ERROR_LOG << "Failed to open temp file: " << tmpPath << std::endl;
            res.setStatus(500);
            removeTempFile(tmpPath);
            return;
        }

        // Create new temp file for the body
        std::string tmpBodyPath;
        if (!createTempFile(tmpBodyPath)) {
            res.setStatus(500);
            tmpInHandle.close();
            removeTempFile(tmpPath);
            return;
        }

        std::ofstream tmpBodyHandle( tmpBodyPath, std::ios::binary );
        if (!tmpBodyHandle.is_open()) {
            ERROR_LOG << "Failed to open temp file: " << tmpBodyPath << std::endl;
            res.setStatus(500);
            tmpInHandle.close();
            removeTempFile(tmpPath);
            removeTempFile(tmpBodyPath);
            return;
        }

        // Separate headers from body
        bool hasWrittenHeaders = false;
        std::vector<std::pair<std::string, std::string>> parsedHeaders;
        uint16_t cgiStatus = 200;
        std::string line;

        // Reject malformed CGI output with gateway response instead of closing HTTP connection
        auto rejectHeader = [&]() {
            // Bound diagnostic size and escape controls to keep each log entry on one line
            std::string preview;
            constexpr char HEX[] = "0123456789ABCDEF";
            const size_t limit = std::min(line.size(), static_cast<size_t>(256));
            for (size_t i = 0; i < limit; ++i) {
                const unsigned char c = static_cast<unsigned char>(line[i]);
                if (c < 32 || c == 127) {
                    preview += "\\x";
                    preview += HEX[c >> 4];
                    preview += HEX[c & 0x0F];
                } else {
                    preview += static_cast<char>(c);
                }
            }
            ERROR_LOG << "Invalid PHP CGI response header: " << preview << (line.size() > limit ? "..." : "") << std::endl;
            res.setStatus(502);
            tmpBodyHandle.close();
            tmpInHandle.close();
            removeTempFile(tmpPath);
            removeTempFile(tmpBodyPath);
        };
        while (std::getline(tmpInHandle, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (line.empty()) break;

            // Reading first header
            if (!hasWrittenHeaders) {
                hasWrittenHeaders = true;
                res.setStatus(200); // Set default status
            }

            // Read header
            const size_t colonIndex = line.find(':');
            if (colonIndex != std::string::npos) {
                std::string key = line.substr(0, colonIndex);
                std::string val = line.substr(colonIndex + 1);

                trimString(key); trimString(val);
                formatHeaderCasing(key);

                if (!isValidHeaderName(key) || !isValidHeaderValue(val)) {
                    rejectHeader();
                    return;
                }

                if (key == "Status") {
                    // Accept three-digit status plus optional reason phrase without throwing
                    if (val.size() < 3 || val[0] < '1' || val[0] > '5' ||
                        val[1] < '0' || val[1] > '9' || val[2] < '0' || val[2] > '9' ||
                        (val.size() > 3 && val[3] != ' ' && val[3] != '\t')) {
                        rejectHeader();
                        return;
                    }
                    cgiStatus = static_cast<uint16_t>((val[0] - '0') * 100 + (val[1] - '0') * 10 + val[2] - '0');
                } else {
                    parsedHeaders.emplace_back(std::move(key), std::move(val));
                }
            }
        }

        // Publish CGI headers only after validating complete header block
        // Keep partial backend headers out of gateway error responses
        if (hasWrittenHeaders) {
            res.setStatus(cgiStatus);
            for (const auto& [key, val] : parsedHeaders)
                res.setHeader(key, val);
        }

        // Read remainder of the body to the body temp file
        tmpBodyHandle << tmpInHandle.rdbuf();

        // Close file handles
        tmpBodyHandle.close();
        tmpInHandle.close();

        // Fallback if no headers, just return body
        if (!hasWrittenHeaders) {
            // Check size of the raw CGI response (tmpPath)
            if (std::filesystem::file_size(tmpPath) == 0) {
                res.setStatus(204);
                res.setHeader("Content-Length", "0");
                removeTempFile(tmpBodyPath);
            } else {
                res.setStatus(200);
                res.setBodyStream( std::unique_ptr<IBodyStream>( new FileStream(tmpBodyPath, true) ) );
                res.setHeader("Content-Length", std::to_string( std::filesystem::file_size(tmpBodyPath) ));
                inferContentType(tmpBodyPath, res);
            }
        } else {
            // Update the body stream
            res.setBodyStream( std::unique_ptr<IBodyStream>( new FileStream(tmpBodyPath, true) ) );

            // Force set Content-Length
            const size_t bodySize = std::filesystem::file_size(tmpBodyPath);
            res.setHeader("Content-Length", std::to_string( bodySize ));

            // Check Content-Type
            if (bodySize == 0)
                res.clearHeader("Content-Type"); // Clear if no content
            else if (res.getContentType() == "text/html; charset=UTF-8")
                inferContentType(tmpBodyPath, res); // Infer if default MIME is set
        }

        // Remove original temp file
        removeTempFile(tmpPath);
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
        void readFromPipe(HANDLE hPipe, std::ofstream& tmpHandle, PROCESS_INFORMATION& procInfo) {
            char buffer[4096];
            DWORD bytesRead;
            while (true) {
                BOOL isOk = ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL);

                if (isOk && bytesRead > 0) {
                    tmpHandle.write(buffer, bytesRead);
                    continue;
                }

                // ReadFile failed or returned nothing
                DWORD err = GetLastError();
                if (err == ERROR_NO_DATA || err == ERROR_PIPE_LISTENING) {
                    Sleep(1);
                    continue;
                }

                // Check if process exited
                DWORD wait = WaitForSingleObject(procInfo.hProcess, 0);

                if (wait == WAIT_OBJECT_0) {
                    // Child closed, drain any remaining output
                    while (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0)
                        tmpHandle.write(buffer, bytesRead);
                    break;
                }

                // Allow time to wait for response
                Sleep(1);
            }

            // Flush output
            tmpHandle.flush();
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
            // Release startup resources on every return path, including allocation failures
            struct StartupResources {
                HANDLE stderrHandle = INVALID_HANDLE_VALUE;
                std::vector<BYTE> storage;
                LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;

                ~StartupResources() {
                    if (attributes) DeleteProcThreadAttributeList(attributes);
                    if (stderrHandle != INVALID_HANDLE_VALUE) CloseHandle(stderrHandle);
                }
            } resources;

            // Give child valid inheritable stderr without modifying shared parent handle
            HANDLE parentStderr = GetStdHandle(STD_ERROR_HANDLE);
            if (parentStderr && parentStderr != INVALID_HANDLE_VALUE) {
                if (!DuplicateHandle(GetCurrentProcess(), parentStderr, GetCurrentProcess(),
                                     &resources.stderrHandle, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
                    ERROR_LOG << "Failed to duplicate PHP CGI stderr handle: " << GetLastError() << std::endl;
                    return false;
                }
            } else {
                SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
                resources.stderrHandle = CreateFileW(L"NUL", GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (resources.stderrHandle == INVALID_HANDLE_VALUE) {
                    ERROR_LOG << "Failed to open PHP CGI stderr fallback: " << GetLastError() << std::endl;
                    return false;
                }
            }

            // Restrict inheritance to this worker's streams during concurrent CGI launches
            // Keep unrelated request pipes and temporary files out of child processes
            SIZE_T attributeBytes = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
            if (attributeBytes == 0) {
                ERROR_LOG << "Failed to size PHP CGI handle list: " << GetLastError() << std::endl;
                return false;
            }
            resources.storage.resize(attributeBytes);
            auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(resources.storage.data());
            if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)) {
                ERROR_LOG << "Failed to initialize PHP CGI handle list: " << GetLastError() << std::endl;
                return false;
            }
            resources.attributes = attributes;
            HANDLE inheritedHandles[] = {stdinRead, stdoutWrite, resources.stderrHandle};
            if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                          inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr)) {
                ERROR_LOG << "Failed to configure PHP CGI handle list: " << GetLastError() << std::endl;
                return false;
            }

            // Configure STARTUPINFO to redirect stdin/stdout
            STARTUPINFOEXW si;
            ZeroMemory(&si, sizeof(si));
            si.StartupInfo.cb = sizeof(si);
            si.StartupInfo.hStdError = resources.stderrHandle;
            si.StartupInfo.hStdOutput = stdoutWrite;
            si.StartupInfo.hStdInput = stdinRead;
            si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
            si.lpAttributeList = attributes;

            // Init process information
            ZeroMemory(&procInfo, sizeof(procInfo));

            // Start CGI process
            std::wstring phpCgiPathStr = conf::PHP_CGI_EXE_PATH.wstring();
            // Keep config selection in PHPRC rather than CGI command-line parsing
            std::wstring command = L"\"" + phpCgiPathStr + L"\"";
            if (!CreateProcessW(
                    phpCgiPathStr.c_str(), command.data(), NULL, NULL,
                    TRUE, // Inherit only handles from explicit list
                    CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                    envBlock.data(), // Env
                    NULL,
                    &si.StartupInfo, &procInfo)) {
                ERROR_LOG << "Failed to spawn PHP CGI worker process: " << GetLastError() << std::endl;
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
        void Process::send(const std::string& input, std::ofstream& tmpHandle) {
            // Write request body to stdin
            writeToPipe(stdinWrite, input);

            // Close write end to send EOF to CGI
            CloseHandle(stdinWrite);
            stdinWrite = NULL;

            // Read response from CGI
            readFromPipe(stdoutRead, tmpHandle, procInfo);

            // Report crashed or silent Windows workers before handles close
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(procInfo.hProcess, &exitCode)) {
                ERROR_LOG << "Failed to query PHP CGI worker exit code: " << GetLastError() << std::endl;
                tmpHandle.setstate(std::ios::badbit);
            } else if (exitCode != 0 || tmpHandle.tellp() == std::streampos(0)) {
                ERROR_LOG << "PHP CGI worker PID " << procInfo.dwProcessId
                          << " exited with code " << exitCode
                          << ", captured stdout bytes: " << tmpHandle.tellp() << std::endl;
                if (exitCode != 0) tmpHandle.setstate(std::ios::badbit);
            }

            // After reading, also close the read end
            CloseHandle(stdoutRead);
            stdoutRead = NULL;
        }
    #else // Linux piping functions
        // Used to release one descriptor without leaving stale ownership
        static void closePipe(int& fd) {
            if (fd >= 0) close(fd);
            fd = -1;
        }

        // Used to create atomically close-on-exec pipes above standard descriptors
        static bool createWorkerPipe(int fds[2]) {
            if (pipe2(fds, O_CLOEXEC) < 0) return false;

            // Keep spawn redirection safe when Mercury starts w/ closed stdin/stdout
            for (int i = 0; i < 2; ++i) {
                if (fds[i] >= 3) continue;
                const int replacement = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
                if (replacement < 0) {
                    const int error = errno;
                    close(fds[0]); close(fds[1]);
                    errno = error;
                    return false;
                }
                close(fds[i]);
                fds[i] = replacement;
            }
            return true;
        }

        // Used to destroy only successfully initialized spawn settings
        struct SpawnSettings {
            posix_spawn_file_actions_t actions{};
            posix_spawnattr_t attributes{};
            bool actionsReady = false, attributesReady = false;
            int error;

            SpawnSettings() {
                error = posix_spawn_file_actions_init(&actions);
                if (error != 0) return;
                actionsReady = true;
                error = posix_spawnattr_init(&attributes);
                attributesReady = error == 0;
            }

            ~SpawnSettings() {
                if (attributesReady) posix_spawnattr_destroy(&attributes);
                if (actionsReady) posix_spawn_file_actions_destroy(&actions);
            }
        };

        // Used to drain bounded chunks so busy workers cannot bypass timeout checks
        static bool drainWorkerPipe(int& fd, std::ofstream* pOutput, std::string& diagnostic, bool& truncated) {
            char buffer[4096];
            constexpr size_t MAX_DIAGNOSTIC = 4096;
            for (int i = 0; i < 16; ++i) {
                const ssize_t count = read(fd, buffer, sizeof(buffer));
                if (count == 0) { closePipe(fd); return true; }
                if (count < 0) {
                    if (errno == EINTR) continue;
                    return errno == EAGAIN || errno == EWOULDBLOCK;
                }

                if (pOutput != nullptr) {
                    pOutput->write(buffer, count);
                    if (!*pOutput) return false;
                } else {
                    const size_t keep = std::min(static_cast<size_t>(count), MAX_DIAGNOSTIC - diagnostic.size());
                    diagnostic.append(buffer, keep);
                    truncated |= keep < static_cast<size_t>(count);
                }
            }
            return true;
        }

        // Used to log stderr separately from HTTP output without control-character injection
        static void logWorkerDiagnostic(const std::string& diagnostic, const bool truncated) {
            if (diagnostic.empty()) return;
            std::string escaped;
            constexpr char HEX[] = "0123456789ABCDEF";
            for (const unsigned char c : diagnostic) {
                if (c < 32 || c == 127) {
                    escaped += "\\x";
                    escaped += HEX[c >> 4];
                    escaped += HEX[c & 0x0F];
                } else {
                    escaped += static_cast<char>(c);
                }
            }
            ERROR_LOG << "PHP CGI stderr: " << escaped << (truncated ? " [truncated]" : "") << std::endl;
        }

        // Create request pipes that other workers cannot retain across exec
        bool Process::createPipes() {
            int inPipe[2], outPipe[2];
            if (!createWorkerPipe(inPipe)) return false;
            if (!createWorkerPipe(outPipe)) {
                close(inPipe[0]); close(inPipe[1]);
                return false;
            }
            stdinRead = inPipe[0]; stdinWrite = inPipe[1];
            stdoutRead = outPipe[0]; stdoutWrite = outPipe[1];
            return true;
        }

        // Release every owned pipe, including stderr on failed launches
        void Process::closePipes() {
            closePipe(stdinRead); closePipe(stdinWrite);
            closePipe(stdoutRead); closePipe(stdoutWrite);
            closePipe(stderrRead);
        }

        // Spawn without allocating C++ objects in forked multithreaded child
        bool Process::spawnCGIProcess(env_block_t& envBlock) {
            // Prepare request environment before owning child or stderr descriptors
            std::vector<char*> envp;
            envp.reserve(envBlock.size() + 1);
            for (auto& entry : envBlock) envp.push_back(entry.data());
            envp.push_back(nullptr);

            int stderrPipe[2];
            if (!createWorkerPipe(stderrPipe)) return false;
            SpawnSettings settings;
            int error = settings.error;

            // Redirect only this request's standard streams
            const int sources[] = {stdinRead, stdoutWrite, stderrPipe[1]};
            for (int i = 0; i < 3 && error == 0; ++i)
                error = posix_spawn_file_actions_adddup2(&settings.actions, sources[i], i);
            for (const int fd : {stdinRead, stdinWrite, stdoutRead, stdoutWrite, stderrPipe[0], stderrPipe[1]})
                if (error == 0) error = posix_spawn_file_actions_addclose(&settings.actions, fd);

            // Exclude other server descriptors when libc provides closefrom actions
            #ifdef __GLIBC__
                #if __GLIBC_PREREQ(2, 34)
                    if (error == 0) error = posix_spawn_file_actions_addclosefrom_np(&settings.actions, 3);
                #endif
            #endif

            // Own worker process group and restore normal pipe signal behavior
            sigset_t defaults, mask;
            sigemptyset(&defaults); sigaddset(&defaults, SIGPIPE);
            sigemptyset(&mask);
            if (error == 0) error = posix_spawnattr_setsigdefault(&settings.attributes, &defaults);
            if (error == 0) error = posix_spawnattr_setsigmask(&settings.attributes, &mask);
            if (error == 0) error = posix_spawnattr_setpgroup(&settings.attributes, 0);
            if (error == 0)
                error = posix_spawnattr_setflags(&settings.attributes,
                    POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETPGROUP);

            // Use spawn result for launch status, keeping stderr available for diagnostics
            char command[] = "php-cgi";
            char* argv[] = {command, nullptr};
            pid_t pid = -1;
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(conf::CGI_TIMEOUT);
            if (error == 0)
                error = posix_spawnp(&pid, command, &settings.actions, &settings.attributes, argv, envp.data());

            close(stderrPipe[1]);
            if (error != 0) {
                close(stderrPipe[0]);
                ERROR_LOG << "Failed to launch PHP CGI worker: " << std::strerror(error) << std::endl;
                return false;
            }

            // Retain only parent endpoints and make duplex transfers nonblocking
            childPid = pid;
            stderrRead = stderrPipe[0];
            closePipe(stdinRead); closePipe(stdoutWrite);
            for (const int fd : {stdinWrite, stdoutRead, stderrRead}) {
                const int flags = fcntl(fd, F_GETFL, 0);
                if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    ERROR_LOG << "Failed to configure PHP CGI worker I/O: " << std::strerror(errno) << std::endl;
                    closeProcess();
                    return false;
                }
            }
            return true;
        }

        // Terminate unfinished worker group before waiting for process cleanup
        void Process::closeProcess() {
            if (childPid <= 0) return;
            kill(-childPid, SIGKILL);
            // Stop direct worker even if script moved itself to another group
            kill(childPid, SIGKILL);
            int status;
            while (waitpid(childPid, &status, 0) < 0 && errno == EINTR) {}
            childPid = -1;
        }

        // Transfer all three streams concurrently under one request deadline
        void Process::send(const std::string& input, std::ofstream& tmpHandle) {
            size_t offset = 0;
            std::string diagnostic;
            bool truncated = false, succeeded = true;
            if (input.empty()) closePipe(stdinWrite);

            while (childPid > 0) {
                // Cancel promptly during shutdown instead of blocking worker joins
                if (::isExiting.load()) {
                    ERROR_LOG << "PHP CGI worker cancelled during shutdown" << std::endl;
                    succeeded = false;
                    break;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    timedOut = true;
                    ERROR_LOG << "PHP CGI worker timed out after " << conf::CGI_TIMEOUT << " seconds" << std::endl;
                    succeeded = false;
                    break;
                }

                // Reap only after output drains; keep PID reserved while pipes remain open
                if (stdinWrite < 0 && stdoutRead < 0 && stderrRead < 0) {
                    int status;
                    const pid_t result = waitpid(childPid, &status, WNOHANG);
                    if (result == childPid) {
                        childPid = -1;
                        succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                        if (!succeeded)
                            ERROR_LOG << "PHP CGI worker failed, "
                                      << (WIFSIGNALED(status) ? "signal: " : "exit code: ")
                                      << (WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status)) << std::endl;
                        break;
                    }
                    if (result < 0 && errno != EINTR) {
                        ERROR_LOG << "Failed to reap PHP CGI worker: " << std::strerror(errno) << std::endl;
                        childPid = -1;
                        succeeded = false;
                        break;
                    }
                }

                // Poll briefly even when worker closed streams but has not exited
                pollfd fds[] = {{stdinWrite, POLLOUT, 0}, {stdoutRead, POLLIN, 0}, {stderrRead, POLLIN, 0}};
                const int ready = poll(fds, 3, 100);
                if (ready < 0) {
                    if (errno == EINTR) continue;
                    ERROR_LOG << "Failed to poll PHP CGI worker: " << std::strerror(errno) << std::endl;
                    succeeded = false;
                    break;
                }

                // Drain stdout and stderr before sending more request data
                for (int i = 1; i < 3; ++i) {
                    if (fds[i].fd < 0 || fds[i].revents == 0) continue;
                    int& fd = i == 1 ? stdoutRead : stderrRead;
                    if ((fds[i].revents & POLLNVAL) ||
                        !drainWorkerPipe(fd, i == 1 ? &tmpHandle : nullptr, diagnostic, truncated)) {
                        ERROR_LOG << "Failed to capture PHP CGI worker output" << std::endl;
                        succeeded = false;
                        break;
                    }
                }
                if (!succeeded) break;

                // Retry partial writes and close stdin as soon as full body is delivered
                if (stdinWrite >= 0 && fds[0].revents != 0) {
                    const ssize_t count = write(stdinWrite, input.data() + offset, input.size() - offset);
                    if (count > 0) {
                        offset += static_cast<size_t>(count);
                        if (offset == input.size()) closePipe(stdinWrite);
                    } else if (count == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                        ERROR_LOG << "Failed to send request body to PHP CGI worker" << std::endl;
                        succeeded = false;
                        break;
                    }
                }
            }

            logWorkerDiagnostic(diagnostic, truncated);
            if (!succeeded) tmpHandle.setstate(std::ios::badbit);
            closePipes();
            closeProcess();
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
