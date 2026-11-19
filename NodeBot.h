

#ifndef CYBERNODE_NODE_BOT_H
#define CYBERNODE_NODE_BOT_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "CyberNodeTypes.h"


enum NodeBotReaction : uint8_t {
    NODEBOT_REACTION_NONE = 0,
    NODEBOT_REACTION_GREETING,
    NODEBOT_REACTION_NOTIFICATION,
    NODEBOT_REACTION_BLE_CONNECTED,
    NODEBOT_REACTION_BLE_DISCONNECTED,
    NODEBOT_REACTION_DATA_SYNCED,
    NODEBOT_REACTION_STEPS_UP,
    NODEBOT_REACTION_GOAL_REACHED,
    NODEBOT_REACTION_CALL_ENDED,
    NODEBOT_REACTION_MISSED_CALL,
    NODEBOT_REACTION_WEATHER_CHANGED,
    NODEBOT_REACTION_ALARM_DISMISSED,
    NODEBOT_REACTION_TIMER_DISMISSED,
    NODEBOT_REACTION_DATA_STALE
};

namespace NodeBot {

static constexpr uint16_t kRefreshIntervalMs = 120;

#define NODEBOT_FONT_BODY  u8g2_font_7x13B_tf
#define NODEBOT_FONT_SMALL u8g2_font_6x12_tf

class Controller {
public:
    void reset() {
        reaction_ = NODEBOT_REACTION_NONE;
        reactionUntilMs_ = 0;
        bleBaselineReady_ = false;
        lastBleConnected_ = false;
        freshnessBaselineReady_ = false;
        lastPhoneFresh_ = false;
        dataBaselineReady_ = false;
        lastSteps_ = -1;
        lastDailyGoal_ = 0;
        lastTemperature_ = 0;
        lastWeather_[0] = '\0';
        meaningfulDataReactionThisIteration_ = false;
        alarmRinging_ = false;
        timerRinging_ = false;
        goalReached_ = false;
    }

    void beginUiIteration() { meaningfulDataReactionThisIteration_ = false; }

    static inline uint8_t priority(NodeBotReaction reaction) {
        switch (reaction) {
            case NODEBOT_REACTION_GOAL_REACHED:
            case NODEBOT_REACTION_MISSED_CALL:       return 7;
            case NODEBOT_REACTION_NOTIFICATION:      return 6;
            case NODEBOT_REACTION_BLE_DISCONNECTED:
            case NODEBOT_REACTION_DATA_STALE:        return 5;
            case NODEBOT_REACTION_GREETING:           return 4;
            case NODEBOT_REACTION_STEPS_UP:
            case NODEBOT_REACTION_CALL_ENDED:
            case NODEBOT_REACTION_WEATHER_CHANGED:
            case NODEBOT_REACTION_ALARM_DISMISSED:
            case NODEBOT_REACTION_TIMER_DISMISSED:    return 3;
            case NODEBOT_REACTION_BLE_CONNECTED:      return 2;
            case NODEBOT_REACTION_DATA_SYNCED:        return 1;
            default:                                  return 0;
        }
    }

    void trigger(NodeBotReaction reaction, uint32_t nowMs, bool dataSpecific = false) {
        if (reaction == NODEBOT_REACTION_NONE) return;
        const bool active = (int32_t)(reactionUntilMs_ - nowMs) > 0;
        if (active && priority(reaction) < priority(reaction_)) return;
        reaction_ = reaction;
        reactionUntilMs_ = nowMs + duration(reaction);
        if (dataSpecific) meaningfulDataReactionThisIteration_ = true;
    }

    void greet(uint32_t nowMs) { trigger(NODEBOT_REACTION_GREETING, nowMs); }

    void observeBle(bool connected, uint32_t nowMs) {
        if (!bleBaselineReady_) {
            bleBaselineReady_ = true;
            lastBleConnected_ = connected;
            return;
        }
        if (lastBleConnected_ == connected) return;
        trigger(connected ? NODEBOT_REACTION_BLE_CONNECTED : NODEBOT_REACTION_BLE_DISCONNECTED, nowMs);
        lastBleConnected_ = connected;
    }

    void observeAlertDismissed(bool timerOnly, uint32_t nowMs) {
        trigger(timerOnly ? NODEBOT_REACTION_TIMER_DISMISSED : NODEBOT_REACTION_ALARM_DISMISSED, nowMs);
    }

    void observeCallFinished(const char* terminalLabel, uint32_t nowMs) {
        const bool missed = terminalLabel && strcmp(terminalLabel, "MISSED") == 0;
        trigger(missed ? NODEBOT_REACTION_MISSED_CALL : NODEBOT_REACTION_CALL_ENDED, nowMs);
    }

    void observeNotifications(uint8_t newNotificationCount, bool alertsEnabled, uint32_t nowMs) {
        if (newNotificationCount > 0 && alertsEnabled)
            trigger(NODEBOT_REACTION_NOTIFICATION, nowMs, true);
    }

    void setHomeContext(const WatchData& data, bool hasCentralData, bool alarmRinging, bool timerRinging) {
        alarmRinging_ = alarmRinging;
        timerRinging_ = timerRinging;
        goalReached_ = hasCentralData && data.dailyGoal > 0 && data.steps >= data.dailyGoal;
    }

