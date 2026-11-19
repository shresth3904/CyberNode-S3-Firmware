#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace CyberNodeClock {

static inline bool isLeapYear(int year) {
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

static inline int daysInMonth(int year, int month) {
    static const uint8_t lengths[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && isLeapYear(year)) return 29;
    return lengths[month - 1];
}

static inline bool validDate(int year, int month, int day) {
    return year >= 1970 && year <= 2100 && month >= 1 && month <= 12 &&
           day >= 1 && day <= daysInMonth(year, month);
}

static inline int monthFromAbbreviation(const char* month) {
    static const char* names[12] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    if (month == nullptr || strlen(month) != 3) return 0;
    char upper[4] = {
        (char)((month[0] >= 'a' && month[0] <= 'z') ? month[0] - 32 : month[0]),
        (char)((month[1] >= 'a' && month[1] <= 'z') ? month[1] - 32 : month[1]),
        (char)((month[2] >= 'a' && month[2] <= 'z') ? month[2] - 32 : month[2]),
        '\0'
    };
    for (int i = 0; i < 12; ++i) if (strcmp(upper, names[i]) == 0) return i + 1;
    return 0;
}


static inline bool parseDate(const char* text, int& year, int& month, int& day) {
    if (text == nullptr || text[0] == '\0') return false;
    int y = 0, m = 0, d = 0;
    char weekday[4] = {}, monthText[4] = {};
    if (sscanf(text, "%3s %d %3s %d", weekday, &d, monthText, &y) == 4) {
        m = monthFromAbbreviation(monthText);
    } else if (sscanf(text, "%d %3s %d", &d, monthText, &y) == 3) {
        m = monthFromAbbreviation(monthText);
    } else if (sscanf(text, "%d-%d-%d", &y, &m, &d) == 3) {
        
    } else {
        return false;
    }
    if (!validDate(y, m, d)) return false;
    year = y;
    month = m;
    day = d;
    return true;
}


static inline int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = (unsigned)(year - era * 400);
    const unsigned dayOfYear = (153U * (month + (month > 2 ? (unsigned)-3 : 9U)) + 2U) / 5U + day - 1U;
    const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
    return (int64_t)era * 146097LL + (int64_t)dayOfEra - 719468LL;
}

static inline void civilFromDays(int64_t days, int& year, int& month, int& day) {
    int64_t z = days + 719468LL;
    const int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    const unsigned dayOfEra = (unsigned)(z - era * 146097LL);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460U + dayOfEra / 36524U - dayOfEra / 146096U) / 365U;
    int y = (int)yearOfEra + (int)(era * 400LL);
    const unsigned dayOfYear = dayOfEra - (365U * yearOfEra + yearOfEra / 4U - yearOfEra / 100U);
    const unsigned monthPrime = (5U * dayOfYear + 2U) / 153U;
    const unsigned d = dayOfYear - (153U * monthPrime + 2U) / 5U + 1U;
    const unsigned m = monthPrime + (monthPrime < 10U ? 3U : (unsigned)-9);
    y += m <= 2U;
    year = y;
    month = (int)m;
    day = (int)d;
}

