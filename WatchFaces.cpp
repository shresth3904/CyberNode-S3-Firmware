#include "WatchFaces.h"
#include "NodeBot.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define CY_FONT_BODY  u8g2_font_7x13B_tf
#define CY_FONT_SMALL u8g2_font_6x12_tf
#define CY_FONT_MIN   u8g2_font_6x10_tf

static void drawDashedHLine(U8G2& oled, int x, int y, int width,
                            uint8_t dashLength = 2, uint8_t gapLength = 3) {
    if (width <= 0 || dashLength == 0) return;
    const int period = (int)dashLength + (int)gapLength;
    for (int offset = 0; offset < width; offset += period) {
        const int segment = (offset + dashLength > width) ? width - offset : dashLength;
        if (segment > 0) oled.drawHLine(x + offset, y, segment);
    }
}

static void cyChamferBox(U8G2& o, int x, int y, int w, int h, int c) {
    for (int i = 0; i <= c; ++i) {
        const int inset = c - i;
        o.drawHLine(x + inset, y + i, w - 2 * inset);
        o.drawHLine(x + inset, y + h - 1 - i, w - 2 * inset);
    }
    if (h - 2 * (c + 1) > 0) o.drawBox(x, y + c + 1, w, h - 2 * (c + 1));
}

static void cyChamferFrame(U8G2& o, int x, int y, int w, int h, int c) {
    o.drawHLine(x + c, y, w - 2 * c); o.drawHLine(x + c, y + h - 1, w - 2 * c);
    o.drawVLine(x, y + c, h - 2 * c); o.drawVLine(x + w - 1, y + c, h - 2 * c);
    o.drawLine(x, y + c, x + c, y); o.drawLine(x + w - 1 - c, y, x + w - 1, y + c);
    o.drawLine(x, y + h - 1 - c, x + c, y + h - 1);
    o.drawLine(x + w - 1 - c, y + h - 1, x + w - 1, y + h - 1 - c);
}

static const char* const HOME_FACE_NAMES[] = {
    "NODE-BOT", "GLITCH", "DATA RAIN", "HUD", "NEON CITY", "TERMINAL", "CLASSIC"
};
static constexpr uint8_t HOME_FACE_COUNT = 7;
uint8_t watchFaceCount() { return HOME_FACE_COUNT; }
uint16_t watchFaceRefreshIntervalMs(uint8_t face) {
    if (face == 0) return NodeBot::kRefreshIntervalMs;
    if (face == 6) return 1000;
    return 80;
}
const char* watchFaceName(uint8_t face) { return HOME_FACE_NAMES[face < HOME_FACE_COUNT ? face : 0]; }





static bool homeIsDigit(char c) { return c >= '0' && c <= '9'; }
static bool homeParseTime(const char* t, int& h, int& m) {
    if (!t || strlen(t) != 5 || t[2] != ':' || !homeIsDigit(t[0]) || !homeIsDigit(t[1]) ||
        !homeIsDigit(t[3]) || !homeIsDigit(t[4])) return false;
    h = (t[0] - '0') * 10 + (t[1] - '0');
    m = (t[3] - '0') * 10 + (t[4] - '0');
    return h <= 23 && m <= 59;
}

static uint32_t homeHash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dUL;
    x ^= x >> 15; x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}



static void homeDrawConnectionDot(U8G2& o, int cx, int cy, bool connected) {
    if (connected) o.drawDisc(cx, cy, 2);
    else o.drawCircle(cx, cy, 2);
}

static void homeDrawBattery(U8G2& o, int x, int y, int percent) {
    o.drawFrame(x, y, 11, 7);
    o.drawBox(x + 11, y + 2, 2, 3);
    if (percent < 0) {
        
        o.drawLine(x + 3, y + 4, x + 5, y + 2);
        return;
    }
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    const int fillWidth = (percent * 7 + 50) / 100;
    if (fillWidth > 0) o.drawBox(x + 2, y + 2, fillWidth, 3);
}

