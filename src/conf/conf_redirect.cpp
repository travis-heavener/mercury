#include "conf_redirect.hpp"

#include <iostream>

#include "../util/string_tools.hpp"

namespace conf {

    Redirect::Redirect(const std::string& pattern, const std::string& to, const unsigned int status) {
        this->pattern = std::regex(pattern);
        this->to = to;
        this->status = status;
    }

    void Redirect::loadRedirectedPath(const std::string& currentPath, std::string& outPath) const {
        outPath.clear();

        // Check if the regex matches
        std::smatch matches;
        if (!std::regex_search(currentPath, matches, pattern)) return;

        // Replace matches in the "to" path
        outPath = to;

        // Count down in case there are $11 and $1
        for (size_t i = matches.size(); i > 0; --i)
            stringReplaceAll(outPath, "$" + std::to_string(i-1), matches[i-1].str());
    }

    std::unique_ptr<Redirect> loadRedirect(pugi::xml_node& root) {
        // Extract regex
        pugi::xml_attribute patternAttr = root.attribute("pattern");
        if (!patternAttr) {
            std::cerr << "Failed to parse config file, Redirect node missing \"pattern\" attribute." << std::endl;
            return nullptr;
        }

        pugi::xml_attribute toAttr = root.attribute("to");
        if (!patternAttr) {
            std::cerr << "Failed to parse config file, Redirect node missing \"to\" attribute." << std::endl;
            return nullptr;
        }

        unsigned int status;
        try {
            status = std::stoul( root.text().as_string() );
            if (status < 300 || (status > 304 && status != 307 && status != 308))
                throw std::invalid_argument("");
        } catch (std::invalid_argument&) {
            std::cerr << "Failed to parse config file, Redirect node has invalid HTTP status code." << std::endl;
            return nullptr;
        }

        // Create new Match
        std::unique_ptr<Redirect> pRedirect;
        try {
            pRedirect = std::make_unique<Redirect>( patternAttr.as_string(), toAttr.as_string(), status );
        } catch (std::regex_error&) {
            std::cerr << "Failed to parse config file, bad Redirect pattern." << std::endl;
            return nullptr;
        }

        // Base case, return new ptr
        return pRedirect;
    }

}