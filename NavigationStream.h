#ifndef CYBERNODE_NAVIGATION_STREAM_H
#define CYBERNODE_NAVIGATION_STREAM_H

#include <stddef.h>
#include <stdint.h>



static const uint8_t NAV_STREAM_VERSION = 3;
static const uint8_t NAV_STREAM_CAPACITY = 128;
static const uint16_t NAV_STREAM_MAX_PACKET = 240; 
static const uint16_t NAV_STREAM_HEADER_BYTES = 18;
static const uint16_t NAV_STREAM_POINT_BYTES = 12;

enum NavStreamPacketType : uint8_t {
    NAV_STREAM_BEGIN = 1,
    NAV_STREAM_APPEND = 2,
    NAV_STREAM_CURSOR = 3,
    NAV_STREAM_RESET = 4
};

enum NavStreamStatus : uint8_t {
    NAV_STREAM_OK = 0,
    NAV_STREAM_BAD_PACKET,
    NAV_STREAM_BAD_CRC,
    NAV_STREAM_BAD_SESSION,
    NAV_STREAM_BAD_REVISION,
    NAV_STREAM_BAD_SEQUENCE,
    NAV_STREAM_BAD_RANGE,
    NAV_STREAM_BUFFER_FULL,
    NAV_STREAM_NOT_INITIALIZED
};

struct NavStreamPoint {
    uint32_t routeDistanceCm;
    int32_t eastCm;
    int32_t northCm;
};

struct NavStreamCursor {
    uint32_t routeDistanceCm;
    uint16_t headingCentiDegrees; 
    uint16_t speedCmPerSec;
    uint16_t accuracyCm;
    uint32_t locationTimestampMs;
    uint32_t receivedAtMs;
    uint32_t sequence;
    bool valid;
};

struct NavStreamSnapshot {
    uint32_t sessionId;
    uint32_t routeRevision;
    uint32_t oldestPointIndex;
    uint32_t nextPointIndex;
    uint16_t pointCount;
    bool initialized;
    bool recalibrating;
    NavStreamCursor cursor;
    NavStreamPoint points[NAV_STREAM_CAPACITY];
};

class NavigationStream {
public:
    NavigationStream();
    NavStreamStatus processPacket(const uint8_t* packet, size_t length, uint32_t nowMs);
    void clear();
    bool snapshot(NavStreamSnapshot& out) const;
    uint32_t lastAcceptedSequence() const { return lastSequence_; }
    uint16_t bufferedPoints() const { return count_; }

private:
    NavStreamPoint points_[NAV_STREAM_CAPACITY];
    uint16_t head_;
    uint16_t count_;
    uint32_t oldestIndex_;
    uint32_t nextIndex_;
    uint32_t sessionId_;
    uint32_t revision_;
    uint32_t lastSequence_;
    bool initialized_;
    bool haveSequence_;
    bool recalibrating_;
    NavStreamCursor cursor_;

    static uint16_t crc16(const uint8_t* data, size_t length);
    static bool sequenceNewer(uint32_t candidate, uint32_t previous);
    NavStreamStatus appendChunk(uint32_t baseIndex, const NavStreamPoint* chunk, uint16_t chunkCount);
};

#endif
