#ifndef __CONF_ACCESS_HPP
#define __CONF_ACCESS_HPP

#include <string>
#include <vector>

#include "../../lib/pugixml.hpp"

namespace conf {

    enum class IPFamily { IPv4, IPv6 };

    class SanitizedIP {
        public:
            SanitizedIP(const IPFamily ipf, const unsigned int prefixLength);

            const IPFamily family; // Either IPv4 or IPv6
            uint8_t bytes[16]; // Full IP bytes, only first 4 matter for IPv4
            const unsigned int prefixLength; // For CIDR notation
    };

    class Access {
        public:
            Access(const std::string& mode) : isDenyFirst(mode == "deny all") {};

            // Returns true if the IP is allowed access by the exceptions list
            bool isIPAccepted(SanitizedIP& ip) const;

            void insertIP(SanitizedIP ip);
        private:
            const bool isDenyFirst;
            std::vector<SanitizedIP> exceptions;
    };

    SanitizedIP parseSanitizedClientIP(const std::string& clientIP);
    SanitizedIP parseSanitizedIP(const pugi::xml_node& childNode);

}

#endif