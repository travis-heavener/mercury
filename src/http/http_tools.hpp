#ifndef __HTTP_HTTP_TOOLS_HPP
#define __HTTP_HTTP_TOOLS_HPP

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace http {
    enum METHOD {
        GET = 0,
        HEAD = 1,
        OPTIONS = 2,
        POST = 3,
        PUT = 4,
        DEL = 5,
        PATCH = 6,
        UNKNOWN = 999
    };

    typedef int Exception;
    typedef std::pair<size_t, size_t> byte_range_t;

    // Interval merge helper for byte ranges
    void intervalMergeByteRanges(const std::vector<byte_range_t>& ranges, std::vector<byte_range_t>& sortedRanges, const size_t streamSize);

    // Used to extract headers and status info from partial request
    typedef std::unordered_map<std::string, std::string> headers_map_t;
    void loadEarlyHeaders(headers_map_t&, const std::string&);

    void parseAcceptHeader(std::unordered_set<std::string>&, std::string&);
    void parseRangeHeader(std::vector<byte_range_t>&, std::string&);

    // Used to pass in flags to the Request object (e.g. URI too long, content too large)
    typedef struct {
        bool isContentTooLarge = false;
        bool isURITooLong = false;
    } RequestFlags;
};

#endif