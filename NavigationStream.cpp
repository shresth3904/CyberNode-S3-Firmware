#include "NavigationStream.h"
#include <string.h>

namespace {
static uint16_t readU16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t readU32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int32_t readI32(const uint8_t* p) { return (int32_t)readU32(p); }
}

NavigationStream::NavigationStream() : head_(0), count_(0), oldestIndex_(0), nextIndex_(0),
    sessionId_(0), revision_(0), lastSequence_(0), initialized_(false), haveSequence_(false),
    recalibrating_(false), cursor_{} {
    memset(points_, 0, sizeof(points_));
}

uint16_t NavigationStream::crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
    }
    return crc;
}

bool NavigationStream::sequenceNewer(uint32_t candidate, uint32_t previous) {
    return (int32_t)(candidate - previous) > 0;
}

void NavigationStream::clear() {
    head_ = count_ = 0;
    oldestIndex_ = nextIndex_ = 0;
    sessionId_ = revision_ = lastSequence_ = 0;
    initialized_ = haveSequence_ = recalibrating_ = false;
    memset(&cursor_, 0, sizeof(cursor_));
}

NavStreamStatus NavigationStream::appendChunk(uint32_t baseIndex, const NavStreamPoint* chunk, uint16_t chunkCount) {
    if (!chunk || chunkCount == 0 || chunkCount > NAV_STREAM_CAPACITY) return NAV_STREAM_BAD_RANGE;
    if (baseIndex != nextIndex_) return NAV_STREAM_BAD_SEQUENCE;
    if ((uint32_t)count_ + chunkCount > NAV_STREAM_CAPACITY) return NAV_STREAM_BUFFER_FULL;
    for (uint16_t i = 0; i < chunkCount; ++i) {
        if (i && chunk[i].routeDistanceCm <= chunk[i - 1].routeDistanceCm) return NAV_STREAM_BAD_RANGE;
        if (count_ && chunk[i].routeDistanceCm <= points_[(head_ + count_ - 1) % NAV_STREAM_CAPACITY].routeDistanceCm)
            return NAV_STREAM_BAD_RANGE;
        points_[(head_ + count_) % NAV_STREAM_CAPACITY] = chunk[i];
        ++count_;
        ++nextIndex_;
    }
    if (count_ == chunkCount) oldestIndex_ = baseIndex;
    return NAV_STREAM_OK;
}

