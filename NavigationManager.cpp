#include "NavigationManager.h"
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <math.h>

static void navCopyText(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0) return;
    size_t length = source ? strlen(source) : 0;
    if (length >= capacity) length = capacity - 1;
    if (length) memcpy(destination, source, length);
    destination[length] = '\0';
}

namespace {





struct Field {
    char key[20];
    uint8_t type; 
    uint8_t valueCount;
    union Value {
        char text[40];
        uint8_t values[20];
        struct Numeric {
            uint32_t number;
            float decimalValue;
        } numeric;
    } value;
};
static_assert(sizeof(Field) <= 64, "Navigation parser field scratch must remain <=64 bytes");

class JsonReader {
public:
    JsonReader(const char* data, size_t length) : data_(data), length_(length), pos_(0) {}

    bool parse(Field* fields, uint8_t capacity, uint8_t& count) {
        count = 0;
        skipWs();
        if (!take('{')) return false;
        skipWs();
        if (take('}')) { skipWs(); return pos_ == length_; }
        while (pos_ < length_) {
            if (count >= capacity) return false;
            Field& f = fields[count];
            memset(&f, 0, sizeof(f));
            if (!readString(f.key, sizeof(f.key))) return false;
            for (uint8_t i = 0; i < count; ++i) if (strcmp(fields[i].key, f.key) == 0) return false;
            skipWs();
            if (!take(':')) return false;
            skipWs();
            if (peek() == '"') {
                f.type = 1;
                if (!readString(f.value.text, sizeof(f.value.text))) return false;
            } else if (peek() == '[') {
                f.type = 3;
                if (!readArray(f)) return false;
            } else {
                if (!readNumber(f)) return false;
            }
            ++count;
            skipWs();
            if (take('}')) break;
            if (!take(',')) return false;
            skipWs();
        }
        skipWs();
        return pos_ == length_;
    }

private:
    const char* data_;
    size_t length_;
    size_t pos_;
    char peek() const { return pos_ < length_ ? data_[pos_] : '\0'; }
    bool take(char c) { if (peek() != c) return false; ++pos_; return true; }
    void skipWs() { while (pos_ < length_ && (data_[pos_] == ' ' || data_[pos_] == '\t' || data_[pos_] == '\r' || data_[pos_] == '\n')) ++pos_; }

    bool readString(char* out, size_t capacity) {
        if (!take('"') || capacity == 0) return false;
        size_t used = 0;
        while (pos_ < length_) {
            uint32_t codepoint = (unsigned char)data_[pos_++];
            if (codepoint == '"') { out[used] = '\0'; return true; }
            if (codepoint < 0x20) return false;
            if (codepoint == '\\') {
                if (pos_ >= length_) return false;
                const char esc = data_[pos_++];
                switch (esc) {
                    case '"': codepoint = '"'; break;
                    case '\\': codepoint = '\\'; break;
                    case '/': codepoint = '/'; break;
                    case 'b': codepoint = ' '; break;
                    case 'f': codepoint = ' '; break;
                    case 'n': codepoint = ' '; break;
                    case 'r': codepoint = ' '; break;
                    case 't': codepoint = ' '; break;
                    case 'u': {
                        uint32_t first = 0;
                        if (!readHex4(first)) return false;
                        if (first >= 0xD800 && first <= 0xDBFF) {
                            if (pos_ + 2 > length_ || data_[pos_] != '\\' || data_[pos_ + 1] != 'u') return false;
                            pos_ += 2;
                            uint32_t second = 0;
                            if (!readHex4(second) || second < 0xDC00 || second > 0xDFFF) return false;
                            codepoint = 0x10000U + ((first - 0xD800U) << 10) + (second - 0xDC00U);
                        } else {
                            if (first >= 0xDC00 && first <= 0xDFFF) return false;
                            codepoint = first;
                        }
                        break;
                    }
                    default: return false;
                }
            }
            if (codepoint == 0 || codepoint < 0x20 || !appendUtf8(out, capacity, used, codepoint)) return false;
        }
        return false;
    }