static void homeDrawStatusIcons(U8G2& o, const HomeFaceData& f) {
    homeDrawBattery(o, 105, 2, f.batteryPercent);
    homeDrawConnectionDot(o, 122, 5, f.bleConnected);
}

static bool homeWeatherAvailable(const HomeFaceData& f) {
    return f.hasCentralData && f.weather && f.weather[0] && strcmp(f.weather, "NO DATA") != 0 &&
           strcmp(f.weather, "N/A") != 0 && f.temperature >= -50 && f.temperature <= 80;
}

static void homeWeatherText(const HomeFaceData& f, char* out, size_t n) {
    if (!homeWeatherAvailable(f)) { snprintf(out, n, "WX --"); return; }
    char condition[7] = {0};
    uint8_t i = 0;
    while (f.weather[i] && f.weather[i] != ' ' && i < sizeof(condition) - 1) {
        condition[i] = f.weather[i]; ++i;
    }
    condition[i] = '\0';
    
    
    snprintf(out, n, "%d\xC2\xB0""C %.6s", f.temperature, condition);
}

static void homeDrawWeatherText(U8G2& oled, int x, int baseline, const char* text) {
    oled.drawUTF8(x, baseline, text ? text : "");
}

static int homeWeatherTextWidth(U8G2& oled, const char* text) {
    return oled.getUTF8Width(text ? text : "");
}


static void homeStepsText(const HomeFaceData& f, char* out, size_t n) {
    if (!f.hasCentralData || f.steps < 0) { snprintf(out, n, "--"); return; }
    char raw[16];
    snprintf(raw, sizeof(raw), "%ld", (long)f.steps);
    const size_t len = strlen(raw);
    size_t p = 0;
    for (size_t i = 0; i < len && p + 2 < n; ++i) {
        if (i > 0 && ((len - i) % 3) == 0) out[p++] = ',';
        out[p++] = raw[i];
    }
    out[p] = '\0';
}

static void homeDrawHeader(U8G2& o, const HomeFaceData& f, bool compact = false) {
    o.setFont(CY_FONT_SMALL);
    char dateBuf[20];
    snprintf(dateBuf, sizeof(dateBuf), "%.11s", (f.date && f.date[0]) ? f.date : "NOT SYNCED");
    o.drawStr(3, 10, dateBuf);
    homeDrawStatusIcons(o, f);
    if (!compact) drawDashedHLine(o, 3, 13, 122, 3, 2);
}

static bool homeIsNight(const HomeFaceData& f) {
    int h = 0, m = 0;
    return homeParseTime(f.time, h, m) && (h >= 23 || h < 6);
}


static void drawFaceClassic(U8G2& o, const HomeFaceData& f) {
    homeDrawHeader(o, f);
    const char* safeTime = (f.time && f.time[0]) ? f.time : "--:--";
    o.setFont(u8g2_font_logisoso28_tn);
    o.drawStr((128 - o.getStrWidth(safeTime)) / 2, 45, safeTime);
    drawDashedHLine(o, 3, 50, 122, 3, 2);
    o.setFont(CY_FONT_SMALL);
    char wx[20], steps[16]; homeWeatherText(f, wx, sizeof(wx)); homeStepsText(f, steps, sizeof(steps));
    homeDrawWeatherText(o, 3, 62, wx);
    o.drawStr(125 - o.getStrWidth(steps), 62, steps);
}