static inline bool parseHHMM(const char* text, int& hour, int& minute) {
    if (text == nullptr || strlen(text) != 5 || text[2] != ':' ||
        text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' ||
        text[3] < '0' || text[3] > '9' || text[4] < '0' || text[4] > '9') return false;
    hour = (text[0] - '0') * 10 + text[1] - '0';
    minute = (text[3] - '0') * 10 + text[4] - '0';
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

static inline bool epochFromLocalDateTime(int year, int month, int day,
                                           int hour, int minute, int offsetMinutes,
                                           int64_t& epochOut) {
    if (!validDate(year, month, day) || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || offsetMinutes < -720 || offsetMinutes > 840) return false;
    const int64_t localSeconds = daysFromCivil(year, (unsigned)month, (unsigned)day) * 86400LL +
                                 (int64_t)hour * 3600LL + (int64_t)minute * 60LL;
    const int64_t candidate = localSeconds - (int64_t)offsetMinutes * 60LL;
    if (candidate < 0 || candidate > 4102444800LL) return false;
    epochOut = candidate;
    return true;
}

static inline bool localDateAndSecondsOfDay(int64_t epoch, int offsetMinutes,
                                             int& year, int& month, int& day,
                                             int& secondsOfDay) {
    if (epoch < 0 || epoch > 4102444800LL || offsetMinutes < -720 || offsetMinutes > 840) return false;
    const int64_t localSeconds = epoch + (int64_t)offsetMinutes * 60LL;
    int64_t days = localSeconds / 86400LL;
    int64_t remainder = localSeconds % 86400LL;
    if (remainder < 0) { --days; remainder += 86400LL; }
    civilFromDays(days, year, month, day);
    secondsOfDay = (int)remainder;
    return validDate(year, month, day);
}






static inline bool resolveEpoch(const char* incomingDate, bool dateProvided,
                                const char* incomingTime, bool timeProvided,
                                int64_t incomingEpoch, bool epochProvided,
                                int offsetMinutes,
                                int64_t previousEpoch, const char* previousDate,
                                const char* previousTime,
                                int64_t& resolvedEpoch, bool& epochCorrected) {
    epochCorrected = false;
    if (epochProvided && !timeProvided && !dateProvided) {
        if (incomingEpoch < 0 || incomingEpoch > 4102444800LL) return false;
        resolvedEpoch = incomingEpoch;
        return true;
    }

    int hour = 0, minute = 0;
    const char* timeText = timeProvided ? incomingTime : previousTime;
    if (!parseHHMM(timeText, hour, minute)) {
        if (epochProvided) { resolvedEpoch = incomingEpoch; return true; }
        if (previousEpoch > 0) { resolvedEpoch = previousEpoch; return true; }
        return false;
    }

    int year = 0, month = 0, day = 0;
    bool haveDate = dateProvided && parseDate(incomingDate, year, month, day);
    bool useReferenceDate = false;
    int secondsOfDay = hour * 3600 + minute * 60;

    if (!haveDate && epochProvided) {
        int ignoredSeconds = 0;
        haveDate = localDateAndSecondsOfDay(incomingEpoch, offsetMinutes,
                                            year, month, day, ignoredSeconds);
    } else if (!haveDate && previousEpoch > 0) {
        int previousSeconds = 0;
        haveDate = localDateAndSecondsOfDay(previousEpoch, offsetMinutes,
                                            year, month, day, previousSeconds);
        useReferenceDate = haveDate && timeProvided;
        if (useReferenceDate) {
            const int previousTod = previousSeconds;
            const int delta = secondsOfDay - previousTod;
            int64_t dayNumber = daysFromCivil(year, (unsigned)month, (unsigned)day);
            
            
            if (delta < -43200) ++dayNumber;
            else if (delta > 43200) --dayNumber;
            civilFromDays(dayNumber, year, month, day);
        }
    } else if (!haveDate) {
        haveDate = parseDate(previousDate, year, month, day);
        useReferenceDate = haveDate && timeProvided;
        if (useReferenceDate) {
            int oldHour = 0, oldMinute = 0;
            if (parseHHMM(previousTime, oldHour, oldMinute)) {
                const int delta = secondsOfDay - (oldHour * 3600 + oldMinute * 60);
                int64_t dayNumber = daysFromCivil(year, (unsigned)month, (unsigned)day);
                if (delta < -43200) ++dayNumber;
                else if (delta > 43200) --dayNumber;
                civilFromDays(dayNumber, year, month, day);
            }
        }
    }

    if (!haveDate) {
        
        
        if (epochProvided) { resolvedEpoch = incomingEpoch; return true; }
        return false;
    }

    int64_t reconstructed = 0;
    if (!epochFromLocalDateTime(year, month, day, hour, minute, offsetMinutes, reconstructed)) return false;
    if (epochProvided) {
        int64_t difference = incomingEpoch - reconstructed;
        if (difference < 0) difference = -difference;
        if (difference <= 59LL) {
            resolvedEpoch = incomingEpoch; 
        } else {
            resolvedEpoch = reconstructed;
            epochCorrected = true;
        }
    } else {
        resolvedEpoch = reconstructed;
    }
    return true;
}

} 
