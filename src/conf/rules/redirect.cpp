#include "redirect.hpp"

#include <iostream>

#include "../../util/string_tools.hpp"

namespace conf {

#ifdef _WIN32
    Redirect::Redirect(const std::string& pattern, const std::string& to, const unsigned int status)
        : pattern(pattern, std::regex_constants::ECMAScript | std::regex_constants::icase), patternStr(pattern), to(to), status(status) {}
#else
    Redirect::Redirect(const std::string& pattern, const std::string& to, const unsigned int status)
        : pattern(pattern), patternStr(pattern), to(to), status(status) {}
#endif

    void Redirect::loadRedirectedPath(const std::string& currentPath, std::string& outPath) const {
        outPath.clear();

        // Check if the regex matches
        std::smatch matches;
        if (!std::regex_match(currentPath, matches, pattern)) return;

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
        if (!toAttr) {
            std::cerr << "Failed to parse config file, Redirect node missing \"to\" attribute." << std::endl;
            return nullptr;
        }

        unsigned int status;
        try {
            size_t idx = 0;
            std::string statusStr = root.text().as_string();
            trimString(statusStr);
            if (statusStr.empty() || statusStr[0] == '-') throw std::invalid_argument("");
            const unsigned long long val = std::stoull(statusStr, &idx);
            if (idx != statusStr.size() || val > 999) throw std::invalid_argument("");
            status = static_cast<unsigned int>(val);
            if (status < 300 || (status > 304 && status != 307 && status != 308))
                throw std::invalid_argument("");
        } catch (...) {
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