static void drawFaceGlitch(U8G2& o, const HomeFaceData& f, bool minuteEffect) {
    homeDrawHeader(o, f, true);
    cyChamferFrame(o, 3, 15, 122, 34, 5);
    const char* safeTime = (f.time && f.time[0]) ? f.time : "--:--";
    o.setFont(u8g2_font_logisoso28_tn);
    const int w = o.getStrWidth(safeTime);
    const int x = (128 - w) / 2;
    
    const uint32_t burst = homeHash(f.nowMs / 1900UL + 0x91UL) % 17UL;
    const bool glitch = burst == 0 && (f.nowMs % 1900UL) < 150UL;
    if (glitch || minuteEffect) {
        o.setDrawColor(2);
        o.drawStr(x + ((f.nowMs / 40UL) % 2UL ? 1 : -1), 44, safeTime);
        o.setDrawColor(1);
    }
    o.drawStr(x, 44, safeTime);
    
    if (glitch || minuteEffect) {
        o.drawHLine(7, 25 + (int)((f.nowMs / 80UL) % 12UL), 7);
        o.drawHLine(114, 31 - (int)((f.nowMs / 80UL) % 9UL), 7);
    }
    for (int i = 0; i < 4; ++i) {
        const int sx = 8 + i * 4;
        if (i < 2) o.drawHLine(sx, 18, 2); else o.drawHLine(112 + (i - 2) * 3, 45, 2);
    }
    o.setFont(CY_FONT_SMALL);
    char wx[20], steps[16]; homeWeatherText(f, wx, sizeof(wx)); homeStepsText(f, steps, sizeof(steps));
    homeDrawWeatherText(o, 3, 62, wx); o.drawStr(125 - o.getStrWidth(steps), 62, steps);
    if (glitch) o.drawFrame(1, 16, 3, 5);
}


static void drawFaceDataRain(U8G2& o, const HomeFaceData& f) {
    homeDrawHeader(o, f, true);
    static const char glyphs[] = "0123456789ABCDEF";
    o.setFont(CY_FONT_SMALL);
    const uint32_t tick = f.nowMs / 180UL;
    const int columns[] = {5, 13, 114, 122};
    for (uint8_t c = 0; c < 4; ++c) {
        const uint32_t seed = homeHash((uint32_t)c * 1777UL + tick / (1UL + c));
        for (uint8_t r = 0; r < 3; ++r) {
            const int y = 22 + (int)((seed >> (r * 5)) % 30UL);
            char g[2] = {glyphs[(seed >> (r * 4 + 1)) & 15U], '\0'};
            if (y < 18 || y > 50) continue;
            o.drawStr(columns[c], y, g);
        }
    }
    
    for (int i = 0; i < 8; ++i) {
        const uint32_t h = homeHash(tick + (uint32_t)i * 41UL);
        const int px = 4 + (int)(h % 120UL);
        if (px < 16 || px > 111) o.drawPixel(px, 16 + (int)((h >> 9) % 34UL));
    }
    cyChamferBox(o, 17, 20, 94, 28, 4);
    o.setDrawColor(0);
    const char* safeTime = (f.time && f.time[0]) ? f.time : "--:--";
    o.setFont(u8g2_font_logisoso24_tn);
    o.drawStr((128 - o.getStrWidth(safeTime)) / 2, 42, safeTime);
    o.setDrawColor(1);
    o.setFont(CY_FONT_SMALL);
    char wx[20], steps[16]; homeWeatherText(f, wx, sizeof(wx)); homeStepsText(f, steps, sizeof(steps));
    homeDrawWeatherText(o, 3, 62, wx); o.drawStr(125 - o.getStrWidth(steps), 62, steps);
}


