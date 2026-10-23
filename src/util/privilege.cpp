#include "privilege.hpp"

#include <iostream>

#ifndef _WIN32
    #include <cerrno>
    #include <grp.h>
    #include <pwd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

#include "../conf/conf.hpp"
#include "../logs/logger.hpp"

namespace util::privilege {

    // Used to query if process is running with elevated root privileges
    bool isElevated() {
        #ifdef _WIN32
            return false;
        #else
            return geteuid() == 0;
        #endif
    }

    // Used to detect invoking user when executed under sudo or doas
    std::optional<UserIdentity> getSudoUser() {
        #ifdef _WIN32
            return std::nullopt;
        #else
            if (geteuid() != 0) return std::nullopt;

            const char* pSudoUid = std::getenv("SUDO_UID");
            const char* pSudoGid = std::getenv("SUDO_GID");
            const char* pSudoUser = std::getenv("SUDO_USER");

            if (pSudoUid && pSudoGid) {
                try {
                    const uid_t uid = static_cast<uid_t>( std::stoul(pSudoUid) );
                    const gid_t gid = static_cast<gid_t>( std::stoul(pSudoGid) );
                    if (uid != 0) {
                        std::string name = pSudoUser ? pSudoUser : "";
                        if (name.empty()) {
                            struct passwd* pPw = getpwuid(uid);
                            if (pPw && pPw->pw_name) name = pPw->pw_name;
                        }
                        return UserIdentity{static_cast<unsigned int>(uid), static_cast<unsigned int>(gid), name};
                    }
                } catch (...) {}
            }

            const char* pDoasUser = std::getenv("DOAS_USER");
            const char* pTargetUser = pDoasUser ? pDoasUser : pSudoUser;
            if (pTargetUser) {
                struct passwd* pPw = getpwnam(pTargetUser);
                if (pPw && pPw->pw_uid != 0)
                    return UserIdentity{static_cast<unsigned int>(pPw->pw_uid), static_cast<unsigned int>(pPw->pw_gid), pPw->pw_name};
            }

            return std::nullopt;
        #endif
    }

    // Used to apply user ownership and permissions to file or directory
    void applyOwnershipAndPermissions(const std::filesystem::path& path, unsigned int mode) {
        #ifndef _WIN32
            const auto optUser = getSudoUser();
            if (!optUser) return;

            const std::string pathStr = path.string();
            if (pathStr.empty()) return;

            // Change ownership to invoking user
            if (chown(pathStr.c_str(), static_cast<uid_t>(optUser->uid), static_cast<gid_t>(optUser->gid)) < 0)
                ERROR_LOG << "Failed to change ownership of " << pathStr << ", errno: " << errno << std::endl;

            // Apply mode if specified
            if (mode != 0 && chmod(pathStr.c_str(), static_cast<mode_t>(mode)) < 0)
                ERROR_LOG << "Failed to set permissions of " << pathStr << ", errno: " << errno << std::endl;
        #else
            (void)path;
            (void)mode;
        #endif
    }

    // Used to permanently drop root privileges to invoking user after binding
    bool dropPrivileges() {
        #ifndef _WIN32
            if (geteuid() != 0) return true;

            const auto optUser = getSudoUser();
            if (!optUser) return true;

            const auto& user = *optUser;
            const uid_t targetUid = static_cast<uid_t>(user.uid);
            const gid_t targetGid = static_cast<gid_t>(user.gid);

            // Drop supplementary groups
            if (!user.name.empty()) {
                initgroups(user.name.c_str(), targetGid);
            } else {
                setgroups(1, &targetGid);
            }

            // Drop group ID
            if (setgid(targetGid) != 0) {
                std::cerr << "Failed to drop group privileges to GID " << targetGid << std::endl;
                return false;
            }

            // Drop user ID permanently
            if (setuid(targetUid) != 0) {
                std::cerr << "Failed to drop user privileges to UID " << targetUid << std::endl;
                return false;
            }

            // Verify privileges dropped permanently
            if (getuid() != targetUid || geteuid() != targetUid) {
                std::cerr << "Failed to permanently drop root privileges." << std::endl;
                return false;
            }

            // Verify root privileges cannot be restored
            if (seteuid(0) == 0) {
                std::cerr << "Failed to permanently drop root privileges: restored root euid." << std::endl;
                return false;
            }

            const std::string displayName = user.name.empty() ? std::to_string(user.uid) : user.name;
            ACCESS_LOG << "Dropped root privileges to user " << displayName
                << " (UID: " << user.uid << ", GID: " << user.gid << ")." << std::endl;
            if (conf::SHOW_PRIVILEGE_DEESCALATION_MESSAGE)
                std::cout << "> Dropped root privileges to user '" << displayName << "'." << std::endl;
        #endif
        return true;
    }

}

