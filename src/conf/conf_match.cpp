#include "conf_match.hpp"

#include <iostream>

#include "../util/string_tools.hpp"

namespace conf {

    Match::Match(const std::string& pattern) {
        this->pattern = std::regex(pattern);
    }

    void Match::addHeader(const std::string& k, const std::string& v) {
        headers.insert({k, v});
    }

    const std::unordered_map<std::string, std::string>& Match::getHeaders() const {
        return headers;
    }

    const std::regex& Match::getPattern() const {
        return pattern;
    }

    bool Match::showDirectoryIndexes() const {
        return _showDirectoryIndexes;
    }

    void Match::setShowDirectoryIndexes(const bool b) {
        _showDirectoryIndexes = b;
    }


    Match* loadMatch(pugi::xml_node& root) {
        // Extract regex
        pugi::xml_attribute patternAttr = root.attribute("pattern");
        if (!patternAttr) {
            std::cerr << "Failed to parse config file, Match node missing pattern attribute.\n";
            return nullptr;
        }

        // Create new Match
        Match* pMatch;
        try {
            pMatch = new Match(patternAttr.as_string());
        } catch (std::regex_error&) {
            std::cerr << "Failed to parse config file, bad Match pattern.\n";
            return nullptr;
        }

        /***************************** Extract all Headers *****************************/

        pugi::xml_object_range headerNodes = root.children("Header");

        for (pugi::xml_node& headerNode : headerNodes) {
            // Parse header node
            pugi::xml_attribute nameAttr = headerNode.attribute("name");
            if (!nameAttr) {
                std::cerr << "Failed to parse config file, Header node missing name attribute.\n";
                delete pMatch;
                return nullptr;
            }

            const std::string name(nameAttr.as_string());
            std::string value(headerNode.text().as_string());
            trimString(value);

            // Append header
            pMatch->addHeader(name, value);
        }

        /***************************** Extract ShowDirectoryIndexes *****************************/
        pugi::xml_node showDirectoryIndexNode = root.child("ShowDirectoryIndexes");

        if (!showDirectoryIndexNode) {
            std::cerr << "Failed to parse config file, missing ShowDirectoryIndexes node in Match.\n";
            delete pMatch;
            return nullptr;
        }

        std::string showDirectoryIndexStr = showDirectoryIndexNode.text().as_string();
        trimString(showDirectoryIndexStr);

        // Verify valid value provided
        if (showDirectoryIndexStr != "on" && showDirectoryIndexStr != "off") {
            std::cerr << "Failed to parse config file, invalid value for ShowDirectoryIndexes node in Match.\n";
            delete pMatch;
            return nullptr;
        }

        pMatch->setShowDirectoryIndexes( showDirectoryIndexStr == "on" );

        // Return Match ptr
        return pMatch;
    }

}