static void drawFaceHud(U8G2& o, const HomeFaceData& f) {
    homeDrawHeader(o, f, true);
    const int cx = 16, cy = 34;
    o.drawCircle(cx, cy, 14); o.drawCircle(cx, cy, 10);
    for (int i = 0; i < 12; ++i) {
        const float a = (float)i * 6.2831853f / 12.0f;
        const int r0 = (i % 3 == 0) ? 10 : 12;
        o.drawLine(cx + (int)lroundf(r0 * sinf(a)), cy - (int)lroundf(r0 * cosf(a)),
                   cx + (int)lroundf(14 * sinf(a)), cy - (int)lroundf(14 * cosf(a)));
    }
    int second = f.epochSeconds > 0 ? (int)(f.epochSeconds % 60LL) : (int)((f.nowMs / 1000UL) % 60UL);
    const float sa = (float)second * 6.2831853f / 60.0f;
    const int sx = cx + (int)lroundf(8 * sinf(sa));
    const int sy = cy - (int)lroundf(8 * cosf(sa));
    o.drawLine(cx, cy, sx, sy); o.drawDisc(cx, cy, 2);
    o.setFont(u8g2_font_logisoso20_tn);
    const char* safeTime = (f.time && f.time[0]) ? f.time : "--:--";
    o.drawStr(35, 40, safeTime);
    o.setFont(CY_FONT_SMALL);
    char wx[20], steps[16]; homeWeatherText(f, wx, sizeof(wx)); homeStepsText(f, steps, sizeof(steps));
    o.drawHLine(3, 51, 122);
    homeDrawWeatherText(o, 3, 63, wx);
    o.drawStr(125 - o.getStrWidth(steps), 63, steps);
    
    const int shift = (int)((f.nowMs / 500UL) % 8UL);
    o.drawHLine(108 + shift, 17, 13 - shift);
    o.drawVLine(121, 17, 4);
}


static void homeDrawBuilding(U8G2& o, int x, int baseY, int w, int h, uint32_t tick, int id, bool night) {
    const int top = baseY - h;
    o.drawFrame(x, top, w, h);
    for (int wy = top + 4; wy < baseY - 2; wy += 5) {
        for (int wx = x + 3; wx < x + w - 2; wx += 5) {
            const uint32_t seed = homeHash(tick / 4UL + (uint32_t)id * 97UL + (uint32_t)wx * 11UL + (uint32_t)wy * 3UL);
            if (night ? ((seed & 1UL) == 0) : ((seed & 3UL) == 0)) o.drawPixel(wx, wy);
        }
    }
}

static void drawFaceNeonCity(U8G2& o, const HomeFaceData& f) {
    homeDrawHeader(o, f, true);
    
    cyChamferFrame(o, 25, 16, 78, 30, 4);
    const char* safeTime = (f.time && f.time[0]) ? f.time : "--:--";
    o.setFont(u8g2_font_logisoso24_tn);
    o.drawStr(64 - o.getStrWidth(safeTime) / 2, 40, safeTime);
    const uint32_t tick = f.nowMs / 250UL;
    const bool night = homeIsNight(f);
    homeDrawBuilding(o, 1, 64, 9, 14, tick, 0, night);
    homeDrawBuilding(o, 11, 64, 10, 20, tick, 1, night);
    homeDrawBuilding(o, 105, 64, 9, 17, tick, 2, night);
    homeDrawBuilding(o, 115, 64, 12, 24, tick, 3, night);
    
    for (int i = 0; i < 5; ++i) {
        const uint32_t seed = homeHash(tick / 2UL + (uint32_t)i * 71UL);
        const int y = 19 + (int)((seed >> 7) % 24UL);
        const int x = (seed & 1UL) ? (5 + i * 3) : (108 + i * 3);
        o.drawLine(x, y, x - 2, y + 3);
    }
    o.drawHLine(0, 63, 128);
    
    o.setFont(CY_FONT_SMALL);
    char steps[16], tempOnly[12]; homeStepsText(f, steps, sizeof(steps));
    if (homeWeatherAvailable(f)) snprintf(tempOnly, sizeof(tempOnly), "%d\xC2\xB0""C", f.temperature);
    else snprintf(tempOnly, sizeof(tempOnly), "WX --");
    o.setDrawColor(0); o.drawBox(21, 48, 86, 15); o.setDrawColor(1);
    homeDrawWeatherText(o, 26, 60, tempOnly);
    o.drawStr(102 - o.getStrWidth(steps), 60, steps);
    
    if ((tick % 48UL) < 8UL) {
        const int carX = 25 + (int)((tick % 48UL) * 9UL);
        o.drawBox(carX, 45, 5, 2); o.drawPixel(carX + 1, 44); o.drawPixel(carX + 4, 44);
    }
}