    bool alarmRinging() const { return alarmRinging_ || timerRinging_; }
    bool goalReached() const { return goalReached_; }

    
    
    void observeWatchData(const WatchData& data, bool phoneFreshBeforeUpdate, uint32_t nowMs) {
        if (!dataBaselineReady_) {
            storeDataBaseline(data);
            dataBaselineReady_ = true;
            return;
        }

        if (lastSteps_ >= 0 && data.steps > lastSteps_) {
            const int32_t delta = data.steps - lastSteps_;
            const bool crossedThousand = (lastSteps_ / 1000) < (data.steps / 1000);
            if (delta >= 20 || crossedThousand)
                trigger(NODEBOT_REACTION_STEPS_UP, nowMs, true);
        }

        const bool goalWasReached = lastDailyGoal_ > 0 && lastSteps_ >= lastDailyGoal_;
        const bool goalIsReached = data.dailyGoal > 0 && data.steps >= data.dailyGoal;
        if (!goalWasReached && goalIsReached)
            trigger(NODEBOT_REACTION_GOAL_REACHED, nowMs, true);

        if (phoneFreshBeforeUpdate &&
            (strcmp(lastWeather_, data.weather) != 0 || abs((int)lastTemperature_ - (int)data.temperature) >= 3)) {
            trigger(NODEBOT_REACTION_WEATHER_CHANGED, nowMs, true);
        }
        storeDataBaseline(data);
    }

    
    
    void observePhoneFreshness(bool fresh, uint32_t nowMs) {
        if (!freshnessBaselineReady_) {
            freshnessBaselineReady_ = true;
            lastPhoneFresh_ = fresh;
            return;
        }
        if (lastPhoneFresh_ != fresh) {
            if (!meaningfulDataReactionThisIteration_)
                trigger(fresh ? NODEBOT_REACTION_DATA_SYNCED : NODEBOT_REACTION_DATA_STALE, nowMs);
            lastPhoneFresh_ = fresh;
        }
    }

    NodeBotReaction activeReaction(uint32_t nowMs) const {
        return (int32_t)(reactionUntilMs_ - nowMs) > 0 ? reaction_ : NODEBOT_REACTION_NONE;
    }
    uint32_t reactionUntilMs() const { return reactionUntilMs_; }

private:
    static inline uint32_t duration(NodeBotReaction reaction) {
        switch (reaction) {
            case NODEBOT_REACTION_GREETING:         return 2300UL;
            case NODEBOT_REACTION_NOTIFICATION:     return 2100UL;
            case NODEBOT_REACTION_BLE_CONNECTED:    return 1900UL;
            case NODEBOT_REACTION_BLE_DISCONNECTED: return 2600UL;
            case NODEBOT_REACTION_DATA_SYNCED:      return 2200UL;
            case NODEBOT_REACTION_STEPS_UP:         return 1800UL;
            case NODEBOT_REACTION_GOAL_REACHED:     return 4200UL;
            case NODEBOT_REACTION_CALL_ENDED:       return 2000UL;
            case NODEBOT_REACTION_MISSED_CALL:      return 2500UL;
            case NODEBOT_REACTION_WEATHER_CHANGED:  return 1900UL;
            case NODEBOT_REACTION_ALARM_DISMISSED:  return 1600UL;
            case NODEBOT_REACTION_TIMER_DISMISSED:  return 2200UL;
            case NODEBOT_REACTION_DATA_STALE:       return 2600UL;
            default:                                return 2200UL;
        }
    }

    void storeDataBaseline(const WatchData& data) {
        lastSteps_ = data.steps;
        lastDailyGoal_ = data.dailyGoal;
        lastTemperature_ = data.temperature;
        strncpy(lastWeather_, data.weather, sizeof(lastWeather_) - 1);
        lastWeather_[sizeof(lastWeather_) - 1] = '\0';
    }

    NodeBotReaction reaction_ = NODEBOT_REACTION_NONE;
    uint32_t reactionUntilMs_ = 0;
    bool bleBaselineReady_ = false;
    bool lastBleConnected_ = false;
    bool freshnessBaselineReady_ = false;
    bool lastPhoneFresh_ = false;
    bool dataBaselineReady_ = false;
    int32_t lastSteps_ = -1;
    int32_t lastDailyGoal_ = 0;
    int16_t lastTemperature_ = 0;
    char lastWeather_[16] = {0};
    bool meaningfulDataReactionThisIteration_ = false;
    bool alarmRinging_ = false;
    bool timerRinging_ = false;
    bool goalReached_ = false;
};

inline Controller& controller() {
    static Controller instance;
    return instance;
}



static inline uint32_t hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dUL;
    x ^= x >> 15; x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}
