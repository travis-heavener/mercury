#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../src/http/fastcgi/params.hpp"
#include "../src/http/fastcgi/protocol.hpp"

using namespace http::fastcgi;

// Test wire structure sizes and alignments
void testWireStructures() {
    assert(sizeof(FCGI_Header) == 8);
    assert(sizeof(FCGI_BeginRequestBody) == 8);
    assert(sizeof(FCGI_BeginRequestRecord) == 16);
    assert(sizeof(FCGI_EndRequestBody) == 8);
    assert(sizeof(FCGI_EndRequestRecord) == 16);
    assert(sizeof(FCGI_UnknownTypeBody) == 8);
    assert(sizeof(FCGI_UnknownTypeRecord) == 16);
    std::cout << "[PASS] Wire structure sizes" << std::endl;
}

// Test big-endian network byte order conversions
void testEndianConversions() {
    uint16_t val16 = 0x1234;
    uint16_t net16 = toNetwork16(val16);
    const uint8_t* pBytes16 = reinterpret_cast<const uint8_t*>(&net16);
    assert(pBytes16[0] == 0x12);
    assert(pBytes16[1] == 0x34);
    assert(fromNetwork16(net16) == val16);

    uint32_t val32 = 0x12345678;
    uint32_t net32 = toNetwork32(val32);
    const uint8_t* pBytes32 = reinterpret_cast<const uint8_t*>(&net32);
    assert(pBytes32[0] == 0x12);
    assert(pBytes32[1] == 0x34);
    assert(pBytes32[2] == 0x56);
    assert(pBytes32[3] == 0x78);
    assert(fromNetwork32(net32) == val32);

    std::cout << "[PASS] Big-endian network byte order conversions" << std::endl;
}

// Test 8-byte alignment padding calculation
void testPaddingCalculation() {
    assert(calculatePadding(0) == 0);
    assert(calculatePadding(1) == 7);
    assert(calculatePadding(2) == 6);
    assert(calculatePadding(3) == 5);
    assert(calculatePadding(4) == 4);
    assert(calculatePadding(5) == 3);
    assert(calculatePadding(6) == 2);
    assert(calculatePadding(7) == 1);
    assert(calculatePadding(8) == 0);
    assert(calculatePadding(100) == 4);
    assert(calculatePadding(65528) == 0);
    assert(calculatePadding(65535) == 1);

    for (size_t len = 0; len <= 1000; ++len) {
        uint8_t pad = calculatePadding(len);
        assert((len + pad) % 8 == 0);
    }

    std::cout << "[PASS] 8-byte alignment padding calculations" << std::endl;
}

// Test record header framing
void testRecordHeaderFraming() {
    FCGI_Header header = makeHeader(FCGI_BEGIN_REQUEST, 42, 8, 0);
    assert(header.version == FCGI_VERSION_1);
    assert(header.type == FCGI_BEGIN_REQUEST);
    assert(getRequestId(header) == 42);
    assert(getContentLength(header) == 8);
    assert(header.paddingLength == 0);
    assert(header.reserved == 0);

    // Verify wire bytes
    const uint8_t* pWire = reinterpret_cast<const uint8_t*>(&header);
    assert(pWire[0] == 1); // Version
    assert(pWire[1] == FCGI_BEGIN_REQUEST); // Type
    assert(pWire[2] == 0x00 && pWire[3] == 42); // RequestId big-endian
    assert(pWire[4] == 0x00 && pWire[5] == 8); // ContentLength big-endian
    assert(pWire[6] == 0); // PaddingLength
    assert(pWire[7] == 0); // Reserved

    std::cout << "[PASS] Record header framing and byte layout" << std::endl;
}

// Test begin and end request records
void testBeginEndRecords() {
    // Begin request record
    FCGI_BeginRequestRecord beginRec = makeBeginRequestRecord(1, FCGI_RESPONDER, FCGI_KEEP_CONN);
    assert(beginRec.header.version == FCGI_VERSION_1);
    assert(beginRec.header.type == FCGI_BEGIN_REQUEST);
    assert(getRequestId(beginRec.header) == 1);
    assert(getContentLength(beginRec.header) == 8);
    assert(getRole(beginRec.body) == FCGI_RESPONDER);
    assert(beginRec.body.flags == FCGI_KEEP_CONN);

    // End request record
    FCGI_EndRequestRecord endRec = makeEndRequestRecord(1, 0, FCGI_REQUEST_COMPLETE);
    assert(endRec.header.version == FCGI_VERSION_1);
    assert(endRec.header.type == FCGI_END_REQUEST);
    assert(getRequestId(endRec.header) == 1);
    assert(getContentLength(endRec.header) == 8);
    assert(getAppStatus(endRec.body) == 0);
    assert(endRec.body.protocolStatus == FCGI_REQUEST_COMPLETE);

    std::cout << "[PASS] Begin and end request records" << std::endl;
}