static void drawFaceTerminal(U8G2& o, const HomeFaceData& f) {
    cyChamferFrame(o, 1, 1, 126, 62, 4);
    o.setDrawColor(1); cyChamferBox(o, 3, 3, 122, 12, 2);
    o.setDrawColor(0); o.setFont(CY_FONT_SMALL); o.drawStr(8, 13, "NODE:~");
    
    homeDrawStatusIcons(o, f);
    o.setDrawColor(1);
    o.setFont(CY_FONT_SMALL);
    char dateBuf[18]; snprintf(dateBuf, sizeof(dateBuf), "%.11s", (f.date && f.date[0]) ? f.date : "NOT SYNCED");
    o.drawStr(5, 26, dateBuf);
    o.setFont(u8g2_font_logisoso20_tn);
    const char* safeTime = (f.time && f.time[0]) ? f.time : "--:--";
    o.drawStr(4, 48, ">");
    o.drawStr(16, 48, safeTime);
    
    
    const uint8_t typed = (uint8_t)((f.nowMs / 240UL) % 7UL);
    const int typedWidth = (typed > 5 ? 5 : typed) * 12;
    if (typedWidth > 0) o.drawHLine(16, 49, typedWidth);
    if (typed < 6) o.drawFrame(16 + typedWidth, 45, 3, 4);
    else if ((f.nowMs / 430UL) % 2UL == 0) o.drawBox(18 + o.getStrWidth(safeTime), 45, 3, 4);
    o.setFont(CY_FONT_SMALL);
    char steps[16], wx[20], stepLine[24];
    homeStepsText(f, steps, sizeof(steps)); homeWeatherText(f, wx, sizeof(wx));
    snprintf(stepLine, sizeof(stepLine), "STP %s", steps);
    o.drawStr(5, 60, stepLine);
    homeDrawWeatherText(o, 126 - homeWeatherTextWidth(o, wx), 60, wx);
}

static void drawFacePickerOverlay(U8G2& o, uint8_t index, bool showName) {
    const int spacing = 8;
    const int startX = 64 - ((HOME_FACE_COUNT - 1) * spacing) / 2;
    o.setDrawColor(1);
    for (uint8_t i = 0; i < HOME_FACE_COUNT; ++i) {
        const int x = startX + i * spacing;
        if (i == index) o.drawHLine(x - 3, 0, 7); else o.drawPixel(x, 0);
    }
    if (!showName) return;
    char label[24];
    snprintf(label, sizeof(label), "%u/%u %s", (unsigned)(index + 1), (unsigned)HOME_FACE_COUNT, HOME_FACE_NAMES[index]);
    o.setFont(CY_FONT_BODY);
    const int w = o.getStrWidth(label) + 14;
    const int x = (128 - w) / 2;
    o.setDrawColor(0); o.drawBox(0, 47, 128, 17); o.setDrawColor(1);
    cyChamferBox(o, x, 49, w, 14, 3);
    o.setDrawColor(0); o.drawStr(x + 7, 60, label); o.setDrawColor(1);
}

void renderWatchFace(U8G2& display, uint8_t face, const HomeFaceData& data,
                     bool minuteEffect, bool pickerActive, bool pickerShowName) {
    display.clearBuffer();
    display.setDrawColor(1);
    switch (face) {
        case 0: NodeBot::render(display, data); break;
        case 1: drawFaceGlitch(display, data, minuteEffect); break;
        case 2: drawFaceDataRain(display, data); break;
        case 3: drawFaceHud(display, data); break;
        case 4: drawFaceNeonCity(display, data); break;
        case 5: drawFaceTerminal(display, data); break;
        default: drawFaceClassic(display, data); break;
    }
    if (pickerActive) drawFacePickerOverlay(display, face, pickerShowName);
}