inline bool parseTime(const char* t, int& h, int& m) {
    if (!t || strlen(t) != 5 || t[2] != ':' || t[0] < '0' || t[0] > '9' ||
        t[1] < '0' || t[1] > '9' || t[3] < '0' || t[3] > '9' || t[4] < '0' || t[4] > '9') return false;
    h = (t[0] - '0') * 10 + t[1] - '0';
    m = (t[3] - '0') * 10 + t[4] - '0';
    return h <= 23 && m <= 59;
}
template <typename FrameData>
static inline bool isNight(const FrameData& f) {
    int h = 0, m = 0;
    return parseTime(f.time, h, m) && (h >= 23 || h < 6);
}
static inline void chamferFrame(U8G2& o, int x, int y, int w, int h, int c) {
    o.drawHLine(x + c, y, w - 2 * c); o.drawHLine(x + c, y + h - 1, w - 2 * c);
    o.drawVLine(x, y + c, h - 2 * c); o.drawVLine(x + w - 1, y + c, h - 2 * c);
    o.drawLine(x, y + c, x + c, y); o.drawLine(x + w - 1 - c, y, x + w - 1, y + c);
    o.drawLine(x, y + h - 1 - c, x + c, y + h - 1);
    o.drawLine(x + w - 1 - c, y + h - 1, x + w - 1, y + h - 1 - c);
}
template <typename FrameData>
static inline void drawStatusIcons(U8G2& o, const FrameData& f) {
    o.drawFrame(105, 2, 11, 7); o.drawBox(116, 4, 2, 3);
    if (f.batteryPercent < 0) o.drawLine(108, 6, 110, 4);
    else {
        int percent = f.batteryPercent > 100 ? 100 : f.batteryPercent;
        const int fill = (percent * 7 + 50) / 100;
        if (fill > 0) o.drawBox(107, 4, fill, 3);
    }
    if (f.bleConnected) o.drawDisc(122, 5, 2); else o.drawCircle(122, 5, 2);
}
template <typename FrameData>
static inline void weatherText(const FrameData& f, char* out, size_t n) {
    if (!f.hasCentralData || !f.weather || !f.weather[0] || strcmp(f.weather, "NO DATA") == 0 ||
        strcmp(f.weather, "N/A") == 0 || f.temperature < -50 || f.temperature > 80) {
        snprintf(out, n, "WX --"); return;
    }
    char condition[7] = {0}; uint8_t i = 0;
    while (f.weather[i] && f.weather[i] != ' ' && i < sizeof(condition) - 1) { condition[i] = f.weather[i]; ++i; }
    snprintf(out, n, "%d\xC2\xB0""C %.6s", f.temperature, condition);
}
template <typename FrameData>
static inline void stepsText(const FrameData& f, char* out, size_t n) {
    if (!f.hasCentralData || f.steps < 0) { snprintf(out, n, "--"); return; }
    char raw[16]; snprintf(raw, sizeof(raw), "%ld", (long)f.steps);
    const size_t len = strlen(raw); size_t p = 0;
    for (size_t i = 0; i < len && p + 2 < n; ++i) {
        if (i > 0 && ((len - i) % 3) == 0) out[p++] = ',';
        out[p++] = raw[i];
    }
    out[p] = '\0';
}
template <typename FrameData>
static inline void splitDate(const FrameData& f, char* dow, char* day, char* mon) {
    dow[0] = day[0] = mon[0] = '\0';
    if (!f.date || !f.date[0]) { snprintf(dow, 8, "--"); return; }
    sscanf(f.date, "%7s %7s %7s", dow, day, mon);
}

struct NodeBotMoodProfile {
    int8_t browLift;
    int8_t lidDrop;
    int8_t gazeX;
    int8_t gazeY;
    uint8_t blush;
    uint8_t mouth;
};

static inline NodeBotMoodProfile nodeBotMoodForBucket(uint32_t bucket) {
    
    switch (hash(bucket * 73UL + 0x4e4f4445UL) % 8UL) {
        case 0: return { 0, 0,  0,  0, 0, 0 }; 
        case 1: return { 2, 0,  0, -1, 0, 2 }; 
        case 2: return {-1, 2,  0,  1, 3, 1 }; 
        case 3: return { 1, 1,  1,  0, 2, 3 }; 
        case 4: return { 0, 2, -1,  1, 1, 1 }; 
        case 5: return { 1, 3,  1,  1, 0, 1 }; 
        case 6: return { 3, 0, -1,  0, 2, 5 }; 
        default:return { 0, 1,  1,  0, 0, 4 }; 
    }
}




static inline uint8_t nodeBotIdleCueAt(uint32_t nowMs) {
    const uint32_t bucket = nowMs / 17000UL;
    const uint32_t seed = hash(bucket * 313UL + 0x71b07UL);
    if ((seed % 4UL) != 0UL) return 0; 
    const uint32_t start = bucket * 17000UL + 2200UL + ((seed >> 8) % 11800UL);
    if (nowMs < start) return 0;
    const uint32_t age = nowMs - start;
    const uint8_t cue = (uint8_t)(1UL + ((seed >> 20) % 4UL));
    
    
    if (age >= (cue == 4U ? 220UL : 620UL)) return 0;
    return cue; 
}

static inline uint8_t nodeBotEase8(uint32_t elapsed, uint32_t duration) {
    if (duration == 0 || elapsed >= duration) return 255;
    const uint32_t t = (elapsed * 255UL) / duration;
    
    return (uint8_t)((t * t * (765UL - 2UL * t)) / 65025UL);
}

static inline int nodeBotMix(int a, int b, uint8_t amount) {
    const int scaled = (b - a) * (int)amount;
    const int delta = scaled >= 0 ? (scaled + 127) / 255 : (scaled - 127) / 255;
    return a + delta;
}

