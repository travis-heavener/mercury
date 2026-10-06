#include "tools.hpp"

#include <algorithm>
#include <charconv>
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

    bool parseUnsignedDecimal(const std::string& value, size_t& result) {
        if (value.empty()) return false;
        for (const char c : value)
            if (c < '0' || c > '9') return false;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size();
    }

    bool isValidHeaderName(const std::string& name) {
        if (name.empty()) return false;
        for (const unsigned char c : name) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
            if (std::string("!#$%&'*+-.^_`|~").find(c) == std::string::npos) return false;
        }
        return true;
    }

    bool isValidHeaderValue(const std::string& value) {
        for (const unsigned char c : value)
            if ((c < 32 && c != '\t') || c == 127) return false;
        return true;
    }

    // Parse headers strictly before deciding how many body bytes to consume.
    void loadEarlyHeaders(headers_map_t& headers, const std::string& raw) {
        std::string line;
        size_t startIndex = 0;
        if (!readLine(raw, line, startIndex) || line.empty() || line.back() != '\r')
            throw http::Exception();

        while (readLine(raw, line, startIndex)) {
            if (line == "\r") return;
            if (line.empty() || line.back() != '\r') throw http::Exception();
            line.pop_back();

            const size_t colon = line.find(':');
            if (colon == std::string::npos) throw http::Exception();
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            if (!isValidHeaderName(key) || !isValidHeaderValue(value)) throw http::Exception();
            trimString(value);
            strToUpper(key);

            if ((key == "CONTENT-LENGTH" || key == "HOST" || key == "TRANSFER-ENCODING") && headers.contains(key))
                throw http::Exception(); // Reject ambiguous framing and routing.

            if ((key == "ACCEPT" || key == "ACCEPT-ENCODING" || key == "CONNECTION") && headers.contains(key)) {
                headers[key].append(',' + value);
            } else if (key == "RANGE" && headers.contains(key)) {
                const size_t bytesEnd = value.find('=');
                if (bytesEnd != std::string::npos)
                    headers[key].append(',' + value.substr(bytesEnd + 1));
            } else {
                headers.insert({key, value});
            }
        }
        throw http::Exception(); // Missing header terminator.
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
        if (!rawHeader.starts_with("bytes=")) return;

        // Break apart ranges into string pairs
        std::vector<std::string> intermediateVec;
        std::string rawRanges( rawHeader.substr(6) );
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
            if ((!startBuf.empty() && (!parseUnsignedDecimal(startBuf, startIndex) || startIndex == std::string::npos)) ||
                (!endBuf.empty() && (!parseUnsignedDecimal(endBuf, endIndex) || endIndex == std::string::npos))) {
                splitVec.clear();
                return;
            }

            // Emplace byte range
            splitVec.emplace_back( byte_range_t(startIndex, endIndex) );
        }
    }

    // Normalize the path before access checks, but preserve query bytes for CGI/redirects.
    bool loadRequestPaths(RequestPath& paths, const std::string& rawRequestPath, const bool preserveQueryString) {
        if (rawRequestPath.empty()) return false;
        if (!preserveQueryString) paths.rawPathFromRequest = rawRequestPath;

        // Rewrite rules receive an already-decoded path. Do not decode it a second time.
        const size_t queryIndex = preserveQueryString ? std::string::npos : rawRequestPath.find('?');
        paths.rawURI = rawRequestPath.substr(0, queryIndex);
        std::string decoded = paths.rawURI;
        try {
            if (!preserveQueryString) decodeURI(decoded);
            if (!preserveQueryString) {
                paths.rawQueryString = queryIndex == std::string::npos ? "" : rawRequestPath.substr(queryIndex);
                paths.decodedQueryString = paths.rawQueryString;
                decodeURI(paths.decodedQueryString);
            }
        } catch (const std::invalid_argument&) {
            return false;
        }

        for (char& c : decoded) {
            if (static_cast<unsigned char>(c) < 32 || c == 127) return false;
            if (c == '\\') c = '/';
        }
        if (decoded == "*") {
            paths.decodedURI = decoded;
            return true;
        }
        if (decoded.empty() || decoded.front() != '/') return false;

        const bool trailingSlash = decoded.back() == '/' || decoded.ends_with("/.");
        std::vector<std::string> segments;
        splitString(segments, decoded, '/', false);
        std::string normalized;
        for (const std::string& segment : segments) {
            if (segment == "..") return false;
            if (segment != ".") normalized += '/' + segment;
        }
        if (normalized.empty()) normalized = "/";
        else if (trailingSlash) normalized += '/';
        paths.decodedURI = std::move(normalized);
        return true;
    }

}