NavStreamStatus NavigationStream::processPacket(const uint8_t* packet, size_t length, uint32_t nowMs) {
    if (!packet || length < NAV_STREAM_HEADER_BYTES + 2 || length > NAV_STREAM_MAX_PACKET) return NAV_STREAM_BAD_PACKET;
    if (packet[0] != 'N' || packet[1] != 'V' || packet[2] != NAV_STREAM_VERSION) return NAV_STREAM_BAD_PACKET;
    const uint8_t type = packet[3];
    const uint16_t payloadLength = readU16(packet + 4);
    const uint32_t session = readU32(packet + 6);
    const uint32_t revision = readU32(packet + 10);
    const uint32_t sequence = readU32(packet + 14);
    if ((size_t)NAV_STREAM_HEADER_BYTES + payloadLength + 2 != length) return NAV_STREAM_BAD_PACKET;
    const uint16_t expectedCrc = readU16(packet + length - 2);
    if (crc16(packet, length - 2) != expectedCrc) return NAV_STREAM_BAD_CRC;
    if (sequence == 0) return NAV_STREAM_BAD_SEQUENCE;

    if (type == NAV_STREAM_BEGIN || type == NAV_STREAM_RESET) {
        if (payloadLength != 4) return NAV_STREAM_BAD_PACKET; 
        
        
        if (initialized_ && session == sessionId_ && haveSequence_ &&
            !sequenceNewer(sequence, lastSequence_)) return NAV_STREAM_BAD_SEQUENCE;
        
        clear();
        sessionId_ = session;
        revision_ = revision;
        initialized_ = true;
        recalibrating_ = true;
        nextIndex_ = oldestIndex_ = readU32(packet + NAV_STREAM_HEADER_BYTES);
        lastSequence_ = sequence;
        haveSequence_ = true;
        return NAV_STREAM_OK;
    }

    if (!initialized_) return NAV_STREAM_NOT_INITIALIZED;
    if (session != sessionId_) return NAV_STREAM_BAD_SESSION;
    if (revision != revision_) return NAV_STREAM_BAD_REVISION;
    if (haveSequence_ && !sequenceNewer(sequence, lastSequence_)) return NAV_STREAM_BAD_SEQUENCE;

    const uint8_t* payload = packet + NAV_STREAM_HEADER_BYTES;
    if (type == NAV_STREAM_APPEND) {
        if (payloadLength < 5) return NAV_STREAM_BAD_PACKET;
        const uint32_t baseIndex = readU32(payload);
        const uint8_t count = payload[4];
        if (count == 0 || payloadLength != (uint16_t)(5 + count * NAV_STREAM_POINT_BYTES)) return NAV_STREAM_BAD_PACKET;
        NavStreamPoint chunk[NAV_STREAM_CAPACITY];
        if (count > NAV_STREAM_CAPACITY) return NAV_STREAM_BAD_RANGE;
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t* p = payload + 5 + i * NAV_STREAM_POINT_BYTES;
            chunk[i].routeDistanceCm = readU32(p);
            chunk[i].eastCm = readI32(p + 4);
            chunk[i].northCm = readI32(p + 8);
        }
        const NavStreamStatus result = appendChunk(baseIndex, chunk, count);
        if (result != NAV_STREAM_OK) return result;
        recalibrating_ = count_ < 2;
    } else if (type == NAV_STREAM_CURSOR) {
        if (payloadLength != 14) return NAV_STREAM_BAD_PACKET;
        NavStreamCursor next{};
        next.routeDistanceCm = readU32(payload);
        next.headingCentiDegrees = readU16(payload + 4);
        next.speedCmPerSec = readU16(payload + 6);
        next.accuracyCm = readU16(payload + 8);
        next.locationTimestampMs = readU32(payload + 10);
        if (next.headingCentiDegrees >= 36000 || next.accuracyCm > 10000) return NAV_STREAM_BAD_RANGE;
        if (count_ < 2 || next.routeDistanceCm < points_[head_].routeDistanceCm ||
            next.routeDistanceCm > points_[(head_ + count_ - 1) % NAV_STREAM_CAPACITY].routeDistanceCm)
            return NAV_STREAM_BAD_RANGE;
        next.receivedAtMs = nowMs;
        next.sequence = sequence;
        next.valid = true;
        cursor_ = next;
        
        
        const uint32_t retainBehindCm = 5000UL;
        const uint32_t trimBefore = next.routeDistanceCm > retainBehindCm
            ? next.routeDistanceCm - retainBehindCm : 0UL;
        while (count_ > 2 && points_[head_].routeDistanceCm < trimBefore) {
            head_ = (uint16_t)((head_ + 1) % NAV_STREAM_CAPACITY);
            --count_;
            ++oldestIndex_;
        }
        recalibrating_ = false;
    } else {
        return NAV_STREAM_BAD_PACKET;
    }
    lastSequence_ = sequence;
    haveSequence_ = true;
    return NAV_STREAM_OK;
}

bool NavigationStream::snapshot(NavStreamSnapshot& out) const {
    memset(&out, 0, sizeof(out));
    out.sessionId = sessionId_;
    out.routeRevision = revision_;
    out.oldestPointIndex = oldestIndex_;
    out.nextPointIndex = nextIndex_;
    out.pointCount = count_;
    out.initialized = initialized_;
    out.recalibrating = recalibrating_;
    out.cursor = cursor_;
    for (uint16_t i = 0; i < count_; ++i) out.points[i] = points_[(head_ + i) % NAV_STREAM_CAPACITY];
    return initialized_;
}
