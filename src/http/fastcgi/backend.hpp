#ifndef __HTTP_FASTCGI_BACKEND_HPP
#define __HTTP_FASTCGI_BACKEND_HPP

#ifndef _WIN32
    #include <string>
    #include <sys/types.h>
#endif

namespace http::fastcgi {

    // Own only PHP process launched by this Mercury instance
    class Backend {
        public:
            Backend() = default;
            ~Backend();
            Backend(const Backend&) = delete;
            Backend& operator=(const Backend&) = delete;

            void start();

        private:
            #ifdef _WIN32
                void* job = nullptr;
            #else
                void startLinux();
                void stopLinux();

                pid_t process = -1;
                std::string runtimeDirectory;
            #endif
    };

}

#endif
