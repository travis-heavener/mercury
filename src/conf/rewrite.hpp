#ifndef __CONF_REWRITE_HPP
#define __CONF_REWRITE_HPP

#include <memory>
#include <regex>
#include <string>

#include <pugixml.hpp>

namespace conf {

    class Rewrite {
        public:
            Rewrite(const std::string& pattern, const std::string& to);
            inline const std::regex& getPattern() const { return pattern; }

            // Loads the rewritten path to outPath if present, otherwise sets the path to the empty string
            bool loadRewrittenPath(std::string& path) const;
        private:
            std::regex pattern;
            std::string to;
    };

    std::unique_ptr<Rewrite> loadRewrite(pugi::xml_node&);

}

#endif