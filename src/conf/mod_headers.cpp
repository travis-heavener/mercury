#include "mod_headers.hpp"

#include <iostream>

#include "../util/string_tools.hpp"

namespace conf {

    IModHeader::IModHeader(const std::string& name) : name(name) {
        strToUpper(this->name);
    }

    bool ModIfHeaderMatch::doesRequestFitCondition(const http::headers_map_t& headers) const {
        if (!headers.contains(name)) return false;
        return std::regex_match(headers.at(name), pattern);
    }

    bool ModIfNotHeaderMatch::doesRequestFitCondition(const http::headers_map_t& headers) const {
        if (!headers.contains(name)) return false;
        return !std::regex_match(headers.at(name), pattern);
    }

    std::unique_ptr<IModHeader> loadModIfHeader(pugi::xml_node& node) {
        const std::string nodeName = node.name();

        // Extract regex
        pugi::xml_attribute nameAttr = node.attribute("name");
        if (!nameAttr) {
            std::cerr << "Failed to parse config file, " << nodeName << " node missing \"name\" attribute." << std::endl;
            return nullptr;
        }

        // Check if the node has a pattern
        if (nodeName == "FilterIfHeaderMatch" || nodeName == "FilterIfNotHeaderMatch") {
            pugi::xml_attribute patternAttr = node.attribute("pattern");
            if (!patternAttr) {
                std::cerr << "Failed to parse config file, " << nodeName << " node missing \"pattern\" attribute." << std::endl;
                return nullptr;
            }

            // Create new IModHeader
            std::unique_ptr<IModHeader> pModHeader;
            try {
                if (nodeName == "FilterIfHeaderMatch")
                    pModHeader = std::make_unique<ModIfHeaderMatch>( nameAttr.as_string(), patternAttr.as_string() );
                else
                    pModHeader = std::make_unique<ModIfNotHeaderMatch>( nameAttr.as_string(), patternAttr.as_string() );
            } catch (std::regex_error&) {
                std::cerr << "Failed to parse config file, bad " << nodeName << " pattern." << std::endl;
                return nullptr;
            }

            // Base case, return new ptr
            return pModHeader;
        }

        // Base case, no pattern
        if (nodeName == "FilterIfHeaderExist")
            return std::make_unique<ModIfHeaderExist>( nameAttr.as_string() );
        return std::make_unique<ModIfNotHeaderExist>( nameAttr.as_string() );
    }

}