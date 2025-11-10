#include "access.hpp"

#ifdef _WIN32
    #include "../winheader.hpp"
#else
    #include <arpa/inet.h>
#endif

#include <cstring>
#include <stdexcept>

#include "../util/string_tools.hpp"

namespace conf {

    SanitizedIP::SanitizedIP(const IPFamily ipf, const unsigned int prefixLength) :
        family(ipf), prefixLength(prefixLength) {
        std::memset(this->bytes, 0, sizeof(this->bytes));
    }

    bool doesAddressFitCIDR(const SanitizedIP& cidr, const SanitizedIP& candidate) {
        if (cidr.family != candidate.family) return false;
        const int length = cidr.family == IPFamily::IPv4 ? 4 : 16;

        // Match CIDR notation
        int bitsLeft = cidr.prefixLength;
        for (int i = 0; i < length && bitsLeft > 0; ++i) {
            // Mask bytes
            const uint8_t mask = (bitsLeft >= 8) ? 0xFF : static_cast<uint8_t>(0xFF << (8 - bitsLeft));

            // Compare against mask
            if ((cidr.bytes[i] & mask) != (candidate.bytes[i] & mask))
                return false;

            bitsLeft -= (bitsLeft >= 8) ? 8 : bitsLeft;
        }

        // Base case, success
        return true;
    }

    bool Access::isIPAccepted(SanitizedIP& candidate) const {
        // Check each exception
        for (const SanitizedIP& cidr : this->exceptions)
            if (doesAddressFitCIDR(cidr, candidate)) // Check address
                return this->isDenyFirst;

        // Base case, no exceptions met
        return !this->isDenyFirst;
    }

    SanitizedIP parseSanitizedClientIP(const std::string& clientIP) {
        // Try IPv4
        in_addr addr4;
        if (inet_pton(AF_INET, clientIP.c_str(), &addr4) == 1) {
            SanitizedIP sip( IPFamily::IPv4, 32 );
            std::memcpy(sip.bytes, &addr4, 4);
            return sip;
        }

        // Try IPv6
        in6_addr addr6;
        if (inet_pton(AF_INET6, clientIP.c_str(), &addr6) == 1) {
            SanitizedIP sip( IPFamily::IPv6, 128 );
            std::memcpy(sip.bytes, &addr6, 16);
            return sip;
        }

        // Base case, invalid IP address
        throw std::invalid_argument("Invalid IP address");
    }

    SanitizedIP parseSanitizedIP(const pugi::xml_node& childNode) {
        // Extract raw string
        std::string raw = childNode.text().as_string();
        trimString(raw);
        if (raw.size() == 0) throw std::invalid_argument("Empty IP address");

        int prefixLength = -1;

        // Extract prefix length, if present
        const size_t slashIndex = raw.find('/');
        if (slashIndex != std::string::npos) {
            prefixLength = static_cast<unsigned int>( std::stoul( raw.substr(slashIndex+1) ) );

            // Remove prefix length
            while (raw.size() > slashIndex)
                raw.pop_back();
        }

        // Extract address
        // Try IPv4
        in_addr addr4;
        if (inet_pton(AF_INET, raw.c_str(), &addr4) == 1) {
            if (prefixLength == -1) prefixLength = 32;
            if (prefixLength < 0 || prefixLength > 32)
                throw std::invalid_argument("Invalid IPv4 prefix length");

            SanitizedIP sip( IPFamily::IPv4, prefixLength );
            std::memcpy(sip.bytes, &addr4, 4);
            return sip;
        }

        // Try IPv6
        in6_addr addr6;
        if (inet_pton(AF_INET6, raw.c_str(), &addr6) == 1) {
            if (prefixLength == -1) prefixLength = 128;
            if (prefixLength < 0 || prefixLength > 128)
                throw std::invalid_argument("Invalid IPv6 prefix length");
            
            SanitizedIP sip( IPFamily::IPv6, prefixLength );
            std::memcpy(sip.bytes, &addr6, 16);
            return sip;
        }

        // Base case, invalid IP address
        throw std::invalid_argument("Invalid IP address");
    }

}