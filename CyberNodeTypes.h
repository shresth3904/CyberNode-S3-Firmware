#pragma once

#include <stdint.h>

static constexpr uint8_t WATCH_NOTIFICATION_MAX = 5;

struct WatchNotification {
    char id[20];
    char app[16];
    char title[32];
    char content[120];
    char timestamp[20];
};




struct WatchData {
    char time[6];                 
    char date[16];                
    int64_t epochSeconds;         
    int16_t timezoneOffsetMinutes; 

    int32_t steps;
    int16_t temperature;
    uint8_t humidity;
    char weather[16];

    int16_t aqi;
    char aqiStatus[12];           
    uint8_t precipitationChance;  
    char precipitation[16];       

    int32_t dailyGoal;
    uint8_t goalPercent;          
    int32_t stepHistory[7];       
    char stepHistoryDays[7][4];   

    uint8_t notificationCount;
    WatchNotification notifications[WATCH_NOTIFICATION_MAX];
};


enum TransitionDirection {
    TRANSITION_FROM_LEFT = 0,
    TRANSITION_FROM_RIGHT = 1,
    TRANSITION_FROM_CENTER = 2,
    TRANSITION_RADIAL_REVEAL = 3,
    TRANSITION_CAROUSEL_NEXT = 4,
    TRANSITION_CAROUSEL_PREVIOUS = 5,
    
    TRANSITION_RADIAL_REVEAL_REVERSE = 6
};
