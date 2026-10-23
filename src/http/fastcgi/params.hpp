#ifndef __HTTP_FASTCGI_PARAMS_HPP
#define __HTTP_FASTCGI_PARAMS_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "protocol.hpp"

namespace http::fastcgi {

    /*******************************************************************/
    /********************* FASTCGI PARAMETER ENCODER *******************/
    /*******************************************************************/

    class ParamEncoder {
        public:
            ParamEncoder();
            explicit ParamEncoder(size_t initialCapacity);
            ~ParamEncoder() = default;

            // Add parameter using string views
            void add(std::string_view name, std::string_view value);

            // Clear internal buffer
            void clear();

            // Check if buffer is empty
            bool empty() const;

            // Total encoded parameter payload size
            size_t getPayloadSize() const;

            // Access raw encoded parameter buffer
            const std::vector<uint8_t>& getBuffer() const;

            // Serialize into FCGI_PARAMS records w/ headers, padding, and empty terminator
            std::vector<uint8_t> buildRecords(uint16_t requestId = FCGI_DEFAULT_REQUEST_ID,
                                              bool padTo8Bytes = true,
                                              size_t maxChunkSize = FCGI_MAX_ALIGNED_CHUNK) const;

            // Used to calculate encoded size for name-value pair
            static size_t calculateEncodedSize(std::string_view name, std::string_view value);

            // Used to append length prefix to destination byte vector
            static void appendLength(size_t len, std::vector<uint8_t>& dest);

            // Used to append name-value pair to destination byte vector
            static void appendParam(std::string_view name, std::string_view value, std::vector<uint8_t>& dest);

            // Used to decode single parameter from byte stream
            static bool decodeNext(const uint8_t* pData, size_t remaining, size_t& bytesRead,
                                  std::string_view& name, std::string_view& value);

            // Used to decode all parameters from continuous byte stream
            static bool decodeAll(const uint8_t* pData, size_t length,
                                  std::vector<std::pair<std::string_view, std::string_view>>& out);

        private:
            std::vector<uint8_t> buffer;
    };

}

#endif

