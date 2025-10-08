#include "conf_match.hpp"

#include <iostream>

#include "../util/string_tools.hpp"

namespace conf {

    Match::Match(const std::string& pattern) {
        this->pattern = std::regex(pattern);
    }

    std::unique_ptr<Match> loadMatch(pugi::xml_node& root) {
        // Extract regex
        pugi::xml_attribute patternAttr = root.attribute("pattern");
        if (!patternAttr) {
            std::cerr << "Failed to parse config file, Match node missing \"pattern\" attribute." << std::endl;
            return nullptr;
        }

        // Create new Match
        std::unique_ptr<Match> pMatch;
        try {
            pMatch = std::make_unique<Match>( patternAttr.as_string() );
        } catch (std::regex_error&) {
            std::cerr << "Failed to parse config file, bad Match pattern." << std::endl;
            return nullptr;
        }

        /***************************** Extract all Headers *****************************/

        pugi::xml_object_range headerNodes = root.children("Header");

        for (pugi::xml_node& headerNode : headerNodes) {
            // Parse header node
            pugi::xml_attribute nameAttr = headerNode.attribute("name");
            if (!nameAttr) {
                std::cerr << "Failed to parse config file, Header node missing name attribute." << std::endl;
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

        if (showDirectoryIndexNode) {
            std::string showDirectoryIndexStr = showDirectoryIndexNode.text().as_string();
            trimString(showDirectoryIndexStr);

            // Verify valid value provided
            if (showDirectoryIndexStr != "on" && showDirectoryIndexStr != "off") {
                std::cerr << "Failed to parse config file, invalid value for ShowDirectoryIndexes node in Match." << std::endl;
                return nullptr;
            }

            pMatch->setShowDirectoryIndexes( showDirectoryIndexStr == "on" );
        } else {
            // Not present, use default value (true)
            pMatch->setShowDirectoryIndexes( true );
        }

        /***************************** Extract Access nodes *****************************/
        pugi::xml_node accessNode = root.child("Access");

        if (accessNode) {
            // Extract mode
            pugi::xml_attribute modeAttr = accessNode.attribute("mode");
            if (!modeAttr) {
                std::cerr << "Failed to parse config file, Access node missing name attribute." << std::endl;
                return nullptr;
            }

            const std::string modeStr( modeAttr.as_string() );
            if (modeStr != "deny all" && modeStr != "allow all") {
                std::cerr << "Failed to parse config file, Access node has invalid mode, must be either \"deny all\" or \"allow all\"." << std::endl;
                return nullptr;
            }

            std::unique_ptr<Access> pAccess = std::unique_ptr<Access>(new Access(modeStr));

            // Extract Allow/Deny nodes
            const std::string subnodeName( modeStr == "deny all" ? "Allow" : "Deny" ); // Either Allow or Deny
            pugi::xml_object_range nodeChildren = accessNode.children( subnodeName.c_str() );

            for (const pugi::xml_node& childNode : nodeChildren) {
                try {
                    pAccess->insertIP( parseSanitizedIP(childNode) );
                } catch (std::invalid_argument&) {
                    std::cerr << "Failed to parse config file, " << subnodeName << " node has an invalid IP address." << std::endl;
                    return nullptr;
                }
            }

            // Add pAccess to Match
            pMatch->setAccessControl(std::move(pAccess));
        } else {
            // Not present, set to a blank Access node that allows all
            pMatch->setAccessControl( std::unique_ptr<Access>(new Access("allow all")) );
        }

        // Return Match ptr
        return pMatch;
    }

}