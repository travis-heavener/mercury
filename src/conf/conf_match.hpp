#ifndef __CONF_MATCH_HPP
#define __CONF_MATCH_HPP

#include <memory>
#include <regex>

#include <pugixml.hpp>

#include "conf_access.hpp"
#include "../pch/common.hpp"

namespace conf {

    class Match {
        public:
            Match(const std::string& pattern);
            inline void addHeader(const std::string& k, const std::string& v) { headers.insert({k, v}); }
            inline const std::unordered_map<std::string, std::string>& getHeaders() const { return headers; }
            inline const std::regex& getPattern() const { return pattern; }
            inline bool showDirectoryIndexes() const { return _showDirectoryIndexes; }
            inline void setShowDirectoryIndexes(const bool b) { _showDirectoryIndexes = b; }
            inline void setAccessControl(std::unique_ptr<Access> pAccess) { this->pAccess = std::move(pAccess); }
            inline const std::unique_ptr<Access>& getAccessControl() const { return pAccess; };
        private:
            std::regex pattern;
            std::unordered_map<std::string, std::string> headers;
            bool _showDirectoryIndexes;
            std::unique_ptr<Access> pAccess;
    };

    std::unique_ptr<Match> loadMatch(pugi::xml_node&);

}

#endif