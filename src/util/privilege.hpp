#ifndef __PRIVILEGE_HPP
#define __PRIVILEGE_HPP

#include <filesystem>
#include <optional>
#include <string>

namespace util::privilege {

    struct UserIdentity {
        unsigned int uid = 0; // Target user ID
        unsigned int gid = 0; // Target group ID
        std::string name;     // Target user name
    };

    // Used to query if process is running with elevated root privileges
    bool isElevated();

    // Used to detect invoking user when executed under sudo or doas
    std::optional<UserIdentity> getSudoUser();

    // Used to apply user ownership and permissions to file or directory
    void applyOwnershipAndPermissions(const std::filesystem::path& path, unsigned int mode = 0);

    // Used to permanently drop root privileges to invoking user after binding
    bool dropPrivileges();

}

#endif