    bool readHex4(uint32_t& value) {
        if (pos_ + 4 > length_) return false;
        value = 0;
        for (uint8_t i = 0; i < 4; ++i) {
            const char c = data_[pos_++];
            uint8_t digit;
            if (c >= '0' && c <= '9') digit = (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f') digit = (uint8_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = (uint8_t)(c - 'A' + 10);
            else return false;
            value = (value << 4) | digit;
        }
        return true;
    }

    bool appendUtf8(char* out, size_t capacity, size_t& used, uint32_t cp) {
        uint8_t bytes[4];
        uint8_t count = 0;
        if (cp <= 0x7F) { bytes[0] = (uint8_t)cp; count = 1; }
        else if (cp <= 0x7FF) {
            bytes[0] = (uint8_t)(0xC0 | (cp >> 6));
            bytes[1] = (uint8_t)(0x80 | (cp & 0x3F)); count = 2;
        } else if (cp <= 0xFFFF) {
            if (cp >= 0xD800 && cp <= 0xDFFF) return false;
            bytes[0] = (uint8_t)(0xE0 | (cp >> 12));
            bytes[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            bytes[2] = (uint8_t)(0x80 | (cp & 0x3F)); count = 3;
        } else if (cp <= 0x10FFFF) {
            bytes[0] = (uint8_t)(0xF0 | (cp >> 18));
            bytes[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
            bytes[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            bytes[3] = (uint8_t)(0x80 | (cp & 0x3F)); count = 4;
        } else return false;
        if (used + count >= capacity) return false;
        for (uint8_t i = 0; i < count; ++i) out[used++] = (char)bytes[i];
        return true;
    }

    bool readNumber(Field& field) {
        const size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (peek() == '0') {
            ++pos_;
            if (peek() >= '0' && peek() <= '9') return false;
        } else {
            if (peek() < '1' || peek() > '9') return false;
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }

        bool fractional = false;
        if (peek() == '.') {
            fractional = true;
            ++pos_;
            if (peek() < '0' || peek() > '9') return false;
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            fractional = true;
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (peek() < '0' || peek() > '9') return false;
            while (peek() >= '0' && peek() <= '9') ++pos_;
        }

        const char next = peek();
        if (!(next == ',' || next == '}' || next == ']' || next == ' ' ||
              next == '\t' || next == '\r' || next == '\n')) return false;
        const size_t tokenLength = pos_ - start;
        if (tokenLength == 0 || tokenLength >= 32) return false;

        char token[32];
        memcpy(token, data_ + start, tokenLength);
        token[tokenLength] = '\0';

        
        
        if (!fractional) {
            if (token[0] == '-') return false;
            uint64_t integer = 0;
            for (size_t i = 0; i < tokenLength; ++i) {
                const char c = token[i];
                if (c < '0' || c > '9') return false;
                integer = integer * 10ULL + (uint8_t)(c - '0');
                if (integer > UINT32_MAX) return false;
            }
            field.type = 2;
            field.value.numeric.number = (uint32_t)integer;
            field.value.numeric.decimalValue = (float)integer;
            return true;
        }

        char* end = nullptr;
        const float decimal = strtof(token, &end);
        if (!end || *end != '\0' || !isfinite(decimal)) return false;
        field.type = 4;
        field.value.numeric.decimalValue = decimal;
        return true;
    }

    bool readUnsigned(uint32_t& value) {
        const size_t start = pos_;
        uint64_t accumulator = 0;
        if (peek() < '0' || peek() > '9') return false;
        if (peek() == '0') ++pos_;
        else {
            while (peek() >= '0' && peek() <= '9') {
                accumulator = accumulator * 10U + (uint8_t)(data_[pos_] - '0');
                if (accumulator > UINT32_MAX) return false;
                ++pos_;
            }
        }
        if (pos_ == start) return false;
        if (data_[start] == '0') accumulator = 0;
        
        const char next = peek();
        if (!(next == ',' || next == '}' || next == ']' || next == ' ' || next == '\t' || next == '\r' || next == '\n')) return false;
        value = (uint32_t)accumulator;
        return true;
    }

    bool readArray(Field& field) {
        if (!take('[')) return false;
        skipWs();
        if (take(']')) return true;
        while (pos_ < length_) {
            if (field.valueCount >= 20) return false;
            uint32_t value = 0;
            
            
            if (!readUnsigned(value) || value > 127) return false;
            field.value.values[field.valueCount++] = (uint8_t)value;
            skipWs();
            if (take(']')) return true;
            if (!take(',')) return false;
            skipWs();
        }
        return false;
    }
};

const Field* findField(const Field* fields, uint8_t count, const char* key) {
    for (uint8_t i = 0; i < count; ++i) if (strcmp(fields[i].key, key) == 0) return &fields[i];
    return nullptr;
}

bool getString(const Field* fields, uint8_t count, const char* key, char* out, size_t size, bool required) {
    const Field* f = findField(fields, count, key);
    if (!f) { if (required && size) out[0] = '\0'; return !required; }
    if (f->type != 1 || strlen(f->value.text) >= size) return false;
    memcpy(out, f->value.text, strlen(f->value.text) + 1);
    return true;
}

bool getUInt(const Field* fields, uint8_t count, const char* key, uint32_t& value, bool required, bool* present = nullptr) {
    const Field* f = findField(fields, count, key);
    if (present) *present = f != nullptr;
    if (!f) return !required;
    if (f->type != 2) return false;
    value = f->value.numeric.number;
    return true;
}

bool getFloat(const Field* fields, uint8_t count, const char* key, float& value, bool required) {
    const Field* f = findField(fields, count, key);
    if (!f) return !required;
    if (f->type != 2 && f->type != 4) return false;
    value = (f->type == 2) ? (float)f->value.numeric.number : f->value.numeric.decimalValue;
    if (!isfinite(value) || value < 0.0f || value >= 360.0f) return false;
    return true;
}
bool isManeuver(const char* value) {
    static const char* const names[] = {
        "UNKNOWN",
        "DEPART",
        "CONTINUE_STRAIGHT",
        "STRAIGHT",

        "SLIGHT_LEFT",
        "TURN_SLIGHT_LEFT",
        "LEFT",
        "TURN_LEFT",
        "SHARP_LEFT",
        "TURN_SHARP_LEFT",
        "U_TURN_LEFT",

        "SLIGHT_RIGHT",
        "TURN_SLIGHT_RIGHT",
        "RIGHT",
        "TURN_RIGHT",
        "SHARP_RIGHT",
        "TURN_SHARP_RIGHT",
        "U_TURN_RIGHT",

        "KEEP_LEFT",
        "KEEP_RIGHT",

        "FORK_LEFT",
        "FORK_RIGHT",

        "ROUNDABOUT",
        "ROUNDABOUT_ENTER",
        "ROUNDABOUT_EXIT",

        "MERGE",
        "RAMP_LEFT",
        "RAMP_RIGHT",

        "ARRIVE",
        "REROUTING",
        "STOPPED"
    };

    if (value == nullptr) return false;

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(value, names[i]) == 0) {
            return true;
        }
    }

    return false;
}

bool readPair(const Field* f, uint8_t& x, uint8_t& y) {
    if (!f || f->type != 3 || f->valueCount != 2 || f->value.values[0] > 127 || f->value.values[1] > 53) return false;
    x = (uint8_t)f->value.values[0]; y = (uint8_t)f->value.values[1]; return true;
}
}

static bool parseNavigationMessageForVersion(const char* json, size_t length,
                                             NavigationMessage& output, uint8_t requiredVersion) {
    if (!json || length == 0 || length > NAV_MAX_PAYLOAD_BYTES) {
        return false;
    }
    Field fields[10]; uint8_t fieldCount = 0;
    JsonReader reader(json, length);
    if (!reader.parse(fields, 10, fieldCount)) return false;

    NavigationMessage parsed;
    memset(&parsed, 0, sizeof(parsed));
    char type[16] = {0};
    uint32_t number = 0;
    if (!getString(fields, fieldCount, "type", type, sizeof(type), true) ||
        !getUInt(fields, fieldCount, "v", number, true) || number != requiredVersion) return false;
    parsed.version = (uint8_t)number;
    if (!getUInt(fields, fieldCount, "sid", parsed.sessionId, true) || parsed.sessionId == 0 ||
        !getUInt(fields, fieldCount, "seq", parsed.sequence, true) ||
        (requiredVersion >= 2 && parsed.sequence == 0)) return false;

    if (strcmp(type, "NAV_STATE") == 0) {
        parsed.kind = NAV_MESSAGE_STATE;
        char state[16] = {0};
        if (!getString(fields, fieldCount, "state", state, sizeof(state), true)) return false;
        if (strcmp(state, "ENROUTE") == 0) parsed.state = NAV_ROUTE_ENROUTE;
        else if (strcmp(state, "REROUTING") == 0) parsed.state = NAV_ROUTE_REROUTING;
        else if (strcmp(state, "STOPPED") == 0) parsed.state = NAV_ROUTE_STOPPED;
        else return false;
        if (parsed.state == NAV_ROUTE_ENROUTE) {
            if (!getString(fields, fieldCount, "maneuver", parsed.maneuver, sizeof(parsed.maneuver), true) ||
                !isManeuver(parsed.maneuver)) return false;
            
            
            
            if (strcmp(parsed.maneuver, "TURN_LEFT") == 0) navCopyText(parsed.maneuver, sizeof(parsed.maneuver), "LEFT");
            else if (strcmp(parsed.maneuver, "TURN_RIGHT") == 0) navCopyText(parsed.maneuver, sizeof(parsed.maneuver), "RIGHT");
        } else if (parsed.state == NAV_ROUTE_REROUTING) {
            
            navCopyText(parsed.maneuver, sizeof(parsed.maneuver), "UNKNOWN");
        }
        if (!getUInt(fields, fieldCount, "distM", parsed.distanceToManeuverM, false, &parsed.hasDistanceToManeuver) ||
            !getUInt(fields, fieldCount, "remainM", parsed.remainingDistanceM, false, &parsed.hasRemainingDistance) ||
            !getUInt(fields, fieldCount, "remainS", parsed.remainingSeconds, false, &parsed.hasRemainingSeconds) ||
            !getString(fields, fieldCount, "road", parsed.road, sizeof(parsed.road), false)) return false;
        return (output = parsed, true);
    }

    if (strcmp(type, "NAV_MAP") == 0) {
        parsed.kind = NAV_MESSAGE_MAP;
        uint32_t frame = 0;
        if (!getUInt(fields, fieldCount, "frame", frame, true) || frame > UINT16_MAX ||
            (requiredVersion >= 2 && frame == 0)) return false;
        parsed.frame = (uint16_t)frame;
        if (requiredVersion >= 2) {
            char orientation[16] = {0};
            if (!getString(fields, fieldCount, "orientation", orientation, sizeof(orientation), true) ||
                strcmp(orientation, "COURSE_UP") != 0 ||
                !getFloat(fields, fieldCount, "courseDeg", parsed.courseDegrees, true)) return false;
            parsed.orientation = NAV_MAP_COURSE_UP;
            parsed.hasCourse = true;
        } else {
            parsed.orientation = NAV_MAP_NORTH_UP;
            parsed.hasCourse = false;
            parsed.courseDegrees = 0.0f;
        }
        const Field* pos = findField(fields, fieldCount, "pos");
        const Field* pts = findField(fields, fieldCount, "pts");
        if (!readPair(pos, parsed.positionX, parsed.positionY) || !pts || pts->type != 3 ||
            pts->valueCount < 4 || (pts->valueCount & 1U) != 0 || pts->valueCount > 20) return false;

        
        
        
        for (uint8_t i = 0; i < pts->valueCount; i += 2) {
            const uint8_t x = pts->value.values[i];
            const uint8_t y = pts->value.values[i + 1];
            if (y > 53) return false;
            if (parsed.pointCount > 0 &&
                parsed.pointX[parsed.pointCount - 1] == x &&
                parsed.pointY[parsed.pointCount - 1] == y) continue;
            if (parsed.pointCount >= NAV_MAX_ROUTE_POINTS) return false;
            parsed.pointX[parsed.pointCount] = x;
            parsed.pointY[parsed.pointCount] = y;
            ++parsed.pointCount;
        }
        if (parsed.pointCount < 2) return false;
        return (output = parsed, true);
    }

    if (strcmp(type, "NAV_POS") == 0) {
        parsed.kind = NAV_MESSAGE_POSITION;
        uint32_t frame = 0, x = 0, y = 0;
        if (!getUInt(fields, fieldCount, "frame", frame, true) || frame > UINT16_MAX ||
            (requiredVersion >= 2 && frame == 0) ||
            !getUInt(fields, fieldCount, "x", x, true) || x > 127 ||
            !getUInt(fields, fieldCount, "y", y, true) || y > 53) return false;
        parsed.frame = (uint16_t)frame; parsed.positionX = (uint8_t)x; parsed.positionY = (uint8_t)y;
        return (output = parsed, true);
    }
    return false;
}

bool parseNavigationMessageV1(const char* json, size_t length, NavigationMessage& output) {
    return parseNavigationMessageForVersion(json, length, output, 1);
}

bool parseNavigationMessageV2(const char* json, size_t length, NavigationMessage& output) {
    return parseNavigationMessageForVersion(json, length, output, 2);
}

bool parseNavigationMessage(const char* json, size_t length, NavigationMessage& output) {
    if (!json || length == 0 || length > NAV_MAX_PAYLOAD_BYTES) return false;
    NavigationMessage parsed;
    if (parseNavigationMessageV2(json, length, parsed)) {
        output = parsed;
        return true;
    }
    if (parseNavigationMessageV1(json, length, parsed)) {
        output = parsed;
        return true;
    }
    return false;
}

NavigationManager::NavigationManager() : haveSession_(false), haveStateSequence_(false), haveMapSequence_(false),
    havePositionSequence_(false), lastStateSequence_(0), lastMapSequence_(0), lastPositionSequence_(0), bleConnected_(false) {
    memset(&state_, 0, sizeof(state_));
    state_.orientation = NAV_MAP_NORTH_UP;
}

bool NavigationManager::isSequenceNewer(uint32_t candidate, uint32_t previous) {
    return (int32_t)(candidate - previous) > 0;
}
bool NavigationManager::isFrameNewer(uint16_t candidate, uint16_t previous) {
    return (int16_t)(candidate - previous) > 0;
}
void NavigationManager::resetForSession(uint32_t sessionId) {
    memset(&state_, 0, sizeof(state_));
    state_.sessionId = sessionId;
    state_.state = NAV_ROUTE_NONE;
    state_.orientation = NAV_MAP_NORTH_UP;
    haveSession_ = true;
    haveStateSequence_ = haveMapSequence_ = havePositionSequence_ = false;
    lastStateSequence_ = lastMapSequence_ = lastPositionSequence_ = 0;
}

NavigationUpdateResult NavigationManager::apply(const NavigationMessage& m, uint32_t nowMs) {
    if ((m.version != 1 && m.version != 2) || m.sessionId == 0) return NAV_UPDATE_REJECTED;
    bool newSession = false;
    if (m.kind == NAV_MESSAGE_STATE) {
        if (!haveSession_ || m.sessionId != state_.sessionId) {
            if (m.state == NAV_ROUTE_STOPPED) return NAV_UPDATE_REJECTED;
            resetForSession(m.sessionId);
            newSession = true;
        } else {
            if (state_.stopped && m.state != NAV_ROUTE_STOPPED) return NAV_UPDATE_REJECTED;
            if (haveStateSequence_ && !isSequenceNewer(m.sequence, lastStateSequence_)) return NAV_UPDATE_REJECTED;
        }

        lastStateSequence_ = m.sequence; haveStateSequence_ = true;
        state_.lastStateUpdateMs = nowMs;
        
        
        
        state_.linkLost = false;
        state_.stale = false;
        state_.state = m.state;
        if (m.state == NAV_ROUTE_STOPPED) {
            state_.active = false; state_.stopped = true; state_.hasMap = false;
            state_.pointCount = 0; state_.mapFrame = 0;
            state_.orientation = NAV_MAP_NORTH_UP;
            state_.hasCourse = false; state_.courseDegrees = 0.0f;
            state_.positionX = state_.positionY = 0;
            memset(state_.pointX, 0, sizeof(state_.pointX));
            memset(state_.pointY, 0, sizeof(state_.pointY));
            state_.road[0] = '\0'; state_.maneuver[0] = '\0';
            state_.hasDistanceToManeuver = state_.hasRemainingDistance = state_.hasRemainingSeconds = false;
            state_.distanceToManeuverM = state_.remainingDistanceM = state_.remainingSeconds = 0;
            state_.stale = false; state_.linkLost = false;
            return NAV_UPDATE_SESSION_STOPPED;
        }
        state_.active = true; state_.stopped = false;
        navCopyText(state_.maneuver, sizeof(state_.maneuver), m.maneuver);
        navCopyText(state_.road, sizeof(state_.road), m.road);
        state_.hasDistanceToManeuver = m.hasDistanceToManeuver;
        state_.hasRemainingDistance = m.hasRemainingDistance;
        state_.hasRemainingSeconds = m.hasRemainingSeconds;
        state_.distanceToManeuverM = m.distanceToManeuverM;
        state_.remainingDistanceM = m.remainingDistanceM;
        state_.remainingSeconds = m.remainingSeconds;
        if (m.state == NAV_ROUTE_REROUTING) {
            state_.maneuver[0] = '\0';
            state_.hasDistanceToManeuver = false;
            state_.road[0] = '\0';
            state_.hasMap = false;
            state_.pointCount = 0;
            state_.mapFrame = 0;
            state_.orientation = NAV_MAP_NORTH_UP;
            state_.hasCourse = false;
            state_.courseDegrees = 0.0f;
            memset(state_.pointX, 0, sizeof(state_.pointX));
            memset(state_.pointY, 0, sizeof(state_.pointY));
            state_.positionX = state_.positionY = 0;
        }
        return newSession ? NAV_UPDATE_SESSION_STARTED : NAV_UPDATE_ACCEPTED;
    }

    if (!haveSession_ || !state_.active || m.sessionId != state_.sessionId) return NAV_UPDATE_REJECTED;
    if (m.kind == NAV_MESSAGE_MAP) {
        
        
        
        if (state_.state == NAV_ROUTE_REROUTING) return NAV_UPDATE_REJECTED;
        if (haveMapSequence_ && !isSequenceNewer(m.sequence, lastMapSequence_)) return NAV_UPDATE_REJECTED;
        if (state_.hasMap && !isFrameNewer(m.frame, state_.mapFrame)) return NAV_UPDATE_REJECTED;
        lastMapSequence_ = m.sequence; haveMapSequence_ = true;
        state_.hasMap = true; state_.mapFrame = m.frame; state_.pointCount = m.pointCount;
        state_.orientation = m.orientation;
        state_.hasCourse = m.hasCourse;
        state_.courseDegrees = m.courseDegrees;
        memcpy(state_.pointX, m.pointX, sizeof(state_.pointX));
        memcpy(state_.pointY, m.pointY, sizeof(state_.pointY));
        state_.positionX = m.positionX; state_.positionY = m.positionY;
        return NAV_UPDATE_ACCEPTED;
    }
    if (m.kind == NAV_MESSAGE_POSITION) {
        if (!state_.hasMap || m.frame != state_.mapFrame ||
            (havePositionSequence_ && !isSequenceNewer(m.sequence, lastPositionSequence_))) return NAV_UPDATE_REJECTED;
        lastPositionSequence_ = m.sequence; havePositionSequence_ = true;
        state_.positionX = m.positionX; state_.positionY = m.positionY;
        return NAV_UPDATE_ACCEPTED;
    }
    return NAV_UPDATE_REJECTED;
}

void NavigationManager::setBleConnected(bool connected) {
    
    
    
    bleConnected_ = connected;
}

bool NavigationManager::isGuidanceFresh(uint32_t nowMs) const {
    return state_.active && (uint32_t)(nowMs - state_.lastStateUpdateMs) < NAV_GUIDANCE_STALE_MS;
}

bool NavigationManager::refreshStaleState(uint32_t nowMs) {
    const bool wasStale = state_.stale;
    state_.stale = state_.active && !isGuidanceFresh(nowMs);
    state_.linkLost = state_.active && state_.stale && !bleConnected_;
    return wasStale != state_.stale;
}

void NavigationManager::clear() {
    memset(&state_, 0, sizeof(state_));
    state_.orientation = NAV_MAP_NORTH_UP;
    haveSession_ = haveStateSequence_ = haveMapSequence_ = havePositionSequence_ = false;
    lastStateSequence_ = lastMapSequence_ = lastPositionSequence_ = 0;
    bleConnected_ = false;
}
