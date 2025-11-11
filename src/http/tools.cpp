#include "tools.hpp"

#include <algorithm>
#include <string>

#include "../logs/logger.hpp"
#include "../util/string_tools.hpp"

namespace http {

    // Interval merge helper for byte ranges
    void intervalMergeByteRanges(const std::vector<byte_range_t>& ranges, std::vector<byte_range_t>& sortedRanges, const size_t streamSize) {
        // Normalize ranges
        std::vector<byte_range_t> normalizedRanges;
        for (auto [first, second] : ranges) {
            if (first == std::string::npos) {
                // Ex. bytes=-500
                size_t length = (std::min)(second, streamSize);
                first = streamSize - length;
                second = streamSize - 1;
            } else if (second == std::string::npos || second >= streamSize) {
                // Ex. bytes=0- OR bytes=200-300
                second = streamSize - 1;
            }

            normalizedRanges.emplace_back(first, second);
        }

        // Sort by start offset
        std::sort(normalizedRanges.begin(), normalizedRanges.end(),
            [](const byte_range_t& A, const byte_range_t& B) { return A.first < B.first; }
        );

        // Merge overlapping/adjacent ranges
        for (const auto& range : normalizedRanges) {
            if (sortedRanges.empty()) {
                sortedRanges.push_back(range);
            } else {
                auto& last = sortedRanges.back();
                if (range.first <= last.second + 1) // Touching, merge ranges
                    last.second = (std::max)(last.second, range.second);
                else // Not touching, new range
                    sortedRanges.push_back(range);
            }
        }
    }

    // Used to extract headers and status info from partial request
    void loadEarlyHeaders(headers_map_t& headers, const std::string& raw) {
        // Skip status line
        std::string line;
        size_t startIndex = 0;
        readLine(raw, line, startIndex);

        // Read headers from buffer
        while (readLine(raw, line, startIndex)) {
            if (line.size() <= 1) break; // Parse body

            // Check for invalid line format
            if (line.back() != '\r')
                throw http::Exception();
            line.pop_back();

            // Find first colon-space delimiter
            size_t firstSpaceIndex = line.find(": ");
            if (firstSpaceIndex != std::string::npos) {
                std::string key = line.substr(0, firstSpaceIndex);
                std::string value = line.substr(firstSpaceIndex+2);
                strToUpper(key);

                // Combine extra list headers
                if ((key == "ACCEPT" && headers.contains("ACCEPT")) ||
                    (key == "ACCEPT-ENCODING" && headers.contains("ACCEPT-ENCODING"))) {
                    headers[key].append(',' + value);
                } else if (key == "RANGE" && headers.contains("RANGE")) {
                    size_t bytesEnd = value.find('=');
                    if (bytesEnd != std::string::npos && bytesEnd + 1 < value.length())
                        headers[key].append(',' + value.substr(bytesEnd+1) );
                } else {
                    headers.insert({key, value});
                }
            }
        }
    }

    void parseAcceptHeader(std::unordered_set<std::string>& splitVec, std::string& string) {
        std::vector<std::string> splitBuf;
        size_t startIndex = 0;
        for (size_t i = 0; i < string.size(); i++) {
            if (string[i] == ',') {
                std::string substr = string.substr(startIndex, i-startIndex);
                trimString(substr);
                if (substr.size() > 0) splitBuf.push_back(substr);
                startIndex = i+1;
            }
        }

        // Append last snippet
        if (startIndex < string.size()) {
            std::string substr = string.substr(startIndex);
            trimString(substr);
            if (substr.size() > 0) splitBuf.push_back(substr);
        }

        // Format split buffer
        for (std::string& mime : splitBuf)
            splitVec.insert(mime.substr(0, mime.find(';')));
    }

    void parseRangeHeader(std::vector<byte_range_t>& splitVec, std::string& rawHeader) {
        trimString(rawHeader);
        size_t unitStart = rawHeader.find("bytes");
        if (unitStart == std::string::npos) return;

        // Break apart ranges into string pairs
        std::vector<std::string> intermediateVec;
        std::string rawRanges( rawHeader.substr(unitStart + 6) );
        splitString(intermediateVec, rawRanges, ',', true);

        // Parse each range
        std::string startBuf, endBuf;
        for (const std::string& rangePair : intermediateVec) {
            size_t dashIndex = rangePair.find('-');
            if (dashIndex == std::string::npos) {
                splitVec.clear(); return;
            }

            startBuf = rangePair.substr(0, dashIndex);
            endBuf = rangePair.substr(dashIndex+1);
            trimString(startBuf); trimString(endBuf);

            // Parse and create buffer
            if (startBuf.empty() && endBuf.empty()) {
                splitVec.clear(); return;
            }

            size_t startIndex = std::string::npos;
            size_t endIndex = std::string::npos;
            try {
                if (!startBuf.empty()) startIndex = std::stoull(startBuf);
                if (!endBuf.empty())   endIndex =   std::stoull(endBuf);
            } catch (std::invalid_argument&) {
                // Handle invalid range
                ERROR_LOG << "Parse failure for Range header" << std::endl;
                splitVec.clear();
                return;
            }

            // Emplace byte range
            splitVec.emplace_back( byte_range_t(startIndex, endIndex) );
        }
    }

    // Returns true if the path loading was successful, false otherwise
    bool loadRequestPaths(RequestPath& paths, const std::string& rawRequestPath, const bool preserveQueryString) {
        bool isSuccess = true;
        paths.rawPathFromRequest = rawRequestPath;

        if (rawRequestPath.size() == 0) return false;

        // Normalize the request path
        std::string normalizedPath = rawRequestPath;
        stringReplaceAll(normalizedPath, "\\", "/");
        stringReplaceAll(normalizedPath, "//", "/");

        // Split query string & URI
        size_t queryIndex = normalizedPath.find('?');

        // Split URI
        if (queryIndex == 0) { // Path is '?'
            isSuccess = false;
        } else {
            paths.rawURI = paths.decodedURI = normalizedPath.substr(0, queryIndex == std::string::npos ? normalizedPath.size() : queryIndex);
        }

        try {
            decodeURI(paths.decodedURI);
        } catch (std::invalid_argument&) {
            isSuccess = false;
        }

        // Break off query string (if not preserving it)
        if (!preserveQueryString && queryIndex != std::string::npos) {
            paths.rawQueryString = paths.decodedQueryString = normalizedPath.substr(queryIndex);
            try {
                decodeURI(paths.decodedQueryString);
            } catch (std::invalid_argument&) {
                isSuccess = false;
            }
        }

        // Return success status
        return isSuccess;
    }

}