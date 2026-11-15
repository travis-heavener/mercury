#ifndef __HTTP_VERSION_CHECKER_HPP
#define __HTTP_VERSION_CHECKER_HPP

#include <cstdint>
#include <string>

// Used to fetch the latest version from remote
std::string fetchLatestVersion(const std::string& host = "mercury.wowtravis.com",
                               const std::string& path = "/latest",
                               uint16_t port = 443);

#endif
