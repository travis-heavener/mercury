#ifndef __CONF_REDIRECT_HPP
#define __CONF_REDIRECT_HPP

#include <memory>
#include <regex>

#include <pugixml.hpp>

#include "../pch/common.hpp"

namespace conf {

    class Redirect {
        public:
            Redirect(const std::string& pattern, const std::string& to, const unsigned int status);
            inline const std::regex& getPattern() const { return pattern; }
            inline unsigned int getStatus() const { return status; }

            // Loads the redirected path to outPath if present, otherwise sets the path to the empty string
            void loadRedirectedPath(const std::string& currentPath, std::string& outPath) const;
        private:
            std::regex pattern;
            std::string to;
            unsigned int status;
    };

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

    std::unique_ptr<Redirect> loadRedirect(pugi::xml_node&);
    std::unique_ptr<Rewrite> loadRewrite(pugi::xml_node&);

}

#endif