#ifndef __HTTP_FASTCGI_PROTOCOL_HPP
#define __HTTP_FASTCGI_PROTOCOL_HPP

#include <bit>
#include <cstddef>
#include <cstdint>

namespace http::fastcgi {

    /*******************************************************************/
    /********************* PROTOCOL CONSTANTS & ENUMS ******************/
    /*******************************************************************/

    // Protocol version
    constexpr uint8_t FCGI_VERSION_1 = 1;

    // Connection keep-alive flag
    constexpr uint8_t FCGI_KEEP_CONN = 1;

    // Maximum payload bytes per record
    constexpr size_t FCGI_MAX_CONTENT_LENGTH = 65535;

    // Recommended maximum payload bytes aligned to 8-byte boundary (8191 * 8)
    constexpr size_t FCGI_MAX_ALIGNED_CHUNK = 65528;

    // Default request identifier for non-multiplexed connections
    constexpr uint16_t FCGI_DEFAULT_REQUEST_ID = 1;

    // FastCGI record types
    enum FCGI_TYPE : uint8_t {
        FCGI_BEGIN_REQUEST     = 1,
        FCGI_ABORT_REQUEST     = 2,
        FCGI_END_REQUEST       = 3,
        FCGI_PARAMS            = 4,
        FCGI_STDIN             = 5,
        FCGI_STDOUT            = 6,
        FCGI_STDERR            = 7,
        FCGI_DATA              = 8,
        FCGI_GET_VALUES        = 9,
        FCGI_GET_VALUES_RESULT = 10,
        FCGI_UNKNOWN_TYPE      = 11
    };

    // FastCGI responder roles
    enum FCGI_ROLE : uint16_t {
        FCGI_RESPONDER  = 1,
        FCGI_AUTHORIZER = 2,
        FCGI_FILTER     = 3
    };

    // FastCGI protocol statuses
    enum FCGI_PROTOCOL_STATUS : uint8_t {
        FCGI_REQUEST_COMPLETE = 0,
        FCGI_CANT_MPX_CONN    = 1,
        FCGI_OVERLOADED       = 2,
        FCGI_UNKNOWN_ROLE     = 3
    };

    /*******************************************************************/
    /********************* BYTE ORDER SERIALIZATION ********************/
    /*******************************************************************/

    // Used to convert 16-bit int to network big-endian
    inline uint16_t toNetwork16(uint16_t val) {
        if constexpr (std::endian::native == std::endian::big) return val;
        return static_cast<uint16_t>( ((val & 0x00FFU) << 8) | ((val & 0xFF00U) >> 8) );
    }

    // Used to convert 16-bit int from network big-endian
    inline uint16_t fromNetwork16(uint16_t val) {
        return toNetwork16(val);
    }

    // Used to convert 32-bit int to network big-endian
    inline uint32_t toNetwork32(uint32_t val) {
        if constexpr (std::endian::native == std::endian::big) return val;
        return ((val & 0x000000FFU) << 24) |
               ((val & 0x0000FF00U) << 8)  |
               ((val & 0x00FF0000U) >> 8)  |
               ((val & 0xFF000000U) >> 24);
    }

    // Used to convert 32-bit int from network big-endian
    inline uint32_t fromNetwork32(uint32_t val) {
        return toNetwork32(val);
    }

    // Used to calculate 8-byte alignment padding length
    inline uint8_t calculatePadding(size_t contentLength) {
        return static_cast<uint8_t>( (8 - (contentLength % 8)) % 8 );
    }

    /*******************************************************************/
    /********************** WIRE FRAMING STRUCTURES ********************/
    /*******************************************************************/

    #pragma pack(push, 1)

    // FastCGI 8-byte wire record header
    struct FCGI_Header {
        uint8_t  version = FCGI_VERSION_1;
        uint8_t  type = 0;
        uint16_t requestId = 0;      // Network byte order
        uint16_t contentLength = 0;  // Network byte order
        uint8_t  paddingLength = 0;
        uint8_t  reserved = 0;
    };
    static_assert(sizeof(FCGI_Header) == 8, "FCGI_Header must be 8 bytes");

    // FastCGI 8-byte begin request body
    struct FCGI_BeginRequestBody {
        uint16_t role = 0;           // Network byte order
        uint8_t  flags = 0;
        uint8_t  reserved[5] = {0};
    };
    static_assert(sizeof(FCGI_BeginRequestBody) == 8, "FCGI_BeginRequestBody must be 8 bytes");

