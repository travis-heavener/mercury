#include "params.hpp"

#include <algorithm>
#include <cstring>

#include "protocol.hpp"

namespace http::fastcgi {

    ParamEncoder::ParamEncoder() {
        this->buffer.reserve(2048);
    }

    ParamEncoder::ParamEncoder(size_t initialCapacity) {
        this->buffer.reserve(initialCapacity);
    }

    // Used to calculate encoded size for name-value pair
    size_t ParamEncoder::calculateEncodedSize(std::string_view name, std::string_view value) {
        size_t nameLenBytes = (name.size() < 128) ? 1 : 4;
        size_t valLenBytes = (value.size() < 128) ? 1 : 4;
        return nameLenBytes + valLenBytes + name.size() + value.size();
    }

    // Used to append length prefix to destination byte vector
    void ParamEncoder::appendLength(size_t len, std::vector<uint8_t>& dest) {
        if (len < 128) {
            dest.push_back(static_cast<uint8_t>(len));
        } else {
            dest.push_back(static_cast<uint8_t>((len >> 24) | 0x80));
            dest.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
            dest.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            dest.push_back(static_cast<uint8_t>(len & 0xFF));
        }
    }

    // Used to append name-value pair to destination byte vector
    void ParamEncoder::appendParam(std::string_view name, std::string_view value, std::vector<uint8_t>& dest) {
        size_t needed = calculateEncodedSize(name, value);
        dest.reserve(dest.size() + needed);

        appendLength(name.size(), dest);
        appendLength(value.size(), dest);

        if (!name.empty()) {
            const uint8_t* pName = reinterpret_cast<const uint8_t*>(name.data());
            dest.insert(dest.end(), pName, pName + name.size());
        }

        if (!value.empty()) {
            const uint8_t* pVal = reinterpret_cast<const uint8_t*>(value.data());
            dest.insert(dest.end(), pVal, pVal + value.size());
        }
    }

    void ParamEncoder::add(std::string_view name, std::string_view value) {
        appendParam(name, value, this->buffer);
    }

    void ParamEncoder::clear() {
        this->buffer.clear();
    }

    bool ParamEncoder::empty() const {
        return this->buffer.empty();
    }

    size_t ParamEncoder::getPayloadSize() const {
        return this->buffer.size();
    }

    const std::vector<uint8_t>& ParamEncoder::getBuffer() const {
        return this->buffer;
    }

    // Serialize into FCGI_PARAMS records w/ headers, padding, and empty terminator
    std::vector<uint8_t> ParamEncoder::buildRecords(uint16_t requestId, bool padTo8Bytes, size_t maxChunkSize) const {
        std::vector<uint8_t> records;

        // Constrain chunk size to protocol maximum
        if (maxChunkSize == 0 || maxChunkSize > FCGI_MAX_CONTENT_LENGTH)
            maxChunkSize = FCGI_MAX_CONTENT_LENGTH;

        // Handle non-empty payload
        size_t offset = 0;
        const size_t total = this->buffer.size();

        while (offset < total) {
            size_t chunkSize = std::min(total - offset, maxChunkSize);
            uint8_t padding = padTo8Bytes ? calculatePadding(chunkSize) : 0;

            FCGI_Header header = makeHeader(FCGI_PARAMS, requestId, static_cast<uint16_t>(chunkSize), padding);
            const uint8_t* pHeader = reinterpret_cast<const uint8_t*>(&header);

            // Append record header
            records.insert(records.end(), pHeader, pHeader + sizeof(FCGI_Header));

            // Append chunk payload
            const uint8_t* pChunk = this->buffer.data() + offset;
            records.insert(records.end(), pChunk, pChunk + chunkSize);

            // Append padding bytes
            if (padding > 0)
                records.insert(records.end(), padding, 0);

            offset += chunkSize;
        }

        // Append empty terminator record
        FCGI_Header termHeader = makeHeader(FCGI_PARAMS, requestId, 0, 0);
        const uint8_t* pTerm = reinterpret_cast<const uint8_t*>(&termHeader);
        records.insert(records.end(), pTerm, pTerm + sizeof(FCGI_Header));

        return records;
    }

    // Used to decode single parameter from byte stream
    bool ParamEncoder::decodeNext(const uint8_t* pData, size_t remaining, size_t& bytesRead,
                                  std::string_view& name, std::string_view& value) {
        bytesRead = 0;
        if (remaining < 2) return false;

        const uint8_t* pCur = pData;

        // Decode name length
        size_t nameLen = 0;
        if ((*pCur & 0x80) == 0) {
            nameLen = *pCur++;
            --remaining;
            ++bytesRead;
        } else {
            if (remaining < 4) return false;
            nameLen = ((static_cast<size_t>(pCur[0] & 0x7F) << 24) |
                       (static_cast<size_t>(pCur[1]) << 16) |
                       (static_cast<size_t>(pCur[2]) << 8) |
                       static_cast<size_t>(pCur[3]));
            pCur += 4;
            remaining -= 4;
            bytesRead += 4;
        }

        // Decode value length
        if (remaining < 1) return false;
        size_t valLen = 0;
        if ((*pCur & 0x80) == 0) {
            valLen = *pCur++;
            --remaining;
            ++bytesRead;
        } else {
            if (remaining < 4) return false;
            valLen = ((static_cast<size_t>(pCur[0] & 0x7F) << 24) |
                      (static_cast<size_t>(pCur[1]) << 16) |
                      (static_cast<size_t>(pCur[2]) << 8) |
                      static_cast<size_t>(pCur[3]));
            pCur += 4;
            remaining -= 4;
            bytesRead += 4;
        }

        // Verify sufficient payload bytes remain
        if (remaining < nameLen + valLen) return false;

        name = std::string_view(reinterpret_cast<const char*>(pCur), nameLen);
        pCur += nameLen;
        bytesRead += nameLen;

        value = std::string_view(reinterpret_cast<const char*>(pCur), valLen);
        bytesRead += valLen;

        return true;
    }

    // Used to decode all parameters from continuous byte stream
    bool ParamEncoder::decodeAll(const uint8_t* pData, size_t length,
                                 std::vector<std::pair<std::string_view, std::string_view>>& out) {
        size_t offset = 0;
        while (offset < length) {
            size_t bytesRead = 0;
            std::string_view name;
            std::string_view val;
            if (!decodeNext(pData + offset, length - offset, bytesRead, name, val)) return false;
            out.emplace_back(name, val);
            offset += bytesRead;
        }
        return true;
    }

}