static inline NodeBotMoodProfile nodeBotMoodAt(uint32_t nowMs) {
    const uint32_t bucket = nowMs / 13000UL;
    const uint32_t within = nowMs % 13000UL;
    
    const uint8_t blend = nodeBotEase8(within, 900UL);
    const NodeBotMoodProfile a = nodeBotMoodForBucket(bucket - 1UL);
    const NodeBotMoodProfile b = nodeBotMoodForBucket(bucket);
    NodeBotMoodProfile out;
    out.browLift = (int8_t)nodeBotMix(a.browLift, b.browLift, blend);
    out.lidDrop = (int8_t)nodeBotMix(a.lidDrop, b.lidDrop, blend);
    out.gazeX = (int8_t)nodeBotMix(a.gazeX, b.gazeX, blend);
    out.gazeY = (int8_t)nodeBotMix(a.gazeY, b.gazeY, blend);
    out.blush = (uint8_t)nodeBotMix(a.blush, b.blush, blend);
    out.mouth = (uint8_t)nodeBotMix(a.mouth, b.mouth, blend);
    return out;
}

static inline int nodeBotSmoothGaze(uint32_t nowMs, uint32_t periodMs, uint32_t salt, int range) {
    const uint32_t bucket = nowMs / periodMs;
    const uint8_t blend = nodeBotEase8(nowMs % periodMs, periodMs);
    const int span = range * 2 + 1;
    const int a = (int)(hash(bucket * 101UL + salt) % (uint32_t)span) - range;
    const int b = (int)(hash((bucket + 1UL) * 101UL + salt) % (uint32_t)span) - range;
    return nodeBotMix(a, b, blend);
}

static inline uint8_t nodeBotBlinkOpen(uint32_t nowMs, bool sleepy) {
    const uint32_t slotMs = sleepy ? 3900UL : 2450UL;
    const uint32_t slot = nowMs / slotMs;
    const uint8_t skip = sleepy ? 2U : 4U; 
    const uint16_t duration = sleepy ? 560U : 190U;
    for (uint8_t back = 0; back < 2; ++back) {
        
        
        
        if (back > slot) continue;
        const uint32_t eventSlot = slot - back;
        const uint32_t h = hash(eventSlot * 977UL + (sleepy ? 0x51eeUL : 0xb11aUL));
        if ((h % skip) != 0UL) continue;
        const uint32_t eventAt = eventSlot * slotMs + 350UL + ((h >> 8) % (slotMs - 750UL));
        const uint32_t age = nowMs - eventAt;
        uint32_t blinkAge = 0xFFFFFFFFUL;
        if (age < duration) blinkAge = age;
        else if ((h & 0x00070000UL) == 0x00030000UL) {
            
            const uint32_t doubleAge = age - (uint32_t)(duration + (sleepy ? 260U : 105U));
            if (doubleAge < duration) blinkAge = doubleAge;
        }
        if (blinkAge < duration) {
            const uint32_t half = duration / 2U;
            if (blinkAge < half) return (uint8_t)(10U - (8UL * blinkAge) / half);
            return (uint8_t)(2U + (8UL * (blinkAge - half)) / (duration - half));
        }
    }
    return 10;
}

static inline bool nodeBotWinkNow(uint32_t nowMs) {
    const uint32_t bucket = nowMs / 23000UL;
    const uint32_t h = hash(bucket * 131UL + 0x71a9UL);
    const uint32_t start = bucket * 23000UL + 7000UL + ((h >> 8) % 9000UL);
    const uint32_t age = nowMs - start;
    return (h % 5UL) == 0UL && age < 320UL;
}

static inline int nodeBotBreathOffset(uint32_t nowMs) {
    
    const uint32_t phase = nowMs % 5200UL;
    uint32_t amplitude = 0;
    if (phase < 1300UL) {
        amplitude = (phase * 255UL) / 1300UL;
        return amplitude >= 160UL ? 1 : 0;
    }
    if (phase < 2600UL) {
        amplitude = ((2600UL - phase) * 255UL) / 1300UL;
        return amplitude >= 160UL ? 1 : 0;
    }
    if (phase < 3900UL) {
        amplitude = ((phase - 2600UL) * 255UL) / 1300UL;
        return amplitude >= 160UL ? -1 : 0;
    }
    amplitude = ((5200UL - phase) * 255UL) / 1300UL;
    return amplitude >= 160UL ? -1 : 0;
}

