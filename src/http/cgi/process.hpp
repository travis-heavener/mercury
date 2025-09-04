#ifndef __HTTP_CGI_PROCESS_HPP
#define __HTTP_CGI_PROCESS_HPP

#include <condition_variable>
#include <map>
#include <mutex>

#include "../../pch/common.hpp"

#ifdef _WIN32
    #include "../../winheader.hpp"
#else
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include "../request.hpp"
#include "../response.hpp"
#include "../../logs/logger.hpp"

// Platform specifics

namespace http::cgi {

    #ifdef _WIN32
        typedef std::vector<wchar_t> env_block_t;
    #else
        typedef std::vector<std::string> env_block_t;
    #endif

    // Platform independent, one-off CGI processor
    void handlePHPRequest(const File& file, const Request& req, Response& res);

    // Platform independent worker
    class Process {
        public:
            Process(env_block_t& envBlock);
            ~Process();

            void send(const std::string& input, std::string& outBuffer);
            bool hasSucceeded() const { return isSuccess; };
        private:
            bool createPipes(); // Creates the pipes for the worker
            bool spawnCGIProcess(env_block_t&); // Spawns a new CGI process

            // Cleanup methods
            void closePipes();
            void closeProcess();

            #ifdef _WIN32
                // Worker pipes
                HANDLE stdinRead = NULL, stdinWrite = NULL;
                HANDLE stdoutRead = NULL, stdoutWrite = NULL;

                PROCESS_INFORMATION procInfo;
            #else
                // Linux piping fields
                int stdinRead = -1, stdinWrite = -1;
                int stdoutRead = -1, stdoutWrite = -1;

                pid_t childPid;
            #endif

            // Whether or not the Worker succeeded to start
            bool isSuccess = false;
    };

}

#endif