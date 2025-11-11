#ifndef __CONF_MOD_HEADERS_HPP
#define __CONF_MOD_HEADERS_HPP

#include <cstdint>
#include <regex>
#include <string>
#include <vector>

#include <pugixml.hpp>

#include "../http/tools.hpp"

namespace conf {

    class IModHeader {
        public:
            IModHeader(const std::string& name);
            virtual ~IModHeader() = default;

            // Returns true if the request fits the condition for the wrapping Match block
            virtual bool doesRequestFitCondition(const http::headers_map_t&) const {
                throw std::runtime_error("Invoked base class IModHeader::doesRequestFitCondition stub.");
            };
        protected:
            std::string name;
    };

    class ModIfHeaderMatch : public virtual IModHeader {
        public:
            ModIfHeaderMatch(const std::string& name, const std::string& pattern) : IModHeader(name), pattern(pattern) {};
            bool doesRequestFitCondition(const http::headers_map_t& headers) const;
        private:
            std::regex pattern;
    };

    class ModIfNotHeaderMatch : public virtual IModHeader {
        public:
            ModIfNotHeaderMatch(const std::string& name, const std::string& pattern) : IModHeader(name), pattern(pattern) {};
            bool doesRequestFitCondition(const http::headers_map_t& headers) const;
        private:
            std::regex pattern;
    };

    class ModIfHeaderExist : public virtual IModHeader {
        public:
            ModIfHeaderExist(const std::string& name) : IModHeader(name) {};
            bool doesRequestFitCondition(const http::headers_map_t& headers) const { return headers.contains(name); }
    };

    class ModIfNotHeaderExist : public virtual IModHeader {
        public:
            ModIfNotHeaderExist(const std::string& name) : IModHeader(name) {};
            bool doesRequestFitCondition(const http::headers_map_t& headers) const { return !headers.contains(name); }
    };

    std::unique_ptr<IModHeader> loadModIfHeader(pugi::xml_node& node);

}

#endif