static inline void nodeBotDrawEye(U8G2& o, int x, int y, int aperture,
                           int gazeX, int gazeY, int browLift, bool surprised,
                           bool sleepyLid) {
    const int browY = y - 4 - browLift;
    
    if (surprised) {
        o.drawLine(x + 1, browY + 1, x + 5, browY);
        o.drawLine(x + 6, browY, x + 10, browY + 1);
    } else {
        o.drawLine(x + 1, browY + 2, x + 5, browY);
        o.drawLine(x + 6, browY, x + 10, browY + 2);
    }

    
    
    
    
    if (sleepyLid) {
        const int droop = aperture >= 6 ? 1 : 0;
        o.drawLine(x + 2, y + 4 + droop, x + 4, y + 6);
        o.drawLine(x + 4, y + 6, x + 7, y + 6);
        o.drawLine(x + 7, y + 6, x + 9, y + 4 + droop);
        o.drawPixel(x + 1, y + 4 + droop);
        o.drawPixel(x + 10, y + 4 + droop);
        return;
    }

    if (aperture <= 2) {
        
        
        
        
        if (sleepyLid) {
            o.drawLine(x + 2, y + 4, x + 4, y + 6);
            o.drawLine(x + 4, y + 6, x + 7, y + 6);
            o.drawLine(x + 7, y + 6, x + 9, y + 4);
            o.drawPixel(x + 1, y + 4);
            o.drawPixel(x + 10, y + 4);
        } else {
            o.drawLine(x + 2, y + 5, x + 5, y + 4);
            o.drawLine(x + 5, y + 4, x + 8, y + 5);
        }
        return;
    }

    const int eyeHeight = aperture > 10 ? 10 : aperture;
    const int eyeTop = y + (10 - eyeHeight) / 2;
    o.drawRBox(x, eyeTop, 12, eyeHeight, 3);
    
    o.setDrawColor(0);
    int pupilX = x + 6 + gazeX;
    int pupilY = eyeTop + eyeHeight / 2 + gazeY;
    if (pupilX < x + 3) pupilX = x + 3;
    if (pupilX > x + 8) pupilX = x + 8;
    if (pupilY < eyeTop + 2) pupilY = eyeTop + 2;
    if (pupilY > eyeTop + eyeHeight - 2) pupilY = eyeTop + eyeHeight - 2;
    o.drawDisc(pupilX, pupilY, eyeHeight >= 8 ? 2 : 1);
    o.setDrawColor(1);
    if (eyeHeight >= 7) o.drawPixel(pupilX - 1, pupilY - 1);
}

static inline void nodeBotDrawBlush(U8G2& o, int x, int y, uint8_t level, uint32_t phase) {
    if (level == 0) return;
    
    static const uint8_t cheek[3] = {0x0A, 0x15, 0x0A};
    const uint32_t p = phase % 3600UL;
    const uint32_t triangle = p < 1800UL ? p : 3600UL - p;
    const uint8_t fade = (uint8_t)((triangle * 255UL) / 1800UL);
    uint8_t density = level;
    if (level == 1U) {
        if (fade < 72U) return;
    } else if (level == 2U) {
        if (fade < 72U) density = 1U;
    } else {
        density = fade < 72U ? 1U : (fade < 174U ? 2U : 3U);
    }
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < 5; ++col) {
            const bool marked = (cheek[row] & (1U << col)) != 0;
            const uint8_t gate = (uint8_t)((col + row * 2U) % 3U);
            if (marked && (density >= 3U || (density == 2U && gate != 1U) ||
                           (density == 1U && gate == 0U))) {
                o.drawPixel(x + col, y + row);
            }
        }
    }
}

static inline void nodeBotDrawMouth(U8G2& o, uint8_t style, int cx, int y) {
    switch (style) {
        case 1: 
            o.drawHLine(cx - 2, y + 1, 5);
            break;
        case 2: 
            o.drawCircle(cx, y + 1, 2);
            break;
        case 3: 
            o.drawLine(cx - 4, y, cx - 1, y + 2);
            o.drawLine(cx - 1, y + 2, cx + 3, y + 1);
            o.drawPixel(cx + 4, y);
            break;
        case 4: 
            o.drawLine(cx - 4, y + 1, cx - 2, y - 1);
            o.drawLine(cx - 2, y - 1, cx, y + 1);
            o.drawLine(cx, y + 1, cx + 2, y - 1);
            o.drawPixel(cx + 4, y);
            break;
        case 5: 
            o.drawLine(cx - 4, y, cx - 2, y + 3);
            o.drawHLine(cx - 2, y + 3, 5);
            o.drawLine(cx + 3, y + 3, cx + 4, y);
            o.drawPixel(cx + 1, y + 2);
            break;
        case 6: 
            o.drawLine(cx - 4, y + 2, cx - 2, y);
            o.drawLine(cx - 2, y, cx + 2, y);
            o.drawLine(cx + 2, y, cx + 4, y + 2);
            break;
        default: 
            o.drawLine(cx - 4, y, cx - 2, y + 2);
            o.drawLine(cx - 2, y + 2, cx + 2, y + 2);
            o.drawLine(cx + 2, y + 2, cx + 4, y);
            break;
    }
}

static inline void nodeBotDrawFloatingZs(U8G2& o, uint32_t nowMs, int bob) {
    
    
    const uint32_t phase = nowMs % 3600UL;
    const uint32_t ages[2] = {phase, (uint32_t)((phase + 1650UL) % 3600UL)};
    const int xs[2] = {5, 15};
    const char* symbols[2] = {"Z", "z"};
    o.setFont(NODEBOT_FONT_SMALL);
    for (uint8_t i = 0; i < 2; ++i) {
        const uint32_t age = ages[i];
        if (age >= 2600UL) continue;
        const int rise = (int)(age * 9UL / 2600UL);
        
        if (age < 1450UL || ((age / 150UL) & 1UL) == 0UL) {
            const int baseline = (i == 0 ? 19 : 22) + bob - rise;
            o.drawStr(xs[i], baseline, symbols[i]);
        }
    }
}

