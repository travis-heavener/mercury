#include "backend.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#ifndef _WIN32
    #include <cerrno>
    #include <csignal>
    #include <dirent.h>
    #include <fcntl.h>
    #include <regex>
    #include <spawn.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <unistd.h>

    extern char** environ;
#endif

#include "../../conf/conf.hpp"
#include "../../logs/logger.hpp"
#include "client.hpp"

namespace http::fastcgi {

    Backend::~Backend() {
        #ifdef _WIN32
            // Close job to stop only owned PHP process and descendants
            if (this->job) CloseHandle(this->job);
        #else
            this->stopLinux();
            if (!this->runtimeDirectory.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(this->runtimeDirectory, ec);
            }
        #endif
    }

    #ifdef _WIN32
        // Used to probe local listener without logging each startup retry
        static bool isListening(const Endpoint& endpoint) {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* pAddresses = nullptr;
            if (getaddrinfo(endpoint.host.c_str(), std::to_string(endpoint.port).c_str(), &hints, &pAddresses) != 0)
                return false;
            bool ready = false;
            for (auto* pAddr = pAddresses; pAddr && !ready; pAddr = pAddr->ai_next) {
                SOCKET sock = socket(pAddr->ai_family, SOCK_STREAM, IPPROTO_TCP);
                if (sock == INVALID_SOCKET) continue;
                u_long nonblocking = 1;
                if (ioctlsocket(sock, FIONBIO, &nonblocking) == 0) {
                    if (::connect(sock, pAddr->ai_addr, static_cast<int>(pAddr->ai_addrlen)) == 0) {
                        ready = true;
                    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
                        fd_set writable;
                        FD_ZERO(&writable);
                        FD_SET(sock, &writable);
                        timeval timeout{0, 100000};
                        if (select(0, nullptr, &writable, nullptr, &timeout) > 0) {
                            int error = 0;
                            int size = sizeof(error);
                            ready = getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) == 0 && error == 0;
                        }
                    }
                }
                closesocket(sock);
            }
            freeaddrinfo(pAddresses);
            return ready;
        }
    #endif

    #ifndef _WIN32
        /******************** PRIVATE LINUX PHP-FPM POOL ********************/

        // Used to locate newest executable PHP-FPM in PATH and standard install directories
        static std::string findFPM() {
            std::vector<std::string> directories{"/usr/sbin", "/usr/bin", "/usr/local/sbin", "/usr/local/bin"};
            if (const char* pPath = std::getenv("PATH")) {
                std::string path(pPath);
                size_t start = 0;
                do {
                    const size_t end = path.find(':', start);
                    directories.push_back(path.substr(start, end - start));
                    if (end == std::string::npos) break;
                    start = end + 1;
                } while (start <= path.size());
            }

            const std::regex pattern(R"(^php-fpm([0-9]+(\.[0-9]+)*)?$)");
            std::vector<int> bestVersion;
            std::string executable;
            for (const auto& directory : directories) {
                std::error_code ec;
                auto itr = std::filesystem::directory_iterator(directory.empty() ? "." : directory, ec);
                const auto end = std::filesystem::directory_iterator();
                for (; !ec && itr != end; itr.increment(ec)) {
                    const std::string name = itr->path().filename().string();
                    if (!std::regex_match(name, pattern) || !itr->is_regular_file(ec)) continue;
                    if (access(itr->path().c_str(), X_OK) != 0) continue;

                    // Compare numeric version components rather than filename ordering
                    std::vector<int> version{0};
                    if (name.size() > 7) {
                        version.clear();
                        const std::string suffix = name.substr(7);
                        size_t start = 0;
                        try {
                            do {
                                const size_t dot = suffix.find('.', start);
                                version.push_back(std::stoi(suffix.substr(start, dot - start)));
                                if (dot == std::string::npos) break;
                                start = dot + 1;
                            } while (start < suffix.size());
                        } catch (...) {
                            continue;
                        }
                    }
                    if (executable.empty() || version > bestVersion) {
                        auto path = std::filesystem::absolute(itr->path(), ec);
                        if (ec) break;
                        executable = path.string();
                        bestVersion = version;
                    }
                }
            }
            return executable;
        }

        // Used to stop and reap only owned FPM process group
        void Backend::stopLinux() {
            if (this->process <= 0) return;
            const pid_t pid = this->process;
            // Signal group even if master has already exited but remains unreaped
            kill(-pid, SIGTERM);
            int status = 0;
            pid_t result;
            do { result = waitpid(pid, &status, WNOHANG); } while (result < 0 && errno == EINTR);
            if (result == 0) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                do {
                    result = waitpid(pid, &status, WNOHANG);
                    if (result == pid || (result < 0 && errno != EINTR)) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                } while (std::chrono::steady_clock::now() < deadline);

                // Bound shutdown if PHP ignores termination or hangs
                if (result == 0 || (result < 0 && errno == EINTR)) {
                    kill(-pid, SIGKILL);
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
                }
            }
            this->process = -1;
        }

        void Backend::startLinux() {
            if (!this->runtimeDirectory.empty()) return;

            // Reserve private runtime directory before selecting endpoint
            char directory[] = "/tmp/mercury-fpm-XXXXXX";
            if (!mkdtemp(directory)) {
                // Pin failed endpoint so requests cannot fall back to system discovery
                conf::FASTCGI_ADDRESS = "unix:/dev/null";
                std::cerr << "FastCGI: failed to create private PHP-FPM directory: " << std::strerror(errno) << '\n';
                return;
            }
            this->runtimeDirectory = directory;
            const std::string socketPath = this->runtimeDirectory + "/php.sock";
            const std::string configPath = this->runtimeDirectory + "/php-fpm.conf";
            const std::string logPath = this->runtimeDirectory + "/php-fpm.log";
            conf::FASTCGI_ADDRESS = "unix:" + socketPath;

            // Use invoking account after Mercury drops sudo/doas privileges
            // Retain FPM's prohibition on directly launching workers as root
            if (geteuid() == 0) {
                std::cerr << "FastCGI: automatic PHP-FPM requires non-root runtime user. "
                          << "Start Mercury from your development account (sudo/doas may bind ports), "
                          << "or configure external FastCGIAddress.\n";
                return;
            }
            const std::string executable = findFPM();
            if (executable.empty()) {
                std::cerr << "FastCGI: PHP-FPM executable not found. Install php-fpm or configure external FastCGIAddress.\n";
                return;
            }

            // Isolate pool from system service settings and preserve developer file access
            // Omit user/group so unprivileged FPM workers inherit Mercury's identity
            {
                std::ofstream config(configPath);
                config << "[global]\ndaemonize = no\nerror_log = " << logPath
                       << "\n[mercury]\nlisten = " << socketPath
                       << "\nlisten.mode = 0600\npm = ondemand\npm.max_children = 8"
                       << "\npm.process_idle_timeout = 10s\nclear_env = no"
                       << "\ncatch_workers_output = yes\nsecurity.limit_extensions = .php\n";
                config.close();
                if (!config) {
                    std::cerr << "FastCGI: failed to write private PHP-FPM configuration.\n";
                    return;
                }
            }

            // Spawn without running C++ code in forked copy of Mercury worker threads
            posix_spawn_file_actions_t actions;
            posix_spawnattr_t attributes;
            int error = posix_spawn_file_actions_init(&actions);
            if (error != 0) {
                std::cerr << "FastCGI: failed to initialize PHP process actions: " << std::strerror(error) << '\n';
                return;
            }
            error = posix_spawnattr_init(&attributes);
            if (error != 0) {
                posix_spawn_file_actions_destroy(&actions);
                std::cerr << "FastCGI: failed to initialize PHP process attributes: " << std::strerror(error) << '\n';
                return;
            }
            auto recordError = [&error](int result) { if (error == 0) error = result; };
            recordError(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
            recordError(posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600));
            recordError(posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600));

            // Close inherited HTTP listeners and log descriptors in child
            DIR* pDescriptors = opendir("/proc/self/fd");
            if (!pDescriptors) {
                recordError(errno);
            } else {
                const int directoryFD = dirfd(pDescriptors);
                while (dirent* pEntry = readdir(pDescriptors)) {
                    char* pEnd = nullptr;
                    const long descriptor = std::strtol(pEntry->d_name, &pEnd, 10);
                    if (*pEnd == '\0' && descriptor > STDERR_FILENO && descriptor != directoryFD)
                        recordError(posix_spawn_file_actions_addclose(&actions, static_cast<int>(descriptor)));
                }
                closedir(pDescriptors);
            }

            // Give owned master its own process group and normal signal disposition
            sigset_t emptyMask, defaultSignals;
            sigemptyset(&emptyMask);
            sigemptyset(&defaultSignals);
            sigaddset(&defaultSignals, SIGTERM);
            sigaddset(&defaultSignals, SIGINT);
            sigaddset(&defaultSignals, SIGPIPE);
            recordError(posix_spawnattr_setpgroup(&attributes, 0));
            recordError(posix_spawnattr_setsigmask(&attributes, &emptyMask));
            recordError(posix_spawnattr_setsigdefault(&attributes, &defaultSignals));
            recordError(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF));
            char* argv[] = {const_cast<char*>(executable.c_str()), const_cast<char*>("-F"),
                            const_cast<char*>("-y"), const_cast<char*>(configPath.c_str()), nullptr};
            if (error == 0)
                error = posix_spawn(&this->process, executable.c_str(), &actions, &attributes, argv, environ);
            posix_spawnattr_destroy(&attributes);
            posix_spawn_file_actions_destroy(&actions);
            if (error != 0) {
                this->process = -1;
                std::cerr << "FastCGI: failed to launch PHP-FPM: " << std::strerror(error) << '\n';
                return;
            }

            // Wait for private socket before Mercury begins accepting HTTP requests
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
                // Keep exited master unreaped until cleanup signals its process group
                siginfo_t info{};
                const int result = waitid(P_PID, this->process, &info, WEXITED | WNOHANG | WNOWAIT);
                if (result == 0 && info.si_pid == this->process) break;
                if (result < 0 && errno != EINTR) { this->process = -1; break; }
                const int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
                if (sock >= 0) {
                    sockaddr_un address{};
                    address.sun_family = AF_UNIX;
                    std::strncpy(address.sun_path, socketPath.c_str(), sizeof(address.sun_path) - 1);
                    const bool ready = ::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
                    close(sock);
                    if (ready) {
                        ACCESS_LOG << "FastCGI: started private PHP-FPM at " << conf::FASTCGI_ADDRESS << std::endl;
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            this->stopLinux();
            std::cerr << "FastCGI: private PHP-FPM failed to become ready; PHP requests return 502.\n";
            std::ifstream log(logPath);
            std::string line;
            size_t remaining = 4096;
            while (remaining && std::getline(log, line)) {
                std::cerr << line.substr(0, remaining) << '\n';
                remaining -= std::min(remaining, line.size() + 1);
            }
        }
    #endif

    void Backend::start() {
        if (!conf::IS_FASTCGI_ENABLED) return;
        #ifndef _WIN32
            // Own default Linux pool instead of discovering unrelated system services
            if (conf::FASTCGI_ADDRESS.empty() || conf::FASTCGI_ADDRESS == "auto") {
                this->startLinux();
                return;
            }
        #endif
        Endpoint endpoint;
        if (!Endpoint::parse(conf::FASTCGI_ADDRESS, endpoint)) return;

        #ifdef _WIN32
            // Manage only local IPv4 endpoints supported by php-cgi -b
            if (endpoint.type != TransportType::TCP ||
                (endpoint.host != "127.0.0.1" && endpoint.host != "localhost")) {
                std::cerr << "FastCGI: start external PHP backend at " << conf::FASTCGI_ADDRESS
                          << " before serving PHP requests. Automatic startup supports localhost and 127.0.0.1 only.\n";
                return;
            }
            if (isListening(endpoint)) return;

            const std::string address = "127.0.0.1:" + std::to_string(endpoint.port);
            const std::string executable = conf::PHP_CGI_EXE_PATH.string();
            if (executable.empty() || executable.find('"') != std::string::npos) {
                std::cerr << "FastCGI: invalid WinPHPCGIPath; configure path to php-cgi.exe.\n";
                return;
            }

            HANDLE job = CreateJobObjectA(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

            if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                if (job) CloseHandle(job);
                std::cerr << "FastCGI: failed to create PHP process job. Start php-cgi.exe -b " << address << " manually.\n";
                return;
            }

            // Keep managed listener alive past PHP's default request recycle limit
            char* pEnvironment = GetEnvironmentStringsA();
            if (!pEnvironment) {
                CloseHandle(job);
                std::cerr << "FastCGI: failed to read process environment; start PHP manually.\n";
                return;
            }

            std::vector<char> environment;
            constexpr char maxRequests[] = "PHP_FCGI_MAX_REQUESTS=";
            for (const char* pEntry = pEnvironment; *pEntry; pEntry += std::strlen(pEntry) + 1) {
                if (_strnicmp(pEntry, maxRequests, sizeof(maxRequests) - 1) == 0) continue;
                environment.insert(environment.end(), pEntry, pEntry + std::strlen(pEntry) + 1);
            }

            FreeEnvironmentStringsA(pEnvironment);
            constexpr char unlimited[] = "PHP_FCGI_MAX_REQUESTS=0";
            environment.insert(environment.end(), unlimited, unlimited + sizeof(unlimited));
            environment.push_back('\0');

            std::string command = "\"" + executable + "\" -b " + address;
            STARTUPINFOA startup{};
            startup.cb = sizeof(startup);

            PROCESS_INFORMATION process{};
            if (!CreateProcessA(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                                CREATE_SUSPENDED | CREATE_NO_WINDOW, environment.data(), nullptr, &startup, &process)) {
                const DWORD error = GetLastError();
                CloseHandle(job);
                std::cerr << "FastCGI: failed to launch WinPHPCGIPath (Windows error " << error
                          << "). Run conf/setup_php.ps1 or start PHP manually.\n";
                return;
            }

            if (!AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
                TerminateProcess(process.hProcess, 1);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                CloseHandle(job);
                std::cerr << "FastCGI: failed to manage PHP process; start PHP manually.\n";
                return;
            }

            this->job = job;
            CloseHandle(process.hThread);

            bool ready = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
                if (WaitForSingleObject(process.hProcess, 0) != WAIT_TIMEOUT) break;
                if (isListening(endpoint)) { ready = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            CloseHandle(process.hProcess);
            if (!ready) {
                CloseHandle(this->job);
                this->job = nullptr;
                std::cerr << "FastCGI: PHP listener did not become ready at " << address
                          << ". Check php.ini or start php-cgi.exe -b " << address << " manually.\n";
            }
        #else
            // Keep explicitly configured Linux backends externally managed
            // Avoid probing remote hosts during startup
            if (endpoint.type == TransportType::TCP && endpoint.host != "127.0.0.1" &&
                endpoint.host != "localhost" && endpoint.host != "::1") return;
            Client probe(1);
            if (!probe.connect(endpoint)) {
                std::cerr << "FastCGI: explicitly configured backend is unavailable at " << conf::FASTCGI_ADDRESS
                          << ". Check external PHP-FPM service, listen address, and socket permissions; "
                          << "omit FastCGIAddress or use auto for Mercury-managed PHP-FPM.\n";
            }
        #endif
    }

}
