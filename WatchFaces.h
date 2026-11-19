#ifndef WATCH_FACES_H
#define WATCH_FACES_H

#include <Arduino.h>
#include <stdint.h>
#include <U8g2lib.h>
#include "CyberNodeTypes.h"

struct HomeFaceData {
    const char* time;
    const char* date;
    int temperature;
    const char* weather;
    int steps;
    bool hasCentralData;
    bool bleConnected;
    bool phoneDataFresh;
    int64_t epochSeconds;
    uint32_t nowMs;
    int batteryPercent;       
};

uint8_t watchFaceCount();
uint16_t watchFaceRefreshIntervalMs(uint8_t face);
const char* watchFaceName(uint8_t face);
void renderWatchFace(U8G2& display, uint8_t face, const HomeFaceData& data,
                     bool minuteEffect, bool pickerActive, bool pickerShowName);

#endif 