// Test short parameter encoding (< 128 bytes)
void testShortParamEncoding() {
    ParamEncoder encoder;
    encoder.add("SCRIPT_FILENAME", "/var/www/index.php");

    const std::vector<uint8_t>& buf = encoder.getBuffer();
    assert(buf.size() == 1 + 1 + 15 + 18);
    assert(buf[0] == 15); // Name length
    assert(buf[1] == 18); // Value length

    // Decode back
    size_t bytesRead = 0;
    std::string_view name;
    std::string_view val;
    bool ok = ParamEncoder::decodeNext(buf.data(), buf.size(), bytesRead, name, val);
    assert(ok);
    assert(bytesRead == buf.size());
    assert(name == "SCRIPT_FILENAME");
    assert(val == "/var/www/index.php");

    std::cout << "[PASS] Short parameter encoding and decoding" << std::endl;
}

// Test long parameter encoding (>= 128 bytes)
void testLongParamEncoding() {
    std::string longVal(300, 'Z');
    ParamEncoder encoder;
    encoder.add("LONG_PARAM", longVal);

    const std::vector<uint8_t>& buf = encoder.getBuffer();
    // Name is 10 bytes (< 128) -> 1 byte prefix
    // Value is 300 bytes (>= 128) -> 4 byte prefix (0x80, 0x00, 0x01, 0x2C)
    assert(buf.size() == 1 + 4 + 10 + 300);
    assert(buf[0] == 10);
    assert(buf[1] == 0x80);
    assert(buf[2] == 0x00);
    assert(buf[3] == 0x01);
    assert(buf[4] == 0x2C);

    // Decode back
    size_t bytesRead = 0;
    std::string_view name;
    std::string_view val;
    bool ok = ParamEncoder::decodeNext(buf.data(), buf.size(), bytesRead, name, val);
    assert(ok);
    assert(bytesRead == buf.size());
    assert(name == "LONG_PARAM");
    assert(val == longVal);

    std::cout << "[PASS] Long parameter encoding and decoding (>= 128 bytes)" << std::endl;
}

// Test empty parameter value encoding
void testEmptyValueParamEncoding() {
    ParamEncoder encoder;
    encoder.add("QUERY_STRING", "");

    const std::vector<uint8_t>& buf = encoder.getBuffer();
    assert(buf.size() == 1 + 1 + 12 + 0);
    assert(buf[0] == 12);
    assert(buf[1] == 0);

    size_t bytesRead = 0;
    std::string_view name;
    std::string_view val;
    bool ok = ParamEncoder::decodeNext(buf.data(), buf.size(), bytesRead, name, val);
    assert(ok);
    assert(bytesRead == buf.size());
    assert(name == "QUERY_STRING");
    assert(val.empty());

    std::cout << "[PASS] Empty parameter value encoding" << std::endl;
}

// Test multiple parameters and decodeAll
void testMultipleParams() {
    ParamEncoder encoder;
    encoder.add("REQUEST_METHOD", "GET");
    encoder.add("SCRIPT_FILENAME", "/home/travis/app/index.php");
    encoder.add("QUERY_STRING", "foo=bar&baz=1");
    encoder.add("REMOTE_ADDR", "127.0.0.1");
    encoder.add("SERVER_SOFTWARE", "Mercury/0.37.0");

    const std::vector<uint8_t>& buf = encoder.getBuffer();
    std::vector<std::pair<std::string_view, std::string_view>> out;
    bool ok = ParamEncoder::decodeAll(buf.data(), buf.size(), out);
    assert(ok);
    assert(out.size() == 5);
    assert(out[0].first == "REQUEST_METHOD" && out[0].second == "GET");
    assert(out[1].first == "SCRIPT_FILENAME" && out[1].second == "/home/travis/app/index.php");
    assert(out[2].first == "QUERY_STRING" && out[2].second == "foo=bar&baz=1");
    assert(out[3].first == "REMOTE_ADDR" && out[3].second == "127.0.0.1");
    assert(out[4].first == "SERVER_SOFTWARE" && out[4].second == "Mercury/0.37.0");

    std::cout << "[PASS] Multiple parameter sequence serialization and batch decoding" << std::endl;
}