static inline void nodeBotDrawSparkle(U8G2& o, int cx, int cy, int r) {
    o.drawVLine(cx, cy - r, 2 * r + 1);
    o.drawHLine(cx - r, cy, 2 * r + 1);
    if (r >= 2) { o.drawPixel(cx - 1, cy - 1); o.drawPixel(cx + 1, cy + 1); }
}

static inline void nodeBotDrawReactionCue(U8G2& o, NodeBotReaction reaction, uint32_t nowMs, int bob) {
    
    
    const int pulse = (int)((nowMs / 110UL) & 1UL);
    switch (reaction) {
        case NODEBOT_REACTION_NOTIFICATION:
            o.drawFrame(47, 15, 9, 9);
            o.drawVLine(51, 17, 4); o.drawPixel(51, 23);
            break;
        case NODEBOT_REACTION_BLE_CONNECTED:
        case NODEBOT_REACTION_DATA_SYNCED:
            nodeBotDrawSparkle(o, 51, 20 + pulse, 2);
            o.drawPixel(45, 24 + bob);
            break;
        case NODEBOT_REACTION_BLE_DISCONNECTED:
            o.setFont(NODEBOT_FONT_SMALL); o.drawStr(48, 24, "?");
            o.drawHLine(45, 26, 2);
            break;
        case NODEBOT_REACTION_STEPS_UP:
            o.drawHLine(2, 58 + bob, 4);
            o.drawHLine(48, 58 + bob, 5);
            o.drawLine(49, 55 + bob, 52, 52 + bob);
            break;
        case NODEBOT_REACTION_GOAL_REACHED:
            
            o.drawPixel(24, 14); o.drawPixel(28, 14);
            o.drawPixel(23, 15); o.drawPixel(24, 16); o.drawPixel(28, 16); o.drawPixel(29, 15);
            o.drawHLine(24, 17, 5); o.drawPixel(25, 18); o.drawPixel(27, 18); o.drawPixel(26, 19);
            nodeBotDrawSparkle(o, 48, 18, 1 + pulse);
            break;
        case NODEBOT_REACTION_CALL_ENDED:
        case NODEBOT_REACTION_ALARM_DISMISSED:
        case NODEBOT_REACTION_TIMER_DISMISSED:
            o.drawFrame(47, 16, 9, 8);
            o.drawLine(49, 20, 51, 22); o.drawLine(51, 22, 54, 18);
            break;
        case NODEBOT_REACTION_MISSED_CALL:
            o.drawFrame(47, 15, 9, 9);
            o.drawVLine(51, 17, 4); o.drawPixel(51, 23);
            o.drawPixel(46, 18); o.drawPixel(57, 18);
            break;
        case NODEBOT_REACTION_WEATHER_CHANGED:
            o.drawCircle(50, 19, 3);
            o.drawLine(48, 23, 47, 25); o.drawLine(52, 23, 51, 25);
            o.drawPixel(49, 26);
            break;
        case NODEBOT_REACTION_DATA_STALE:
            o.setFont(NODEBOT_FONT_SMALL); o.drawStr(39, 24, "...");
            break;
        default:
            break;
    }
}

