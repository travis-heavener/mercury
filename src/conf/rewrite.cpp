#include "rewrite.hpp"

#include <iostream>

#include "../util/string_tools.hpp"

namespace conf {

    Rewrite::Rewrite(const std::string& pattern, const std::string& to) {
        this->pattern = std::regex(pattern);
        this->to = to;
    }

    bool Rewrite::loadRewrittenPath(std::string& path) const {
        // Check if the regex matches
        std::smatch matches;
        if (!std::regex_match(path, matches, pattern)) return false;

        // Replace matches in the "to" path
        std::string buf( to );

        // Count down in case there are $11 and $1
        for (size_t i = matches.size(); i > 0; --i)
            stringReplaceAll(buf, "$" + std::to_string(i-1), matches[i-1].str());
        path = buf;
        return true;
    }

    std::unique_ptr<Rewrite> loadRewrite(pugi::xml_node& root) {
        // Extract regex
        pugi::xml_attribute patternAttr = root.attribute("pattern");
        if (!patternAttr) {
            std::cerr << "Failed to parse config file, Rewrite node missing \"pattern\" attribute." << std::endl;
            return nullptr;
        }

        pugi::xml_attribute toAttr = root.attribute("to");
        if (!toAttr) {
            std::cerr << "Failed to parse config file, Rewrite node missing \"to\" attribute." << std::endl;
            return nullptr;
        }

        // Create new Match
        std::unique_ptr<Rewrite> pRewrite;
        try {
            pRewrite = std::make_unique<Rewrite>( patternAttr.as_string(), toAttr.as_string() );
        } catch (std::regex_error&) {
            std::cerr << "Failed to parse config file, bad Rewrite pattern." << std::endl;
            return nullptr;
        }

        // Base case, return new ptr
        return pRewrite;
    }

}