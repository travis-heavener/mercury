#ifndef __CONF_MATCH_HPP
#define __CONF_MATCH_HPP

#include <regex>

#include "../pch/common.hpp"
#include "../../lib/pugixml.hpp"

namespace conf {

    class Match {
        public:
            Match(const std::string& pattern);

            void addHeader(const std::string& k, const std::string& v);
            const std::unordered_map<std::string, std::string>& getHeaders() const;

            const std::regex& getPattern() const;
            bool showDirectoryIndexes() const;
            void setShowDirectoryIndexes(const bool b);
        private:
            std::regex pattern;
            std::unordered_map<std::string, std::string> headers;
            bool _showDirectoryIndexes;
    };

    Match* loadMatch(pugi::xml_node&);

}

#endif