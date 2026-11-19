#ifndef CYBERNODE_NAVIGATION_MANAGER_H
#define CYBERNODE_NAVIGATION_MANAGER_H

#include <stddef.h>
#include <stdint.h>

static const size_t NAV_MAX_PAYLOAD_BYTES = 240;

static const size_t NAV_V1_MAX_PAYLOAD_BYTES = NAV_MAX_PAYLOAD_BYTES;
static const uint32_t NAV_GUIDANCE_STALE_MS = 5000UL;
static const uint8_t NAV_MAX_ROUTE_POINTS = 10;

enum NavigationMessageKind : uint8_t { NAV_MESSAGE_STATE = 0, NAV_MESSAGE_MAP = 1, NAV_MESSAGE_POSITION = 2 };
enum NavigationRouteState : uint8_t { NAV_ROUTE_NONE = 0, NAV_ROUTE_ENROUTE = 1, NAV_ROUTE_REROUTING = 2, NAV_ROUTE_STOPPED = 3 };
enum NavigationMapOrientation : uint8_t { NAV_MAP_NORTH_UP = 0, NAV_MAP_COURSE_UP = 1 };

enum NavigationUpdateResult : uint8_t {
    NAV_UPDATE_REJECTED = 0,
    NAV_UPDATE_ACCEPTED = 1,
    NAV_UPDATE_SESSION_STARTED = 2,
    NAV_UPDATE_SESSION_STOPPED = 3
};

struct NavigationMessage {
    NavigationMessageKind kind;
    uint8_t version;
    uint32_t sessionId;
    uint32_t sequence;

    NavigationRouteState state;
    char maneuver[20];
    char road[33];
    bool hasDistanceToManeuver;
    bool hasRemainingDistance;
    bool hasRemainingSeconds;
    uint32_t distanceToManeuverM;
    uint32_t remainingDistanceM;
    uint32_t remainingSeconds;

    uint16_t frame;
    NavigationMapOrientation orientation;
    bool hasCourse;
    float courseDegrees;
    uint8_t pointCount;
    uint8_t pointX[NAV_MAX_ROUTE_POINTS];
    uint8_t pointY[NAV_MAX_ROUTE_POINTS];
    uint8_t positionX;
    uint8_t positionY;
};

struct NavigationSnapshot {
    uint32_t sessionId;
    NavigationRouteState state;
    char maneuver[20];
    char road[33];
    bool hasDistanceToManeuver;
    bool hasRemainingDistance;
    bool hasRemainingSeconds;
    uint32_t distanceToManeuverM;
    uint32_t remainingDistanceM;
    uint32_t remainingSeconds;

    bool hasMap;
    uint16_t mapFrame;
    NavigationMapOrientation orientation;
    bool hasCourse;
    float courseDegrees;
    uint8_t pointCount;
    uint8_t pointX[NAV_MAX_ROUTE_POINTS];
    uint8_t pointY[NAV_MAX_ROUTE_POINTS];
    uint8_t positionX;
    uint8_t positionY;
    bool active;
    bool stopped;
    bool stale;
    bool linkLost;
    uint32_t lastStateUpdateMs;
};




static inline bool navigationRequiresAwakeDisplay(bool keepScreenAwake,
                                                  const NavigationSnapshot& state,
                                                  uint32_t nowMs) {
    return keepScreenAwake && state.active && !state.stale &&
           (uint32_t)(nowMs - state.lastStateUpdateMs) < NAV_GUIDANCE_STALE_MS;
}




bool parseNavigationMessage(const char* json, size_t length, NavigationMessage& output);
bool parseNavigationMessageV1(const char* json, size_t length, NavigationMessage& output);
bool parseNavigationMessageV2(const char* json, size_t length, NavigationMessage& output);

class NavigationManager {
public:
    NavigationManager();

    NavigationUpdateResult apply(const NavigationMessage& message, uint32_t nowMs);
    void setBleConnected(bool connected);
    bool refreshStaleState(uint32_t nowMs);
    const NavigationSnapshot& snapshot() const { return state_; }
    bool hasActiveSession() const { return state_.active; }
    bool isGuidanceFresh(uint32_t nowMs) const;
    void clear();

private:
    NavigationSnapshot state_;
    bool haveSession_;
    bool haveStateSequence_;
    bool haveMapSequence_;
    bool havePositionSequence_;
    uint32_t lastStateSequence_;
    uint32_t lastMapSequence_;
    uint32_t lastPositionSequence_;
    bool bleConnected_;

    static bool isSequenceNewer(uint32_t candidate, uint32_t previous);
    static bool isFrameNewer(uint16_t candidate, uint16_t previous);
    void resetForSession(uint32_t sessionId);
};

#endif
