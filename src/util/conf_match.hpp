#ifndef __CONF_MATCH_HPP
#define __CONF_MATCH_HPP

#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>

#include "string_tools.hpp"

#include "../../lib/pugixml.hpp"

namespace conf {

    class Match {
        public:
            Match(const std::string& pattern) { this->pattern = std::regex(pattern); };

            void addHeader(const std::string& k, const std::string& v) { headers.insert({k, v}); };
            const std::unordered_map<std::string, std::string>& getHeaders() const { return headers; };

            const std::regex& getPattern() const { return pattern; };
        private:
            std::regex pattern;
            std::unordered_map<std::string, std::string> headers;
    };

    Match* loadMatch(pugi::xml_node&);

}

#endif