    // FastCGI 16-byte complete begin request record
    struct FCGI_BeginRequestRecord {
        FCGI_Header           header;
        FCGI_BeginRequestBody body;
    };
    static_assert(sizeof(FCGI_BeginRequestRecord) == 16, "FCGI_BeginRequestRecord must be 16 bytes");

    // FastCGI 8-byte end request body
    struct FCGI_EndRequestBody {
        uint32_t appStatus = 0;      // Network byte order
        uint8_t  protocolStatus = 0;
        uint8_t  reserved[3] = {0};
    };
    static_assert(sizeof(FCGI_EndRequestBody) == 8, "FCGI_EndRequestBody must be 8 bytes");

    // FastCGI 16-byte complete end request record
    struct FCGI_EndRequestRecord {
        FCGI_Header         header;
        FCGI_EndRequestBody body;
    };
    static_assert(sizeof(FCGI_EndRequestRecord) == 16, "FCGI_EndRequestRecord must be 16 bytes");

    // FastCGI 8-byte unknown type body
    struct FCGI_UnknownTypeBody {
        uint8_t type = 0;
        uint8_t reserved[7] = {0};
    };
    static_assert(sizeof(FCGI_UnknownTypeBody) == 8, "FCGI_UnknownTypeBody must be 8 bytes");

    // FastCGI 16-byte complete unknown type record
    struct FCGI_UnknownTypeRecord {
        FCGI_Header          header;
        FCGI_UnknownTypeBody body;
    };
    static_assert(sizeof(FCGI_UnknownTypeRecord) == 16, "FCGI_UnknownTypeRecord must be 16 bytes");

    #pragma pack(pop)

    /*******************************************************************/
    /********************* RECORD BUILDERS & PARSERS *******************/
    /*******************************************************************/

    // Used to build FCGI_Header in network byte order
    inline FCGI_Header makeHeader(uint8_t type, uint16_t requestId, uint16_t contentLength, uint8_t paddingLength = 0) {
        FCGI_Header header;
        header.version = FCGI_VERSION_1;
        header.type = type;
        header.requestId = toNetwork16(requestId);
        header.contentLength = toNetwork16(contentLength);
        header.paddingLength = paddingLength;
        header.reserved = 0;
        return header;
    }

    // Used to decode requestId from FCGI_Header
    inline uint16_t getRequestId(const FCGI_Header& header) {
        return fromNetwork16(header.requestId);
    }

    // Used to decode contentLength from FCGI_Header
    inline uint16_t getContentLength(const FCGI_Header& header) {
        return fromNetwork16(header.contentLength);
    }

    // Used to build complete FCGI_BeginRequestRecord
    inline FCGI_BeginRequestRecord makeBeginRequestRecord(uint16_t requestId, uint16_t role = FCGI_RESPONDER, uint8_t flags = 0) {
        FCGI_BeginRequestRecord record;
        record.header = makeHeader(FCGI_BEGIN_REQUEST, requestId, sizeof(FCGI_BeginRequestBody), 0);
        record.body.role = toNetwork16(role);
        record.body.flags = flags;
        for (size_t i = 0; i < sizeof(record.body.reserved); ++i) record.body.reserved[i] = 0;
        return record;
    }

    // Used to decode role from FCGI_BeginRequestBody
    inline uint16_t getRole(const FCGI_BeginRequestBody& body) {
        return fromNetwork16(body.role);
    }

    // Used to build complete FCGI_EndRequestRecord
    inline FCGI_EndRequestRecord makeEndRequestRecord(uint16_t requestId, uint32_t appStatus, uint8_t protocolStatus = FCGI_REQUEST_COMPLETE) {
        FCGI_EndRequestRecord record;
        record.header = makeHeader(FCGI_END_REQUEST, requestId, sizeof(FCGI_EndRequestBody), 0);
        record.body.appStatus = toNetwork32(appStatus);
        record.body.protocolStatus = protocolStatus;
        for (size_t i = 0; i < sizeof(record.body.reserved); ++i) record.body.reserved[i] = 0;
        return record;
    }

    // Used to decode appStatus from FCGI_EndRequestBody
    inline uint32_t getAppStatus(const FCGI_EndRequestBody& body) {
        return fromNetwork32(body.appStatus);
    }

}

#endif