// Test buildRecords framing and chunking
void testBuildRecordsFraming() {
    // Empty encoder -> produces single 8-byte terminator
    ParamEncoder emptyEncoder;
    std::vector<uint8_t> emptyRecords = emptyEncoder.buildRecords(1);
    assert(emptyRecords.size() == sizeof(FCGI_Header));
    const FCGI_Header* pTerm = reinterpret_cast<const FCGI_Header*>(emptyRecords.data());
    assert(pTerm->type == FCGI_PARAMS);
    assert(getContentLength(*pTerm) == 0);
    assert(getRequestId(*pTerm) == 1);

    // Small encoder -> produces 1 content record (aligned to 8 bytes) + 1 terminator
    ParamEncoder smallEncoder;
    smallEncoder.add("FOO", "BAR");
    std::vector<uint8_t> records = smallEncoder.buildRecords(1, true);

    // First record: Header (8 bytes) + payload + padding
    assert(records.size() >= sizeof(FCGI_Header) * 2);
    const FCGI_Header* pRec1 = reinterpret_cast<const FCGI_Header*>(records.data());
    assert(pRec1->type == FCGI_PARAMS);
    assert(getRequestId(*pRec1) == 1);
    uint16_t cLen = getContentLength(*pRec1);
    uint8_t pLen = pRec1->paddingLength;
    assert((cLen + pLen) % 8 == 0);
    size_t firstRecTotal = sizeof(FCGI_Header) + cLen + pLen;
    assert(firstRecTotal % 8 == 0);

    // Terminator record
    const FCGI_Header* pRec2 = reinterpret_cast<const FCGI_Header*>(records.data() + firstRecTotal);
    assert(pRec2->type == FCGI_PARAMS);
    assert(getContentLength(*pRec2) == 0);
    assert(records.size() == firstRecTotal + sizeof(FCGI_Header));

    // Chunking test w/ maxChunkSize = 32
    ParamEncoder chunkEncoder;
    for (int i = 0; i < 10; ++i) {
        chunkEncoder.add("KEY_" + std::to_string(i), "VAL_" + std::to_string(i));
    }
    size_t totalPayload = chunkEncoder.getPayloadSize();
    assert(totalPayload > 64); // Exceeds 2 chunks of 32

    std::vector<uint8_t> chunked = chunkEncoder.buildRecords(2, true, 32);
    // Parse through chunked stream and reassemble payload
    std::vector<uint8_t> reassembled;
    size_t offset = 0;
    int recordCount = 0;
    while (offset < chunked.size()) {
        assert(offset + sizeof(FCGI_Header) <= chunked.size());
        const FCGI_Header* pHead = reinterpret_cast<const FCGI_Header*>(chunked.data() + offset);
        assert(pHead->type == FCGI_PARAMS);
        assert(getRequestId(*pHead) == 2);
        uint16_t cl = getContentLength(*pHead);
        uint8_t pl = pHead->paddingLength;
        offset += sizeof(FCGI_Header);

        if (cl > 0) {
            assert(cl <= 32);
            reassembled.insert(reassembled.end(), chunked.data() + offset, chunked.data() + offset + cl);
            offset += cl;
            offset += pl; // Skip padding
        }
        ++recordCount;
    }

    assert(reassembled.size() == totalPayload);
    assert(reassembled == chunkEncoder.getBuffer());

    // Verify reassembled parameters decode cleanly
    std::vector<std::pair<std::string_view, std::string_view>> chunkOut;
    bool ok = ParamEncoder::decodeAll(reassembled.data(), reassembled.size(), chunkOut);
    assert(ok);
    assert(chunkOut.size() == 10);
    for (int i = 0; i < 10; ++i) {
        assert(chunkOut[i].first == "KEY_" + std::to_string(i));
        assert(chunkOut[i].second == "VAL_" + std::to_string(i));
    }

    std::cout << "[PASS] Record building, 8-byte alignment, and multi-chunk framing" << std::endl;
}

int main() {
    std::cout << "Running FastCGI protocol unit tests..." << std::endl;
    testWireStructures();
    testEndianConversions();
    testPaddingCalculation();
    testRecordHeaderFraming();
    testBeginEndRecords();
    testShortParamEncoding();
    testLongParamEncoding();
    testEmptyValueParamEncoding();
    testMultipleParams();
    testBuildRecordsFraming();
    std::cout << "\nAll FastCGI Phase 1 unit tests PASSED!" << std::endl;
    return 0;
}

