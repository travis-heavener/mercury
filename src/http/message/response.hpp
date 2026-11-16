#ifndef __HTTP_RESPONSE_HPP
#define __HTTP_RESPONSE_HPP

#include <cstdint>
#include <functional>
#include <memory>

#ifdef _WIN32
    #include "../../winheader.hpp"
#endif

#include "../../io/file.hpp"
#include "../../util/string_tools.hpp"
#include "../../util/metrics.hpp"
#include "../streams/body_stream.hpp"
#include "../tools.hpp"

// The default status code for HTTP/0.9 response bodies
// If this changes, an error has occured
#define RESPONSE_DEFAULT_STATUS_HTTP_0_9 0

namespace http {

    class Response {
        public:
            Response(const std::string& httpVersion);

            inline void setStatus(const uint16_t statusCode) { this->statusCode = statusCode; };
            inline uint16_t getStatus() const { return httpVersion == "HTTP/0.9" ? 0 : statusCode; };
            inline void setMetricHandler(util::MetricHandler handler) { metricHandler = handler; };
            inline util::MetricHandler getMetricHandler() const { return metricHandler; };

            void setHeader(std::string name, const std::string& value);
            void addHeader(std::string name, const std::string& value);
            void clearHeader(std::string name);
            void addVary(std::string_view field);
            inline void clearHeaders() { headers.clear(); setCookies.clear(); hasCustomContentEncoding = false; identityAccepted = true; };
            inline void setIdentityAccepted(const bool accepted) { this->identityAccepted = accepted; };
            inline bool isIdentityAccepted() const { return identityAccepted; };
            inline bool hasCustomContentEncodingHeader() const { return hasCustomContentEncoding; };
            inline std::string getContentEncoding() const {
                auto it = headers.find("Content-Encoding");
                return it == headers.end() ? "" : it->second;
            }
            void setCompressMethod(const int compressMethod);

            int loadBodyFromErrorDoc(const uint16_t statusCode);
            int loadBodyFromFile(File& file);
            inline void setBodyStream(std::unique_ptr<IBodyStream> p) { pBodyStream = std::move(p); };

            inline void setContentType(const std::string& type) {
                this->setHeader("Content-Type", type);
            }
            const std::string getContentType() const;

            size_t getContentLength() const;
            bool shouldCloseConnection() const;

            ssize_t streamBody(const bool isHTMLAccepted, const bool omitBody, std::function<ssize_t(const char*, const size_t)>&);

            // Returns true if the ranges are valid, false otherwise
            bool extendByteRanges(const std::vector<byte_range_t>& byteRanges);
        private:
            bool precompressBody();
            void clearByteRanges();

            std::string httpVersion;
            util::MetricHandler metricHandler = util::MetricHandler::Static;
            uint16_t statusCode = 200;
            std::unique_ptr<IBodyStream> pBodyStream;
            int compressMethod = NO_COMPRESS;

            std::unordered_map<std::string, std::string> headers;
            std::vector<std::string> setCookies;
            bool hasCustomContentEncoding = false;
            bool identityAccepted = true;

            // For precompressed bodies
            std::vector<byte_range_t> originalByteRanges;
            size_t originalBodySize = 0;
            size_t totalByteRangeSize = 0; // The total size of all the byte range data
    };

};

#endif
