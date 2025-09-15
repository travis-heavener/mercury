#ifndef __CONF_MATCH_HPP
#define __CONF_MATCH_HPP

#include <memory>
#include <regex>

#include "conf_access.hpp"
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

            void setAccessControl(std::unique_ptr<Access> p);
            const std::unique_ptr<Access>& getAccessControl() const { return pAccess; };
        private:
            std::regex pattern;
            std::unordered_map<std::string, std::string> headers;
            bool _showDirectoryIndexes;
            std::unique_ptr<Access> pAccess;
    };

    std::unique_ptr<Match> loadMatch(pugi::xml_node&);

}

#endif