template <typename FrameData>
static inline void renderFace(U8G2& o, const FrameData& f) {
    const uint32_t now = f.nowMs;
    const NodeBotReaction reaction = controller().activeReaction(now);
    const uint32_t reactionUntil = controller().reactionUntilMs();
    const bool greeting = reaction == NODEBOT_REACTION_GREETING;
    const bool reactionActive = reaction != NODEBOT_REACTION_NONE &&
                                (int32_t)(reactionUntil - now) > 0;
    const bool eventConnected = reactionActive && reaction == NODEBOT_REACTION_BLE_CONNECTED;
    const bool eventSynced = reactionActive && reaction == NODEBOT_REACTION_DATA_SYNCED;
    const bool eventDisconnected = reactionActive && reaction == NODEBOT_REACTION_BLE_DISCONNECTED;
    const bool eventStale = reactionActive && reaction == NODEBOT_REACTION_DATA_STALE;
    const bool sleeping = isNight(f) && !greeting;
    const bool alarm = controller().alarmRinging();
    const bool confused = (!f.bleConnected || !f.phoneDataFresh) && !eventConnected && !eventSynced;
    const bool happy = controller().goalReached() || (reactionActive && reaction == NODEBOT_REACTION_GOAL_REACHED);
    const bool idleGestureAllowed = !sleeping && !alarm && !confused && !reactionActive && !greeting && !happy;
    const uint8_t idleCue = idleGestureAllowed ? nodeBotIdleCueAt(now) : 0U;
    const int bob = nodeBotBreathOffset(now) +
                    ((reactionActive && reaction == NODEBOT_REACTION_STEPS_UP && ((now / 140UL) & 1UL)) ? -1 : 0) +
                    (((idleCue == 2U || idleCue == 4U) && ((now / 120UL) & 1UL)) ? -1 : 0);
    const NodeBotMoodProfile mood = nodeBotMoodAt(now);
    const int glanceX = nodeBotSmoothGaze(now, 2100UL, 0x91a7UL, 2);
    const int glanceY = nodeBotSmoothGaze(now, 2700UL, 0x2b31UL, 1);
    const bool wink = greeting ? ((now / 310UL) % 7UL == 0UL) :
                      (!sleeping && !alarm && !confused && !happy && !reactionActive &&
                       (idleCue == 3U || nodeBotWinkNow(now)));

    
    chamferFrame(o, 4, 24 + bob, 46, 36, 7);
    o.drawFrame(0, 36 + bob, 6, 12);
    o.drawFrame(48, 36 + bob, 6, 12);
    o.drawLine(27, 24 + bob, 27, 20 + bob);
    o.drawHLine(23, 24 + bob, 8);
    const bool antennaPulse = ((now / 360UL) % 2UL) == 0UL;
    if (antennaPulse || alarm) o.drawDisc(27, 18 + bob, 2);
    else o.drawCircle(27, 18 + bob, 2);

    uint8_t open = nodeBotBlinkOpen(now, sleeping);
    if (sleeping) {
        const uint32_t sleepyPhase = now % 6200UL;
        const uint32_t half = 3100UL;
        const uint32_t triangle = sleepyPhase < half ? sleepyPhase : 6200UL - sleepyPhase;
        const int drowse = 4 + (int)(triangle * 2UL / half); 
        if (open > drowse) open = (uint8_t)drowse;
    }
    const int idleLidDrop = mood.lidDrop;
    const int normalOpen = (int)open - idleLidDrop;
    open = (uint8_t)(normalOpen < 2 ? 2 : normalOpen > 10 ? 10 : normalOpen);

    int gazeX = glanceX + mood.gazeX;
    int gazeY = glanceY + mood.gazeY;
    if (sleeping) { gazeX = 0; gazeY = 2; }
    if (happy) { gazeX = 0; gazeY = 0; }
    if (confused) gazeX = (glanceX >= 0 ? 2 : -2);

    int leftOpen = open;
    int rightOpen = wink ? 2 : open;
    int browLift = mood.browLift;
    const bool surprised = alarm;
    
    if (alarm) {
        leftOpen = rightOpen = 10;
        browLift = 4;
        gazeX = 0;
        gazeY = 0;
    } else if (greeting) {
        leftOpen = rightOpen = 8;
        browLift = 3;
        gazeX = 0;
        gazeY = 0;
    } else if (sleeping) {
        leftOpen = rightOpen = open;
        browLift = -1;
        gazeX = 0;
        gazeY = 2;
    } else if (reactionActive && (eventConnected || eventSynced)) {
        leftOpen = rightOpen = 9; browLift = 3; gazeX = gazeY = 0;
    } else if (reactionActive && reaction == NODEBOT_REACTION_NOTIFICATION) {
        leftOpen = rightOpen = 9; browLift = 4; gazeX = 1; gazeY = -1;
    } else if (reactionActive && (eventDisconnected || eventStale)) {
        leftOpen = 7; rightOpen = 5; browLift = -2; gazeX = -2; gazeY = 1;
    } else if (reactionActive && reaction == NODEBOT_REACTION_STEPS_UP) {
        leftOpen = rightOpen = 9; browLift = 3; gazeX = 0; gazeY = 0;
    } else if (reactionActive && reaction == NODEBOT_REACTION_MISSED_CALL) {
        leftOpen = rightOpen = 9; browLift = 4; gazeX = 0; gazeY = 0;
    } else if (reactionActive && reaction == NODEBOT_REACTION_WEATHER_CHANGED) {
        leftOpen = 8; rightOpen = 10; browLift = 3; gazeX = 1; gazeY = -1;
    } else if (reactionActive && (reaction == NODEBOT_REACTION_CALL_ENDED ||
                                  reaction == NODEBOT_REACTION_ALARM_DISMISSED ||
                                  reaction == NODEBOT_REACTION_TIMER_DISMISSED)) {
        leftOpen = rightOpen = 7; browLift = 2; gazeX = 0; gazeY = 0;
    } else if (confused) {
        leftOpen = (open > 8 ? 8 : open);
        rightOpen = (open > 7 ? 7 : open);
        browLift = 1;
        gazeX = (glanceX >= 0 ? 2 : -2);
    } else if (happy) {
        leftOpen = rightOpen = (open > 7 ? 7 : open);
        browLift = 2;
        gazeX = 0;
        gazeY = 0;
    } else if (idleCue == 1U) {
        leftOpen = 9; rightOpen = 8; browLift = 4; gazeX = 2; gazeY = -1;
    } else if (idleCue == 4U) {
        
        
        leftOpen = rightOpen = 2; browLift = 1; gazeX = gazeY = 0;
    }

    nodeBotDrawEye(o, 10, 34 + bob, leftOpen, gazeX, gazeY, browLift, surprised, sleeping);
    nodeBotDrawEye(o, 31, 34 + bob, rightOpen, gazeX, gazeY, browLift - (wink ? 1 : 0), surprised, sleeping);

    uint8_t blush = mood.blush;
    if (greeting || (reactionActive && (reaction == NODEBOT_REACTION_NOTIFICATION ||
        reaction == NODEBOT_REACTION_GOAL_REACHED || reaction == NODEBOT_REACTION_STEPS_UP))) blush = 3U;
    if (sleeping && blush > 1U) blush = 1U;
    if (confused && blush > 1U) blush = 1U;
    if (happy && !confused && blush < 2U) blush = 2U;
    if (eventConnected || eventSynced) blush = 2U;
    if (alarm || eventDisconnected || (reactionActive && reaction == NODEBOT_REACTION_MISSED_CALL)) blush = 0;
    nodeBotDrawBlush(o, 10, 47 + bob, blush, now);
    nodeBotDrawBlush(o, 37, 47 + bob, blush, now + 450UL);

    uint8_t mouth = mood.mouth;
    if (idleCue == 4U) mouth = 5;
    else if (alarm || (reactionActive && (reaction == NODEBOT_REACTION_NOTIFICATION ||
        reaction == NODEBOT_REACTION_MISSED_CALL || reaction == NODEBOT_REACTION_WEATHER_CHANGED))) mouth = 2;
    else if (greeting) mouth = 5;
    else if (sleeping) mouth = 1;
    else if (eventDisconnected || confused) mouth = 4;
    else if (reactionActive && (eventConnected || eventSynced ||
             reaction == NODEBOT_REACTION_STEPS_UP || reaction == NODEBOT_REACTION_GOAL_REACHED ||
             reaction == NODEBOT_REACTION_TIMER_DISMISSED)) mouth = 5;
    else if (reactionActive && reaction == NODEBOT_REACTION_CALL_ENDED) mouth = 3;
    else if (happy) mouth = 5;
    nodeBotDrawMouth(o, mouth, 27, 49 + bob);

    
    if (sleeping && !alarm) nodeBotDrawFloatingZs(o, now, bob);
    else if (greeting && !alarm) {
        
        
        o.setFont(NODEBOT_FONT_SMALL);
        o.drawFrame(2, 5, 22, 14);
        o.drawStr(7, 15, "Hi!");
        o.drawLine(15, 19, 19, 22);
        const uint32_t wave = now % 620UL;
        const int handLift = wave < 310UL ? (int)(wave * 2UL / 310UL) : 2 - (int)((wave - 310UL) * 2UL / 310UL);
        o.drawLine(47, 44 + bob, 51, 37 + bob - handLift);
        o.drawLine(51, 37 + bob - handLift, 52, 33 + bob - handLift);
        o.drawVLine(53, 30 + bob - handLift, 5);
        o.drawVLine(55, 31 + bob - handLift, 4);
        o.drawHLine(52, 34 + bob - handLift, 4);
    } else if (happy || (reactionActive && reaction == NODEBOT_REACTION_GOAL_REACHED)) {
        
        const int heartY = 27 + bob - ((now / 220UL) & 1UL);
        o.drawPixel(25, heartY); o.drawPixel(29, heartY);
        o.drawPixel(24, heartY + 1); o.drawPixel(30, heartY + 1);
        o.drawHLine(25, heartY + 2, 5);
        o.drawLine(25, heartY + 3, 27, heartY + 5);
        o.drawLine(29, heartY + 3, 27, heartY + 5);
    } else if (idleCue == 1U) {
        o.setFont(NODEBOT_FONT_SMALL); o.drawStr(49, 23, "?");
    } else if (idleCue == 2U) {
        nodeBotDrawSparkle(o, 49, 20 + bob, 2);
        o.drawPixel(55, 24 + bob);
    }

    if (reactionActive && !alarm && !sleeping)
        nodeBotDrawReactionCue(o, reaction, now, bob);

    if (confused && !reactionActive && ((now / 170UL) % 5UL) == 0UL) {
        const uint32_t noise = hash(now / 130UL + 0x1f3UL);
        o.drawPixel(5 + (int)(noise % 45UL), 29 + (int)((noise >> 8) % 25UL) + bob);
    }

    
    drawStatusIcons(o, f);

    const char* safeTime = (f.time && f.time[0]) ? f.time : "--:--";
    
    
    
    
    
    const int timeLeft = 59;
    const int timeRight = 125;
    const int timeMaxWidth = timeRight - timeLeft;
    o.setFont(u8g2_font_logisoso18_tn);
    int timeWidth = o.getStrWidth(safeTime);
    if (timeWidth > timeMaxWidth) {
        o.setFont(NODEBOT_FONT_BODY);
        timeWidth = o.getStrWidth(safeTime);
    }
    int timeX = timeLeft + (timeMaxWidth - timeWidth) / 2;
    if (timeX < timeLeft) timeX = timeLeft;
    if (timeX + timeWidth > timeRight) timeX = timeRight - timeWidth;
    o.drawStr(timeX, 29, safeTime);

    char dow[8], day[8], mon[8], dateShort[20];
    splitDate(f, dow, day, mon);
    if (dow[0] && day[0] && mon[0]) snprintf(dateShort, sizeof(dateShort), "%.3s %.2s %.3s", dow, day, mon);
    else snprintf(dateShort, sizeof(dateShort), "-- -- ---");
    o.setFont(NODEBOT_FONT_SMALL);
    o.drawStr(59, 42, dateShort);

    char wx[20], steps[16];
    weatherText(f, wx, sizeof(wx));
    stepsText(f, steps, sizeof(steps));
    o.drawUTF8(59, 52, wx);
    o.drawStr(59, 63, steps);
}


template <typename FrameData>
inline void render(U8G2& display, const FrameData& frame) {
    renderFace(display, frame);
}

} 

#undef NODEBOT_FONT_BODY
#undef NODEBOT_FONT_SMALL
#endif 
