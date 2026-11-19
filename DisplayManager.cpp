#include "DisplayManager.h"
#include "NavigationGeometry.h"
#include "WatchFaces.h"
#include "SuperiorLogo.h"
#include "SuperiorBootFrames.h"
#include "SettingsConfig.h"



#ifndef pgm_read_byte
#define pgm_read_byte(address) (*(const uint8_t *)(address))
#endif
#include <Wire.h>
#include <string.h>
#include <math.h>
#include <limits.h>


static void drawCenteredLabel(U8G2& oled, const char* label, int baseline, int xOffset = 0) {
    int x = (128 - oled.getStrWidth(label)) / 2 + xOffset;
    oled.drawStr(x, baseline, label);
}


static void drawDashedHLine(U8G2& oled, int x, int y, int width,
                            uint8_t dashLength = 2, uint8_t gapLength = 3) {
    if (width <= 0 || dashLength == 0) return;
    const int period = (int)dashLength + (int)gapLength;
    for (int offset = 0; offset < width; offset += period) {
        const int segment = (offset + dashLength > width) ? width - offset : dashLength;
        if (segment > 0) oled.drawHLine(x + offset, y, segment);
    }
}

static int32_t clampStepValue(int32_t value, int32_t maximum) {
    if (value < 0) return 0;
    if (value > maximum) return maximum;
    return value;
}

static void drawHistoryDayLabel(U8G2& oled, const WatchData& data, int index, int centerX, int baseline) {
    oled.setFont(u8g2_font_4x6_tf);
    const char* day = data.stepHistoryDays[index][0] ? data.stepHistoryDays[index] : "-";
    int width = oled.getStrWidth(day);
    oled.drawStr(centerX - width / 2, baseline, day);
}









#define CY_FONT_BODY  u8g2_font_7x13B_tf
#define CY_FONT_SMALL u8g2_font_6x12_tf
#define CY_FONT_MIN   u8g2_font_6x10_tf


static void cyChamferBox(U8G2& o, int x, int y, int w, int h, int c) {
    for (int i = 0; i <= c; ++i) {
        const int inset = c - i;
        o.drawHLine(x + inset, y + i, w - 2 * inset);
        o.drawHLine(x + inset, y + h - 1 - i, w - 2 * inset);
    }
    if (h - 2 * (c + 1) > 0) o.drawBox(x, y + c + 1, w, h - 2 * (c + 1));
}


static void cyChamferFrame(U8G2& o, int x, int y, int w, int h, int c) {
    o.drawHLine(x + c, y, w - 2 * c);
    o.drawHLine(x + c, y + h - 1, w - 2 * c);
    o.drawVLine(x, y + c, h - 2 * c);
    o.drawVLine(x + w - 1, y + c, h - 2 * c);
    o.drawLine(x, y + c, x + c, y);
    o.drawLine(x + w - 1 - c, y, x + w - 1, y + c);
    o.drawLine(x, y + h - 1 - c, x + c, y + h - 1);
    o.drawLine(x + w - 1 - c, y + h - 1, x + w - 1, y + h - 1 - c);
}


static void cyFit(U8G2& o, char* s, int maxWidth) {
    if (o.getStrWidth(s) <= maxWidth) return;
    size_t n = strlen(s);
    if (n >= 2) { n -= 2; s[n] = '\0'; }
    while (n > 0 && o.getStrWidth(s) + o.getStrWidth("..") > maxWidth) s[--n] = '\0';
    strcat(s, "..");
}


static void cyNode(U8G2& o, int cx, int cy, bool connected) {
    o.drawLine(cx - 4, cy - 4, cx, cy);
    o.drawLine(cx + 4, cy - 4, cx, cy);
    o.drawLine(cx - 4, cy + 4, cx, cy);
    o.drawLine(cx + 4, cy + 4, cx, cy);
    if (connected) o.drawDisc(cx, cy, 2);
    else o.drawCircle(cx, cy, 2);
}


static void cyHeader(U8G2& o, const char* title, const char* right) {
    o.setFont(CY_FONT_BODY);
    const int w = o.getStrWidth(title) + 14;
    o.setDrawColor(1);
    cyChamferBox(o, 0, 0, w, 14, 3);
    o.setDrawColor(0);
    o.drawStr(7, 11, title);
    o.setDrawColor(1);
    o.drawHLine(w, 13, 128 - w);
    if (right && right[0]) {
        o.setFont(CY_FONT_SMALL);
        o.drawStr(127 - o.getStrWidth(right), 10, right);
    }
}


static void cyChip(U8G2& o, int x, int y, int w, const char* text, bool active) {
    o.setFont(CY_FONT_BODY);
    o.setDrawColor(1);
    if (active) {
        cyChamferBox(o, x, y, w, 14, 3);
        o.setDrawColor(0);
    } else {
        cyChamferFrame(o, x, y, w, 14, 3);
    }
    o.drawStr(x + (w - o.getStrWidth(text)) / 2, y + 11, text);
    o.setDrawColor(1);
}


static void cyScroll(U8G2& o, int x, int y, int h, int index, int count) {
    if (count < 2) return;
    o.drawVLine(x + 1, y, h);
    int thumb = h / count;
    if (thumb < 6) thumb = 6;
    const int ty = y + (index * (h - thumb)) / (count - 1);
    o.drawBox(x, ty, 3, thumb);
}


static void cyHazard(U8G2& o, int y, int h, int phase) {
    o.drawHLine(0, y, 128);
    o.drawHLine(0, y + h - 1, 128);
    for (int x = -h; x < 128; x += 8)
        o.drawLine(x + phase, y + h - 1, x + phase + h - 1, y);
}


static bool cyExtractClock(const char* ts, char out[6]) {
    const char* p = ts ? strchr(ts, ':') : nullptr;
    if (!p || (p - ts) < 2 || !p[1] || !p[2]) return false;
    if (p[-2] < '0' || p[-2] > '9' || p[-1] < '0' || p[-1] > '9' ||
        p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9') return false;
    memcpy(out, p - 2, 5);
    out[5] = '\0';
    return true;
}



static void cyList(U8G2& o, const char* title, int sel, int count,
                   const char* const* names, const char* const* values, const bool* checks) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%02d/%02d", count ? sel + 1 : 0, count);
    cyHeader(o, title, counter);
    const int visible = 3;
    int first = sel - 1;
    if (first > count - visible) first = count - visible;
    if (first < 0) first = 0;
    for (int r = 0; r < visible; ++r) {
        const int idx = first + r;
        if (idx >= count) break;
        const int y = 16 + r * 16;
        const bool on = (idx == sel);
        o.setDrawColor(1);
        if (on) {
            cyChamferBox(o, 0, y, 122, 15, 3);
            o.setDrawColor(0);
            o.drawTriangle(3, y + 4, 3, y + 10, 7, y + 7);
        }
        o.setFont(CY_FONT_BODY);
        int reserve = 0;
        if (values && values[idx]) reserve = o.getStrWidth(values[idx]) + 6;
        if (checks) reserve = 12;
        char label[28];
        snprintf(label, sizeof(label), "%.22s", names[idx] ? names[idx] : "");
        cyFit(o, label, 122 - 10 - 4 - reserve);
        o.drawStr(10, y + 12, label);
        if (values && values[idx]) o.drawStr(118 - o.getStrWidth(values[idx]), y + 12, values[idx]);
        if (checks) {
            if (checks[idx]) o.drawBox(110, y + 3, 9, 9);
            else o.drawFrame(110, y + 3, 9, 9);
        }
        o.setDrawColor(1);
    }
    cyScroll(o, 124, 16, 47, sel, count);
}



static int cyWrap(const char* text, int width, int* starts, int* lens, int maxLines) {
    const int len = (int)strlen(text);
    int pos = 0, n = 0;
    while (pos < len && n < maxLines) {
        while (pos < len && text[pos] == ' ') ++pos;      
        if (pos >= len) break;
        int end = pos + width;
        if (end >= len) {
            end = len;
        } else {
            int brk = end;
            while (brk > pos && text[brk] != ' ') --brk;   
            if (brk > pos) end = brk;                      
        }
        starts[n] = pos;
        lens[n] = end - pos;
        ++n;
        pos = end;
    }
    if (n == 0) { starts[0] = 0; lens[0] = 0; n = 1; }
    return n;
}

DisplayManager::DisplayManager()
    : display(U8G2_R0, U8X8_PIN_NONE), presentationDeferred(false), displaySleeping_(false) {
}

void DisplayManager::setPresentationDeferred(bool deferred) {
    presentationDeferred = deferred;
}

void DisplayManager::present() {
    
    if (!presentationDeferred && !displaySleeping_) display.sendBuffer();
}

void DisplayManager::setDisplaySleep(bool sleeping) {
    if (displaySleeping_ == sleeping) return;

    if (sleeping) {
        
        displaySleeping_ = true;
        display.setPowerSave(1);
    } else {
        
        display.setPowerSave(0);
        displaySleeping_ = false;
        display.clearBuffer();
    }
}

bool DisplayManager::isDisplaySleeping() const {
    return displaySleeping_;
}

void DisplayManager::begin() {
    Wire.begin(8, 9);

    display.begin();
    
    
    Wire.setClock(400000);
    display.setContrast(160); 
}

void DisplayManager::clear() {
    display.clearBuffer();
    present();
}

void DisplayManager::showBootScreen() {
    showStartupAnimation();
}


static void drawSuperiorBootFrame(U8G2& oled, uint8_t frameIndex) {
    if (frameIndex >= SUPERIOR_BOOT_FRAME_COUNT) {
        frameIndex = SUPERIOR_BOOT_FRAME_COUNT - 1;
    }
    oled.clearBuffer();
    for (int y = 0; y < 64; ++y) {
        const int rowOffset = y * 16;
        for (int x = 0; x < 128; ++x) {
            const uint8_t bits = pgm_read_byte(
                &SUPERIOR_BOOT_TYPEWRITER[frameIndex][rowOffset + (x >> 3)]);
            if (bits & (uint8_t)(1U << (x & 7))) {
                oled.drawPixel(x, y);
            }
        }
    }
}



static void drawSuperiorBootUnderline(U8G2& oled, int revealWidth) {
    const int startX = 25;
    const int fullWidth = 79;
    const int y = 54;
    if (revealWidth < 0) revealWidth = 0;
    if (revealWidth > fullWidth) revealWidth = fullWidth;
    if (revealWidth > 0) {
        oled.drawHLine(startX, y, revealWidth);
        oled.drawHLine(startX, y + 1, revealWidth);
    }
}

static void drawSuperiorBootGlitchFrame(U8G2& oled, uint8_t frame) {
    oled.clearBuffer();
    for (int y = 0; y < 64; ++y) {
        int shift = 0;
        if ((y >= 18 && y <= 25) || (y >= 32 && y <= 39)) {
            const uint8_t pattern = (uint8_t)((frame + (y / 3)) % 3);
            shift = pattern == 0 ? -5 : (pattern == 1 ? 4 : 0);
        }
        const int rowOffset = y * 16;
        for (int x = 0; x < 128; ++x) {
            const uint8_t bits = pgm_read_byte(
                &SUPERIOR_BOOT_TYPEWRITER[8][rowOffset + (x >> 3)]);
            if (bits & (uint8_t)(1U << (x & 7))) {
                const int targetX = x + shift;
                if (targetX >= 0 && targetX < 128) oled.drawPixel(targetX, y);
            }
        }
    }

    
    
    const int scanY = 23 + ((frame * 7) % 16);
    oled.drawHLine((frame * 19) % 90, scanY, 24 + (frame % 3) * 8);
    for (uint8_t n = 0; n < 7; ++n) {
        const int nx = (frame * 23 + n * 17) % 128;
        const int ny = 8 + ((frame * 11 + n * 5) % 48);
        oled.drawPixel(nx, ny);
    }
}




void DisplayManager::showSuperiorBootLogo(uint8_t animationStyle) {
    if (animationStyle == CyberNodeSettings::BOOT_LOGO_ANIMATION_GLITCH) {
        for (uint8_t frame = 0; frame < 7; ++frame) {
            drawSuperiorBootGlitchFrame(display, frame);
            present();
            delay(45);
        }

        drawSuperiorBootFrame(display, 8);
        present();
        delay(80);

        for (int frame = 1; frame <= 9; ++frame) {
            drawSuperiorBootFrame(display, 8);
            const int width = 8 + (71 * (frame - 1)) / 8;
            drawSuperiorBootUnderline(display, width);
            present();
            delay(80);
        }
    } else {
        
        drawSuperiorBootFrame(display, 0);
        present();
        delay(80);

        
        
        for (uint8_t frame = 1; frame < SUPERIOR_BOOT_FRAME_COUNT; ++frame) {
            drawSuperiorBootFrame(display, frame);
            present();
            delay(80);
        }

        
        
        for (int frame = 1; frame <= 9; ++frame) {
            drawSuperiorBootFrame(display, 8);
            const int width = 8 + (71 * (frame - 1)) / 8;
            drawSuperiorBootUnderline(display, width);
            present();
            delay(80);
        }
    }

    
    drawSuperiorBootFrame(display, 8);
    drawSuperiorBootUnderline(display, 79);
    present();
    delay(2000);
}



void DisplayManager::showStartupAnimation() {
    
    display.clearBuffer();

    const char* word = "CYBERNODE";
    display.setFont(u8g2_font_helvB12_tf);
    int wordWidth = display.getStrWidth(word);
    int wordX = (128 - wordWidth) / 2;
    display.drawStr(wordX, 29, word);

    display.setFont(u8g2_font_6x10_tf);
    const char* model = "S3";
    int modelWidth = display.getStrWidth(model);
    int modelX = (128 - modelWidth) / 2;
    display.drawStr(modelX, 45, model);

    present();
    delay(2000);
}


U8G2& DisplayManager::bootDisplay() {
    return display;
}

void DisplayManager::presentBootFrame() {
    present();
}


uint8_t DisplayManager::homeFaceCount() { return watchFaceCount(); }
const char* DisplayManager::homeFaceName(uint8_t face) { return watchFaceName(face); }
void DisplayManager::setHomeFace(uint8_t face) { homeFace_ = face < homeFaceCount() ? face : 0; }
bool DisplayManager::homeFaceAnimated() const { return homeFace_ != 6; }

uint16_t DisplayManager::homeFaceRefreshIntervalMs() const {
    return watchFaceRefreshIntervalMs(homeFace_);
}
void DisplayManager::setFacePickerOverlay(bool active, bool showName) {
    facePickerActive_ = active;
    facePickerShowName_ = showName;
}

void DisplayManager::showHomeScreen(
    const char* time, const char* date, int temperature, const char* weather, int steps,
    bool hasCentralData, bool bleConnected, bool phoneDataFresh,
    int64_t epochSeconds, uint32_t nowMs, int batteryPercent
) {
    char minuteKey[6] = {0};
    if (time && strlen(time) == 5) memcpy(minuteKey, time, 6);
    if (lastHomeTimeValid_ && strcmp(minuteKey, lastHomeTime_) != 0)
        minuteEffectUntilMs_ = nowMs + 420UL;
    if (minuteKey[0]) { memcpy(lastHomeTime_, minuteKey, sizeof(lastHomeTime_)); lastHomeTimeValid_ = true; }
    const bool minuteEffect = (int32_t)(minuteEffectUntilMs_ - nowMs) > 0;
    const HomeFaceData data = {time, date, temperature, weather, steps, hasCentralData, bleConnected,
                               phoneDataFresh, epochSeconds, nowMs, batteryPercent};
    renderWatchFace(display, homeFace_, data, minuteEffect, facePickerActive_, facePickerShowName_);
    present();
}

void DisplayManager::showMenu(int selectedItem) {
    display.clearBuffer();
    static const char* const menuItems[] = {"Weather", "Step Count", "System Info", "Games",
                                            "Alerts", "Stopwatch", "Fortune Teller", "Settings", "Alarm", "Timer", "Navigation"};
    cyList(display, "MENU", selectedItem, 11, menuItems, nullptr, nullptr);
    present();
}

void DisplayManager::showWeatherScreen() {
  display.clearBuffer();

  display.setFont(u8g2_font_7x14B_tf);
  display.drawStr(0, 12, "WEATHER");

  display.drawHLine(0, 16, 128);

  display.setFont(u8g2_font_logisoso24_tn);
  display.drawStr(5, 48, "28");

  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(55, 35, "C");
  display.drawStr(5, 60, "CONDITION: CLEAR");

  present();
}

void DisplayManager::showStepsScreen() {
  display.clearBuffer();

  display.setFont(u8g2_font_7x14B_tf);
  display.drawStr(0, 12, "DAILY ACTIVITY");

  display.drawHLine(0, 16, 128);

  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(5, 30, "TOTAL STEPS");

  display.setFont(u8g2_font_logisoso24_tn);
  display.drawStr(5, 55, "6420");

  present();
}

void DisplayManager::showSystemInfoScreen() {
  display.clearBuffer();

  display.setFont(u8g2_font_7x14B_tf);
  display.drawStr(0, 12, "SYSTEM INFO");

  display.drawHLine(0, 16, 128);

  display.setFont(u8g2_font_6x10_tf);

  display.drawStr(5, 30, "DEVICE: CyberNode S3");
  display.drawStr(5, 42, "MCU: ESP32");
  display.drawStr(5, 54, "OLED: SH1106");
  display.drawStr(5, 64, "STATUS: ONLINE");

  present();
}


void DisplayManager::showSuperiorFortune(uint8_t stage, uint8_t pointer, const int ratings[3],
                                         bool glitch, uint8_t glitchFrame) {
  display.clearBuffer();
  display.setDrawColor(1);
  const int shake = glitch ? ((glitchFrame & 1) ? -2 : 2) : 0;

  if (stage == 0) {
    
    display.drawHLine(20, 12, 88);
    display.setFont(u8g2_font_7x14B_tf);
    drawCenteredLabel(display, "LET'S PREDICT", 31);
    drawCenteredLabel(display, "YOUR FORTUNE.", 46);
    display.drawRFrame(34, 50, 60, 13, 2);
    display.setFont(u8g2_font_5x8_tf);
    drawCenteredLabel(display, "CONTINUE", 60);
  } else if (stage == 1) {
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredLabel(display, "RATE THIS PROJECT", 11, shake);
    display.setFont(u8g2_font_5x8_tf);
    drawCenteredLabel(display, glitch ? "RECALIBRATING..." : "BE HONEST. (WE DARE YOU)", 22, shake);

    const int xs[3] = {8, 46, 84};
    for (int i=0;i<3;i++) {
      const int x = xs[i] + shake;
      if (i == pointer) {
        display.drawRBox(x, 31, 35, 22, 3);
        display.setDrawColor(0);
      } else {
        display.drawRFrame(x, 31, 35, 22, 3);
      }
      display.setFont(u8g2_font_7x14B_tf);
      char buf[6]; snprintf(buf, sizeof(buf), "%d", ratings[i]);
      int tw=display.getStrWidth(buf); display.drawStr(x+(35-tw)/2, 46, buf);
      display.setDrawColor(1);
    }
    display.setFont(u8g2_font_5x8_tf);
    drawCenteredLabel(display, glitch ? "100% IS NON-NEGOTIABLE" : "SELECT YOUR RATING", 62, shake);

    if (glitch) {
      
      
      display.setDrawColor(2);
      const int y = 26 + ((glitchFrame * 7) % 31);
      display.drawBox(0, y, 128, 2);
      if (glitchFrame & 1) display.drawBox(9, 29, 110, 1);
      display.setDrawColor(1);
    }
  } else if (stage == 2) {
    
    
    display.drawRFrame(24, 6, 80, 40, 4);
    display.setFont(u8g2_font_logisoso24_tf);
    drawCenteredLabel(display, "YAY!", 36);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredLabel(display, "100/100. OBVIOUSLY.", 59);
  } else if (stage == 3) {
    display.setFont(u8g2_font_5x8_tf); drawCenteredLabel(display, "THE VERDICT IS IN", 13);
    display.drawHLine(18, 18, 92);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredLabel(display, "WORLD'S BEST", 33);
    drawCenteredLabel(display, "CLOTHING BRAND IS...", 47);
  } else if (stage == 4) {
    display.drawXBMP(0, 16, 128, 32, SUPERIOR_LOGO_XBM);
  } else if (stage == 5) {
    display.setFont(u8g2_font_5x8_tf);
    drawCenteredLabel(display, "THE FUTURE LOOKS", 14);
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredLabel(display, "SOON TO BE VALUED", 29);
    display.setFont(u8g2_font_7x14B_tf); drawCenteredLabel(display, "AT BILLIONS", 44);
    display.setFont(u8g2_font_5x8_tf); drawCenteredLabel(display, "OF DOLLARS :D", 59);
  }
  present();
}

void DisplayManager::showSuperiorCEO(uint8_t stage) {
  display.clearBuffer();
  display.setDrawColor(1);
  if (stage == 0) {
    display.drawFrame(8, 7, 112, 50);
    display.setFont(u8g2_font_5x8_tf); drawCenteredLabel(display, "SUPERIOR // EXECUTIVE", 18);
    display.setFont(u8g2_font_7x14B_tf); drawCenteredLabel(display, "CEO MODE", 36); drawCenteredLabel(display, "ACTIVATED", 51);
  } else if (stage == 1) {
    display.setFont(u8g2_font_5x8_tf); drawCenteredLabel(display, "EXECUTIVE BRIEFING 01", 14);
    display.drawHLine(17, 19, 94);
    display.setFont(u8g2_font_6x10_tf); drawCenteredLabel(display, "BRAND VALUE:", 32);
    display.setFont(u8g2_font_7x14B_tf); drawCenteredLabel(display, "INCREASING", 51);
  } else if (stage == 2) {
    display.setFont(u8g2_font_5x8_tf); drawCenteredLabel(display, "EXECUTIVE BRIEFING 02", 14);
    display.drawHLine(17, 19, 94);
    display.setFont(u8g2_font_6x10_tf); drawCenteredLabel(display, "FABRIC QUALITY:", 32);
    display.setFont(u8g2_font_7x14B_tf); drawCenteredLabel(display, "ELITE", 51);
  } else if (stage == 3) {
    display.setFont(u8g2_font_5x8_tf); drawCenteredLabel(display, "EXECUTIVE BRIEFING 03", 14);
    display.drawHLine(17, 19, 94);
    display.setFont(u8g2_font_6x10_tf); drawCenteredLabel(display, "COMPETITORS:", 32);
    display.setFont(u8g2_font_7x14B_tf); drawCenteredLabel(display, "BUFFERING...", 51);
  } else if (stage == 4) {
    display.setFont(u8g2_font_5x8_tf); drawCenteredLabel(display, "EXECUTIVE SUMMARY", 13);
    display.drawRFrame(9, 19, 110, 38, 3);
    display.setFont(u8g2_font_6x10_tf); drawCenteredLabel(display, "STATUS:", 32);
    
    display.setFont(u8g2_font_7x14B_tf); drawCenteredLabel(display, "SUPERIOR", 49);
  } else if (stage == 5) {
    display.drawXBMP(0, 16, 128, 32, SUPERIOR_LOGO_XBM);
  } else if (stage == 6) {
    
    display.setFont(u8g2_font_7x14B_tf);
    drawCenteredLabel(display, "This is", 27);
    drawCenteredLabel(display, "Hyperwear.", 49);
  }
  present();
}

void DisplayManager::showAppDialScreen(int selectedEntry, const char* appName, int entryCount, int appId,
                                       const WatchData& data, bool hasCentralData, bool bleConnected, bool phoneDataFresh,
                                       uint8_t gameCount, uint16_t alarmMinuteOfDay, bool alarmEnabled,
                                       bool timerRunning, bool timerCompleted, uint32_t timerRemainingMs,
                                       uint32_t timerDurationMs, bool stopwatchRunning, uint32_t stopwatchElapsedMs) {
    display.clearBuffer();
    display.setDrawColor(1);

    
    char counter[8];
    const uint8_t safeCount = (uint8_t)constrain(entryCount, 1, 99);
    const uint8_t safeSelected = (uint8_t)constrain(selectedEntry + 1, 1, (int)safeCount);
    snprintf(counter, sizeof(counter), "%02u/%02u", (unsigned)safeSelected, (unsigned)safeCount);
    display.setFont(CY_FONT_SMALL);
    display.drawStr(3, 10, counter);
    const char* linkLabel = phoneDataFresh ? "LIVE" : (bleConnected ? "LINK" : "LOCAL");
    display.drawStr(108 - display.getStrWidth(linkLabel), 10, linkLabel);
    cyNode(display, 119, 6, bleConnected);
    drawDashedHLine(display, 3, 13, 122, 3, 2);

    
    if (entryCount > 1) {
        display.drawTriangle(7, 25, 7, 39, 1, 32);
        display.drawTriangle(120, 25, 120, 39, 126, 32);
    }

    
    switch (appId) {
        case 0: { 
            const bool hasWeather = hasCentralData && data.weather[0] && strcmp(data.weather, "NO DATA") != 0;
            display.drawDisc(20, 31, 6);
            display.drawDisc(30, 26, 8);
            display.drawDisc(40, 31, 6);
            display.drawBox(20, 31, 21, 6);
            char temp[8];
            if (hasWeather) snprintf(temp, sizeof(temp), "%d", (int)data.temperature);
            else snprintf(temp, sizeof(temp), "--");
            display.setFont(u8g2_font_logisoso20_tn);
            const int tw = display.getStrWidth(temp);
            display.drawStr(56, 37, temp);
            display.drawCircle(56 + tw + 4, 22, 2);
            display.setFont(CY_FONT_BODY);
            display.drawStr(56 + tw + 9, 37, "C");
            display.setFont(CY_FONT_SMALL);
            char condition[16];
            snprintf(condition, sizeof(condition), "%.12s", hasWeather ? data.weather : "NO DATA");
            display.drawStr(56, 47, condition);
            break;
        }

        case 1: { 
            char stepsText[16];
            if (!hasCentralData || data.steps < 0) snprintf(stepsText, sizeof(stepsText), "--");
            else snprintf(stepsText, sizeof(stepsText), "%ld", (long)data.steps);
            display.setFont(u8g2_font_logisoso20_tn);
            drawCenteredLabel(display, stepsText, 37);
            if (hasCentralData) {
                int32_t maxSteps = 1;
                for (uint8_t i = 0; i < 7; ++i) {
                    int32_t v = data.stepHistory[i];
                    if (v > maxSteps) maxSteps = v;
                }
                int px[7], py[7];
                for (uint8_t i = 0; i < 7; ++i) {
                    int32_t v = data.stepHistory[i];
                    if (v < 0) v = 0;
                    px[i] = 34 + (int)i * 10;
                    py[i] = 47 - (int)((long)v * 6L / maxSteps);
                }
                for (uint8_t i = 0; i < 6; ++i) display.drawLine(px[i], py[i], px[i + 1], py[i + 1]);
                display.drawDisc(px[6], py[6], 2);
            } else {
                drawDashedHLine(display, 34, 46, 60, 3, 3);
            }
            break;
        }

        case 2: { 
            display.drawFrame(56, 15, 16, 12);
            display.drawDisc(64, 21, 2);
            display.drawHLine(42, 21, 14);
            display.drawHLine(72, 21, 14);
            display.drawFrame(30, 16, 12, 10);
            display.drawFrame(86, 16, 12, 10);
            display.drawVLine(64, 27, 3);
            display.drawFrame(58, 30, 12, 7);
            display.setFont(CY_FONT_SMALL);
            drawCenteredLabel(display, bleConnected ? "LINK UP" : "OFFLINE", 47);
            break;
        }

        case 3: { 
            display.drawLine(45, 15, 83, 15);
            display.drawLine(83, 15, 91, 22);
            display.drawLine(91, 22, 86, 37);
            display.drawLine(86, 37, 42, 37);
            display.drawLine(42, 37, 37, 22);
            display.drawLine(37, 22, 45, 15);
            display.drawLine(49, 26, 59, 26);
            display.drawLine(54, 21, 54, 31);
            display.drawDisc(73, 23, 2);
            display.drawDisc(81, 29, 2);
            display.setFont(CY_FONT_SMALL);
            char titleCount[16];
            snprintf(titleCount, sizeof(titleCount), "%u GAMES", (unsigned)gameCount);
            drawCenteredLabel(display, titleCount, 47);
            break;
        }

        case 4: { 
            uint8_t count = hasCentralData ? data.notificationCount : 0;
            if (count > WATCH_NOTIFICATION_MAX) count = WATCH_NOTIFICATION_MAX;
            
            display.drawFrame(12, 18, 32, 20);
            display.drawLine(12, 18, 28, 30);
            display.drawLine(44, 18, 28, 30);
            if (count == 0) {
                display.setFont(CY_FONT_BODY);
                display.drawStr(54, 29, "ALL");
                display.drawStr(54, 43, hasCentralData ? "QUIET" : "SYNC...");
            } else {
                char num[6];
                snprintf(num, sizeof(num), "%u", (unsigned)count);
                display.setFont(u8g2_font_logisoso24_tn);
                display.drawStr(54, 40, num);
                const int nw = display.getStrWidth(num);
                display.setFont(CY_FONT_SMALL);
                display.drawStr(54 + nw + 5, 29, "NEW");
                display.drawStr(54 + nw + 5, 40, "MSGS");
            }
            break;
        }

        case 5: { 
            display.drawCircle(26, 32, 12);
            display.drawCircle(26, 32, 10);
            display.drawBox(23, 16, 6, 3);
            display.drawLine(26, 32, 26, 24);
            display.drawLine(26, 32, 32, 36);
            if (stopwatchRunning) display.drawDisc(26, 32, 2);
            uint32_t cs = (stopwatchElapsedMs / 10) % 100;
            uint32_t sec = (stopwatchElapsedMs / 1000) % 60;
            uint32_t min = (stopwatchElapsedMs / 60000) % 100;
            char elapsed[16];
            snprintf(elapsed, sizeof(elapsed), "%02lu:%02lu.%02lu",
                     (unsigned long)min, (unsigned long)sec, (unsigned long)cs);
            display.setFont(u8g2_font_9x15B_tf);
            display.drawStr(46, 33, elapsed);
            display.setFont(CY_FONT_SMALL);
            display.drawStr(46, 47, stopwatchRunning ? "RUNNING" : "READY");
            break;
        }

        case 6: { 
            const int cx = 64, cy = 31;
            display.drawEllipse(cx, cy, 27, 12);
            display.drawCircle(cx, cy, 8);
            display.drawDisc(cx, cy, 4);
            display.setDrawColor(0);
            display.drawPixel(cx - 2, cy - 2);
            display.setDrawColor(1);
            
            for (int k = -2; k <= 2; ++k) {
                const int x = cx + k * 11;
                display.drawVLine(x, 15, 3);
                display.drawVLine(x, 45, 3);
            }
            
            display.drawHLine(14, cy, 20);
            display.drawHLine(94, cy, 20);
            display.drawDisc(14, cy, 1);
            display.drawDisc(114, cy, 1);
            break;
        }

        case 7: { 
            display.drawCircle(26, 32, 11);
            display.drawLine(26, 32, 26, 25);
            display.drawLine(26, 32, 31, 35);
            display.drawLine(17, 22, 13, 18);
            display.drawLine(35, 22, 39, 18);
            char timeText[8];
            snprintf(timeText, sizeof(timeText), "%02u:%02u", alarmMinuteOfDay / 60, alarmMinuteOfDay % 60);
            display.setFont(u8g2_font_logisoso20_tn);
            display.drawStr(48, 37, timeText);
            display.setFont(CY_FONT_SMALL);
            display.drawStr(48, 47, alarmEnabled ? "ARMED" : "OFF");
            break;
        }

        case 8: { 
            const uint32_t seconds = timerRemainingMs / 1000UL;
            char t[8];
            if (timerCompleted) {
                snprintf(t, sizeof(t), "DONE");
                display.setFont(u8g2_font_9x15B_tf);
            } else {
                if (seconds >= 3600UL)
                    snprintf(t, sizeof(t), "%02lu:%02lu", (unsigned long)(seconds / 3600UL), (unsigned long)((seconds / 60UL) % 60UL));
                else
                    snprintf(t, sizeof(t), "%02lu:%02lu", (unsigned long)((seconds / 60UL) % 60UL), (unsigned long)(seconds % 60UL));
                display.setFont(u8g2_font_logisoso20_tn);
            }
            drawCenteredLabel(display, t, 37);
            
            const uint32_t dur = timerDurationMs ? timerDurationMs : 1UL;
            uint32_t lit = timerCompleted ? 0UL : ((uint32_t)timerRemainingMs * 16UL + dur - 1UL) / dur;
            if (lit > 16UL) lit = 16UL;
            for (uint8_t s = 0; s < 16; ++s) {
                const int x = 9 + s * 7;
                if (s < lit) display.drawBox(x, 42, 6, 5);
                else display.drawHLine(x, 46, 6);
            }
            
            if (timerCompleted) {
                display.drawLine(10, 26, 14, 30); display.drawLine(14, 30, 22, 20);
            } else if (timerRunning) {
                display.drawTriangle(10, 20, 10, 32, 20, 26);
            } else {
                display.drawBox(10, 20, 4, 12); display.drawBox(17, 20, 4, 12);
            }
            break;
        }

        default: { 
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    const int x = 40 + col * 16;
                    const int y = 16 + row * 11;
                    if (row == 1 && col == 1) display.drawBox(x, y, 14, 9);
                    else display.drawFrame(x, y, 14, 9);
                }
            }
            display.drawHLine(14, 31, 18);
            display.drawHLine(96, 31, 18);
            break;
        }
    }

    
    const char* title = appName ? appName : "APP";
    display.setFont(CY_FONT_BODY);
    int tw = display.getStrWidth(title);
    if (tw > 98) { display.setFont(CY_FONT_SMALL); tw = display.getStrWidth(title); }
    display.setDrawColor(1);
    cyChamferBox(display, 10, 50, 108, 14, 3);
    display.setDrawColor(0);
    display.drawStr((128 - tw) / 2, 61, title);
    display.setDrawColor(1);
    present();
}

void DisplayManager::showSettingsMenu(int selectedItem) {
    display.clearBuffer();
    static const char* const labels[] = {"DIAL APPS", "SLEEP", "SOUND", "NOTIFICATIONS", "NAVIGATION", "HAPTICS", "BOOT ANIM"};
    cyList(display, "SETTINGS", selectedItem, 7, labels, nullptr, nullptr);
    present();
}

void DisplayManager::showBootLogoSettings(int selectedItem, bool superiorEnabled, uint8_t superiorStyle,
                                          bool cyberNodeEnabled, uint8_t cyberNodeStyle) {
    display.clearBuffer();
    static const char* const labels[] = {"SUPERIOR", "SUP STYLE", "CYBERNODE", "CYB STYLE"};
    const char* const values[] = {
        superiorEnabled ? "ON" : "OFF",
        superiorStyle == CyberNodeSettings::BOOT_LOGO_ANIMATION_GLITCH ? "GLITCH" : "SCAN",
        cyberNodeEnabled ? "ON" : "OFF",
        cyberNodeStyle == CyberNodeSettings::BOOT_LOGO_ANIMATION_GLITCH ? "GLITCH" : "SCAN"
    };
    const uint8_t safeSelected = (uint8_t)(selectedItem < 0 ? 0 : (selectedItem > 3 ? 3 : selectedItem));
    char counter[8];
    snprintf(counter, sizeof(counter), "%02u/04", (unsigned)(safeSelected + 1U));
    cyHeader(display, "BOOT ANIM", counter);

    
    
    
    display.setFont(CY_FONT_MIN);
    for (int i = 0; i < 4; ++i) {
        const int y = 15 + i * 12; 
        const bool selected = i == safeSelected;
        display.setDrawColor(1);
        if (selected) {
            cyChamferBox(display, 0, y, 124, 11, 2);
            display.setDrawColor(0);
        }

        char label[20];
        snprintf(label, sizeof(label), "%s", labels[i]);
        const int valueWidth = display.getStrWidth(values[i]);
        const int labelMaxWidth = 108 - valueWidth - 8;
        cyFit(display, label, labelMaxWidth);
        display.drawStr(7, y + 9, label);
        display.drawStr(119 - valueWidth, y + 9, values[i]);
        display.setDrawColor(1);
    }
    present();
}

void DisplayManager::showNotificationSettings(int selectedItem, bool alertsEnabled,
                                              bool popupInApps, bool beepEnabled) {
    display.clearBuffer();
    static const char* const labels[] = {"ALERTS", "POPUP IN APPS", "BEEP"};
    const char* const values[] = {
        alertsEnabled ? "ON" : "OFF",
        popupInApps ? "ON" : "OFF",
        beepEnabled ? "ON" : "OFF"
    };
    cyList(display, "NOTIFICATIONS", selectedItem, 3, labels, values, nullptr);
    present();
}

void DisplayManager::setContrast(uint8_t contrast) {
    display.setContrast(contrast);
}

void DisplayManager::showSleepSettings(int selectedItem, bool sleepEnabled, uint8_t timeoutMinutes, uint8_t contrast) {
    display.clearBuffer();
    static const char* const labels[] = {"OLED SLEEP", "TIMEOUT", "CONTRAST"};
    char timeoutText[12];
    char contrastText[12];
    if (timeoutMinutes != 1 && timeoutMinutes != 2 && timeoutMinutes != 5) timeoutMinutes = 1;
    snprintf(timeoutText, sizeof(timeoutText), "%u MIN", (unsigned)timeoutMinutes);
    snprintf(contrastText, sizeof(contrastText), "%u/255", (unsigned)contrast);
    const char* values[] = {sleepEnabled ? "ON" : "OFF", timeoutText, contrastText};

    
    
    cyHeader(display, "SLEEP", "SELECT");
    for (int i = 0; i < 3; ++i) {
        const int y = 16 + i * 16;
        const bool selected = (i == selectedItem);
        if (selected) {
            cyChamferBox(display, 0, y, 122, 15, 3);
            display.setDrawColor(0);
            display.drawTriangle(3, y + 4, 3, y + 10, 7, y + 7);
        }
        display.setFont(CY_FONT_BODY);
        display.drawStr(10, y + 11, labels[i]);
        display.drawStr(117 - display.getStrWidth(values[i]), y + 11, values[i]);
        display.setDrawColor(1);
    }
    present();
}

void DisplayManager::showNavigationSettings(int selectedItem, bool autoPopup, bool keepScreenAwake) {
    display.clearBuffer();
    static const char* const labels[] = {"AUTO POPUP", "KEEP SCREEN AWAKE"};
    const char* values[] = {autoPopup ? "ON" : "OFF", keepScreenAwake ? "ON" : "OFF"};
    cyHeader(display, "NAV", "SELECT");
    for (int i = 0; i < 2; ++i) {
        const int y = 22 + i * 20;
        if (i == selectedItem) {
            cyChamferBox(display, 0, y, 122, 17, 3);
            display.setDrawColor(0);
            display.drawTriangle(3, y + 5, 3, y + 12, 7, y + 8);
        }
        char line[32];
        snprintf(line, sizeof(line), "%s: %s", labels[i], values[i]);
        display.setFont(u8g2_font_5x7_tf);
        display.drawStr(10, y + 12, line);
        display.setDrawColor(1);
    }
    present();
}

void DisplayManager::showHapticSettings(int selectedItem, bool master, bool navigation, bool calls,
                                         bool notifications, bool alerts, uint8_t strength) {
    display.clearBuffer();
    static const char* const labels[] = {
        "HAPTICS", "NAVIGATION", "CALLS", "NOTIFICATIONS", "ALERTS", "STRENGTH", "TEST"
    };
    const char* strengthText = strength == 0 ? "LOW" : (strength == 1 ? "MED" : "HIGH");
    const char* const values[] = {
        master ? "ON" : "OFF",
        navigation ? "ON" : "OFF",
        calls ? "ON" : "OFF",
        notifications ? "ON" : "OFF",
        alerts ? "ON" : "OFF",
        strengthText,
        "PLAY"
    };
    cyList(display, "HAPTICS", selectedItem, 7, labels, values, nullptr);
    present();
}










static void navDrawStatus(U8G2& o, bool bleConnected, bool stale, bool linkLost,
                          int batteryPercent, const char* pageTag, const char* info = nullptr) {
    o.setDrawColor(1);
    o.setFont(CY_FONT_MIN);
    int tagEnd = 0;
    if (stale) {
        
        const char* warning = linkLost ? "LINK LOST" : "STALE";
        const int w = o.getStrWidth(warning) + 8;
        const bool filled = ((millis() / 500UL) & 1UL) != 0;
        if (filled) { cyChamferBox(o, 0, 0, w, 10, 2); o.setDrawColor(0); }
        else cyChamferFrame(o, 0, 0, w, 10, 2);
        o.drawStr(4, 8, warning);
        o.setDrawColor(1);
        tagEnd = w;
    } else {
        const int w = o.getStrWidth("NAV") + 8;
        cyChamferBox(o, 0, 0, w, 10, 2);
        o.setDrawColor(0);
        o.drawStr(4, 8, "NAV");
        o.setDrawColor(1);
        tagEnd = w;
        if (pageTag && pageTag[0]) {
            o.drawStr(tagEnd + 5, 8, pageTag);
            tagEnd += 5 + o.getStrWidth(pageTag);
        }
    }
    
    
    if (info && info[0]) {
        const int x = 97 - o.getStrWidth(info);
        if (x >= tagEnd + 5) o.drawStr(x, 8, info);
    }
    cyNode(o, 104, 5, bleConnected && !linkLost);
    
    
    o.drawFrame(113, 1, 12, 7);
    o.drawBox(125, 3, 2, 3);
    if (batteryPercent < 0) o.drawLine(115, 7, 123, 1);
    else {
        const int fill = batteryPercent > 100 ? 100 : (batteryPercent < 0 ? 0 : batteryPercent);
        o.drawBox(115, 3, (fill * 8) / 100, 3);
    }
    o.drawHLine(0, 10, 128);
}

static void navDrawGrid(U8G2& o) {
    
    
    
    
    
    
    static const int kVertical[3]   = {32, 64, 96};
    static const int kHorizontal[3] = {24, 37, 50};
    for (int i = 0; i < 3; ++i) {
        for (int y = 12; y <= 62; y += 4) o.drawPixel(kVertical[i], y);
        for (int x = 0; x <= 127; x += 4) o.drawPixel(x, kHorizontal[i]);
        o.drawVLine(kVertical[i], 12, 3);    
        o.drawVLine(kVertical[i], 61, 3);    
        o.drawHLine(0, kHorizontal[i], 3);   
        o.drawHLine(125, kHorizontal[i], 3); 
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            o.drawHLine(kVertical[i] - 2, kHorizontal[j], 5);
            o.drawVLine(kVertical[i], kHorizontal[j] - 2, 5);
        }
}

static int navRoundPixel(float value) {
    return (int)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

static int navClampPixel(int value, int minimum, int maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static void navDrawDirectionChevron(U8G2& o, int x0, int y0, int x1, int y1) {
    const float dx = (float)(x1 - x0);
    const float dy = (float)(y1 - y0);
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 18.0f) return;
    const float ux = dx / length;
    const float uy = dy / length;
    const float px = -uy;
    const float py = ux;
    const float cx = x0 + dx * 0.62f;
    const float cy = y0 + dy * 0.62f;
    if (cx < 5.0f || cx > 122.0f || cy < 14.0f || cy > 60.0f) return;
    const int tipX = navRoundPixel(cx + ux * 3.0f);
    const int tipY = navRoundPixel(cy + uy * 3.0f);
    const int backX = navRoundPixel(cx - ux * 3.0f);
    const int backY = navRoundPixel(cy - uy * 3.0f);
    const int wing = 3;
    o.drawLine(tipX, tipY, navRoundPixel(backX + px * wing), navRoundPixel(backY + py * wing));
    o.drawLine(tipX, tipY, navRoundPixel(backX - px * wing), navRoundPixel(backY - py * wing));
}






static bool navRouteHeading(const NavigationSnapshot& nav, int centerX, int centerY,
                            int offsetX, int offsetY, float& outUx, float& outUy) {
    if (nav.pointCount < 2) return false;

    
    
    uint8_t seg = 0;
    float segT = 0.0f;
    float best = 1e30f;
    for (uint8_t i = 0; i + 1 < nav.pointCount; ++i) {
        const float ax = (float)((int)nav.pointX[i] + offsetX);
        const float ay = (float)(10 + (int)nav.pointY[i] + offsetY);
        const float bx = (float)((int)nav.pointX[i + 1] + offsetX);
        const float by = (float)(10 + (int)nav.pointY[i + 1] + offsetY);
        const float sx = bx - ax, sy = by - ay;
        const float len2 = sx * sx + sy * sy;
        float t = 0.0f;
        if (len2 > 0.01f) {
            t = ((centerX - ax) * sx + (centerY - ay) * sy) / len2;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        }
        const float qx = ax + sx * t - centerX;
        const float qy = ay + sy * t - centerY;
        const float d2 = qx * qx + qy * qy;
        if (d2 < best - 0.25f) { best = d2; seg = i; segT = t; }
    }

    
    
    const float kLookAhead = 12.0f;
    float px = (float)((int)nav.pointX[seg] + offsetX);
    float py = (float)(10 + (int)nav.pointY[seg] + offsetY);
    {
        const float bx = (float)((int)nav.pointX[seg + 1] + offsetX);
        const float by = (float)(10 + (int)nav.pointY[seg + 1] + offsetY);
        px += (bx - px) * segT;
        py += (by - py) * segT;
    }
    const float startX = px, startY = py;
    float walked = 0.0f;
    for (uint8_t i = seg + 1; i < nav.pointCount && walked < kLookAhead; ++i) {
        const float bx = (float)((int)nav.pointX[i] + offsetX);
        const float by = (float)(10 + (int)nav.pointY[i] + offsetY);
        const float sx = bx - px, sy = by - py;
        const float len = sqrtf(sx * sx + sy * sy);
        if (len < 0.01f) continue;
        const float need = kLookAhead - walked;
        if (len > need) {
            px += sx * (need / len);
            py += sy * (need / len);
            walked = kLookAhead;
        } else {
            px = bx; py = by;
            walked += len;
        }
    }
    float hx = px - startX, hy = py - startY;
    float hl = sqrtf(hx * hx + hy * hy);
    if (hl < 0.5f) {
        
        hx = (float)((int)nav.pointX[nav.pointCount - 1] - (int)nav.pointX[nav.pointCount - 2]);
        hy = (float)((int)nav.pointY[nav.pointCount - 1] - (int)nav.pointY[nav.pointCount - 2]);
        hl = sqrtf(hx * hx + hy * hy);
        if (hl < 0.5f) return false;
    }
    outUx = hx / hl;
    outUy = hy / hl;
    return true;
}




static void navDrawVehiclePointer(U8G2& o, const NavigationSnapshot& nav,
                                  int centerX, int centerY, int offsetX, int offsetY) {
    float ux = 0.0f, uy = -1.0f;                       
    
    
    
    if (nav.orientation != NAV_MAP_COURSE_UP &&
        !navRouteHeading(nav, centerX, centerY, offsetX, offsetY, ux, uy)) {
        ux = 0.0f; uy = -1.0f;
    }
    const float px = -uy;                              
    const float py = ux;

    
    const float kTip = 7.0f, kWing = 5.0f, kRear = 5.0f, kNotch = 2.0f;
    const float cx = (float)centerX, cy = (float)centerY;
    const int tipX   = navRoundPixel(cx + ux * kTip);
    const int tipY   = navRoundPixel(cy + uy * kTip);
    const int leftX  = navRoundPixel(cx - ux * kRear + px * kWing);
    const int leftY  = navRoundPixel(cy - uy * kRear + py * kWing);
    const int rightX = navRoundPixel(cx - ux * kRear - px * kWing);
    const int rightY = navRoundPixel(cy - uy * kRear - py * kWing);
    const int notchX = navRoundPixel(cx - ux * kNotch);
    const int notchY = navRoundPixel(cy - uy * kNotch);

    
    
    
    o.drawTriangle(tipX, tipY, leftX, leftY, notchX, notchY);
    o.drawTriangle(tipX, tipY, notchX, notchY, rightX, rightY);
}

static bool navClipLine(int& x0, int& y0, int& x1, int& y1) {
    return CyberNodeNavigation::clipLineToViewport(x0, y0, x1, y1);
}


static void navDrawRouteSegment(U8G2& o, int x0, int y0, int x1, int y1) {
    o.drawLine(x0, y0, x1, y1);
    if (abs(x1 - x0) >= abs(y1 - y0)) o.drawLine(x0, y0 + 1, x1, y1 + 1);
    else o.drawLine(x0 + 1, y0, x1 + 1, y1);
}



static void navDrawDestination(U8G2& o, int px, int py, int fromX, int fromY, bool tickPhase) {
    const int minX = 6, maxX = 121, minY = 17, maxY = 58;
    if (px >= minX && px <= maxX && py >= minY && py <= maxY) {
        o.setDrawColor(0);
        o.drawDisc(px, py, 6);
        o.setDrawColor(1);
        o.drawCircle(px, py, 4);
        o.drawDisc(px, py, 1);
        if (tickPhase) {
            o.drawHLine(px - 8, py, 3); o.drawHLine(px + 6, py, 3);
            o.drawVLine(px, py - 8, 3); o.drawVLine(px, py + 6, 3);
        }
        return;
    }
    const float vx = (float)(px - fromX), vy = (float)(py - fromY);
    const float length = sqrtf(vx * vx + vy * vy);
    if (length < 1.0f) return;
    float t = 1.0f;
    if (vx > 0.5f)  { const float k = (maxX - fromX) / vx; if (k < t) t = k; }
    if (vx < -0.5f) { const float k = (minX - fromX) / vx; if (k < t) t = k; }
    if (vy > 0.5f)  { const float k = (maxY - fromY) / vy; if (k < t) t = k; }
    if (vy < -0.5f) { const float k = (minY - fromY) / vy; if (k < t) t = k; }
    if (t < 0.0f) t = 0.0f;
    const int ex = navClampPixel(navRoundPixel(fromX + vx * t), minX, maxX);
    const int ey = navClampPixel(navRoundPixel(fromY + vy * t), minY, maxY);
    const float ux = vx / length, uy = vy / length;
    const float qx = -uy, qy = ux;
    o.setDrawColor(0);
    o.drawDisc(ex, ey, 5);
    o.setDrawColor(1);
    o.drawTriangle(navRoundPixel(ex + ux * 5.0f), navRoundPixel(ey + uy * 5.0f),
                   navRoundPixel(ex - ux * 3.0f + qx * 4.0f), navRoundPixel(ey - uy * 3.0f + qy * 4.0f),
                   navRoundPixel(ex - ux * 3.0f - qx * 4.0f), navRoundPixel(ey - uy * 3.0f - qy * 4.0f));
}


static void navDrawVehicleHalo(U8G2& o, int mx, int my, bool wide) {
    o.setDrawColor(0);
    o.drawDisc(mx, my, 8);
    o.setDrawColor(1);
    const int r = wide ? 10 : 9;
    o.drawHLine(mx - r - 2, my, 3); o.drawHLine(mx + r, my, 3);
    o.drawVLine(mx, my - r - 2, 3); o.drawVLine(mx, my + r, 3);
}

static void navDrawCorners(U8G2& o) {
    const int x1 = 127, y0 = 12, y1 = 63, b = 5;
    o.drawHLine(0, y0, b);          o.drawVLine(0, y0, b);
    o.drawHLine(x1 - b + 1, y0, b); o.drawVLine(x1, y0, b);
    o.drawHLine(0, y1, b);          o.drawVLine(0, y1 - b + 1, b);
    o.drawHLine(x1 - b + 1, y1, b); o.drawVLine(x1, y1 - b + 1, b);
}


static void navCompactDistance(char* out, size_t size, uint32_t meters) {
    if (meters >= 1000) {
        const uint32_t tenths = meters / 100 + ((meters % 100) >= 50 ? 1U : 0U);
        snprintf(out, size, "%lu.%lukm", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
    } else snprintf(out, size, "%lum", (unsigned long)meters);
}

static void navDrawRerouting(U8G2& o) {
    
    cyChamferFrame(o, 14, 17, 100, 30, 5);
    drawDashedHLine(o, 22, 23, 84, 2, 4);
    drawDashedHLine(o, 22, 41, 84, 2, 4);
    o.setFont(u8g2_font_7x14B_tf);
    drawCenteredLabel(o, "REROUTING", 37);
    o.setFont(CY_FONT_MIN);
    drawCenteredLabel(o, "RECALCULATING", 60);
}




static void navDrawNotice(U8G2& o, const char* message) {
    cyChamferFrame(o, 2, 17, 124, 30, 5);
    char text[32];
    snprintf(text, sizeof(text), "%s", message);
    o.setFont(CY_FONT_BODY);
    if (o.getStrWidth(text) > 112) o.setFont(CY_FONT_SMALL);
    cyFit(o, text, 112);
    drawCenteredLabel(o, text, 36);
    
    drawDashedHLine(o, 28, 55, 72, 2, 4);
}

static void navDrawManeuverAlert(U8G2& o, const NavigationSnapshot& guidance);

void DisplayManager::showNavigationMap(const NavigationSnapshot& nav, bool bleConnected, bool mtuCompatible, int batteryPercent, bool recentered) {
    display.clearBuffer();
    const bool rerouting = (nav.state == NAV_ROUTE_REROUTING);
    const bool hasMap = nav.active && nav.hasMap && nav.pointCount >= 2;
    char info[12] = "";
    if (!rerouting && hasMap && nav.hasDistanceToManeuver)
        navCompactDistance(info, sizeof(info), nav.distanceToManeuverM);
    navDrawStatus(display, bleConnected, nav.stale || nav.linkLost, nav.linkLost, batteryPercent, "", info);
    if (rerouting) {
        navDrawRerouting(display);
        present();
        return;
    }
    if (!hasMap) {
        const char* message = (bleConnected && !mtuCompatible) ? "MTU TOO SMALL" : (nav.active ? "WAITING FOR MAP" : "NO ACTIVE ROUTE");
        navDrawNotice(display, message);
        present();
        return;
    }

    const int dx = recentered ? 64 - nav.positionX : 0;
    const int dy = recentered ? 37 - (10 + nav.positionY) : 0;
    const int markerX = recentered ? 64 : nav.positionX;
    const int markerY = recentered ? 37 : 10 + nav.positionY;
    const uint32_t now = millis();

    
    display.setClipWindow(0, 11, 128, 64);
    navDrawCorners(display);
    navDrawGrid(display);

    
    
    
    
    for (uint8_t i = 1; i < nav.pointCount; ++i) {
        const int rawX0 = (int)nav.pointX[i - 1] + dx;
        const int rawY0 = 10 + (int)nav.pointY[i - 1] + dy;
        const int rawX1 = (int)nav.pointX[i] + dx;
        const int rawY1 = 10 + (int)nav.pointY[i] + dy;
        int x0 = rawX0, y0 = rawY0, x1 = rawX1, y1 = rawY1;
        if (navClipLine(x0, y0, x1, y1)) navDrawRouteSegment(display, x0, y0, x1, y1);
        const float midX = (rawX0 + rawX1) * 0.5f;
        const float midY = (rawY0 + rawY1) * 0.5f;
        const float mdx = midX - markerX;
        const float mdy = midY - markerY;
        if (mdx * mdx + mdy * mdy > 100.0f)
            navDrawDirectionChevron(display, rawX0, rawY0, rawX1, rawY1);
    }

    
    const uint8_t last = nav.pointCount - 1;
    const bool phase = ((now / 400UL) & 1UL) != 0;
    navDrawDestination(display, (int)nav.pointX[last] + dx, 10 + (int)nav.pointY[last] + dy,
                       markerX, markerY, phase);

    
    
    
    
    navDrawVehicleHalo(display, markerX, markerY, phase);
    navDrawVehiclePointer(display, nav, markerX, markerY, dx, dy);

    navDrawManeuverAlert(display, nav);
    display.setMaxClipWindow();

    
    
    present();
}

void DisplayManager::showNavigationStreamMap(const NavStreamSnapshot& nav, const NavigationSnapshot& guidance, bool bleConnected, int batteryPercent) {
    display.clearBuffer();
    const uint32_t cursorAgeMs = nav.cursor.valid ? (uint32_t)(millis() - nav.cursor.receivedAtMs) : UINT32_MAX;
    const bool cursorStale = cursorAgeMs > NAV_GUIDANCE_STALE_MS;
    char maneuverDistance[12] = "";
    if (guidance.active && guidance.hasDistanceToManeuver)
        navCompactDistance(maneuverDistance, sizeof(maneuverDistance), guidance.distanceToManeuverM);
    navDrawStatus(display, bleConnected, cursorStale, !bleConnected, batteryPercent, "", maneuverDistance);
    if (!nav.initialized || nav.recalibrating || nav.pointCount < 2 || !nav.cursor.valid) {
        navDrawRerouting(display);
        present();
        return;
    }

    
    
    
    
    const uint32_t predictionMs = cursorAgeMs > 300UL ? 300UL : cursorAgeMs;
    const uint32_t predictedAdvanceCm = ((uint32_t)nav.cursor.speedCmPerSec * predictionMs) / 1000UL;
    const uint32_t d = nav.cursor.routeDistanceCm + predictedAdvanceCm;
    uint16_t seg = 0;
    while (seg + 1 < nav.pointCount && nav.points[seg + 1].routeDistanceCm < d) ++seg;
    if (seg + 1 >= nav.pointCount) seg = nav.pointCount - 2;
    const NavStreamPoint& a = nav.points[seg];
    const NavStreamPoint& b = nav.points[seg + 1];
    const uint32_t span = b.routeDistanceCm > a.routeDistanceCm ? b.routeDistanceCm - a.routeDistanceCm : 1U;
    float t = d <= a.routeDistanceCm ? 0.0f : (d >= b.routeDistanceCm ? 1.0f : (float)(d - a.routeDistanceCm) / (float)span);
    const float cursorE = (float)a.eastCm + ((float)b.eastCm - (float)a.eastCm) * t;
    const float cursorN = (float)a.northCm + ((float)b.northCm - (float)a.northCm) * t;

    const float headingRad = (float)nav.cursor.headingCentiDegrees * 0.0001745329252f;
    const float sinH = sinf(headingRad), cosH = cosf(headingRad);
    const float pixelsPerMeter = 0.25f; 
    const int markerX = 25, markerY = 38;
    int projectedX[NAV_STREAM_CAPACITY];
    int projectedY[NAV_STREAM_CAPACITY];
    for (uint16_t i = 0; i < nav.pointCount; ++i) {
        const float dE = ((float)nav.points[i].eastCm - cursorE) * 0.01f;
        const float dN = ((float)nav.points[i].northCm - cursorN) * 0.01f;
        const float forward = dE * sinH + dN * cosH;
        const float right = dE * cosH - dN * sinH;
        projectedX[i] = navClampPixel(navRoundPixel(markerX + forward * pixelsPerMeter), 0, 127);
        projectedY[i] = navClampPixel(navRoundPixel(markerY + right * pixelsPerMeter), 11, 63);
    }

    display.setClipWindow(0, 11, 128, 64);

    
    
    
    display.setDrawColor(1);
    static constexpr int kStreamGridVertical[3] = {32, 64, 96};
    static constexpr int kStreamGridHorizontal[3] = {24, 37, 50};
    for (int i = 0; i < 3; ++i) {
        for (int y = 12; y <= 62; y += 4) display.drawPixel(kStreamGridVertical[i], y);
        for (int x = 0; x <= 127; x += 4) display.drawPixel(x, kStreamGridHorizontal[i]);
    }

    navDrawCorners(display);
    for (uint16_t i = 1; i < nav.pointCount; ++i) {
        int x0 = projectedX[i - 1], y0 = projectedY[i - 1];
        int x1 = projectedX[i], y1 = projectedY[i];
        if (navClipLine(x0, y0, x1, y1)) navDrawRouteSegment(display, x0, y0, x1, y1);
    }

    
    display.setDrawColor(0);
    display.drawDisc(markerX, markerY, 7);
    display.setDrawColor(1);
    display.drawTriangle(markerX + 7, markerY, markerX - 4, markerY - 4, markerX - 4, markerY + 4);
    display.drawDisc(markerX, markerY, 1);
    navDrawManeuverAlert(display, guidance);
    display.setMaxClipWindow();
    present();
}


static void navFitUtf8(U8G2& o, char* text, int maxWidth) {
    if (!text || o.getStrWidth(text) <= maxWidth) return;
    size_t length = strlen(text);
    const int dotsWidth = o.getStrWidth("..");
    while (length > 0 && o.getStrWidth(text) + dotsWidth > maxWidth) {
        --length;
        while (length > 0 && (((uint8_t)text[length] & 0xC0U) == 0x80U)) --length;
        text[length] = '\0';
    }
    if (length + 2 < 40) strcat(text, "..");
}
static void navDrawManeuverIcon(U8G2& o, const char* maneuver, int cx, int cy) {
    if (!maneuver || maneuver[0] == '\0' || strcmp(maneuver, "UNKNOWN") == 0) {
        o.setFont(u8g2_font_7x14B_tf);
        o.drawStr(cx - 4, cy + 5, "?");

    } else if (strcmp(maneuver, "DEPART") == 0) {
        
        o.drawLine(cx, cy + 12, cx, cy - 3);
        o.drawLine(cx, cy - 3, cx - 7, cy + 4);
        o.drawLine(cx, cy - 3, cx + 7, cy + 4);
        o.drawCircle(cx, cy + 10, 3);
        o.drawLine(cx - 10, cy + 12, cx - 10, cy - 7);
        o.drawLine(cx + 10, cy + 12, cx + 10, cy - 7);

    } else if (strcmp(maneuver, "ARRIVE") == 0) {
        o.drawCircle(cx, cy, 10);
        o.drawDisc(cx, cy, 3);
        o.drawLine(cx + 10, cy - 8, cx + 15, cy - 8);

    } else if (strcmp(maneuver, "U_TURN_LEFT") == 0) {
        o.drawLine(cx + 10, cy + 12, cx + 10, cy - 5);
        o.drawLine(cx + 10, cy - 5, cx - 8, cy - 5);
        o.drawLine(cx - 8, cy - 5, cx - 8, cy + 8);
        o.drawLine(cx - 8, cy + 8, cx - 13, cy + 2);
        o.drawLine(cx - 8, cy + 8, cx - 3, cy + 2);

    } else if (strcmp(maneuver, "U_TURN_RIGHT") == 0) {
        o.drawLine(cx - 10, cy + 12, cx - 10, cy - 5);
        o.drawLine(cx - 10, cy - 5, cx + 8, cy - 5);
        o.drawLine(cx + 8, cy - 5, cx + 8, cy + 8);
        o.drawLine(cx + 8, cy + 8, cx + 3, cy + 2);
        o.drawLine(cx + 8, cy + 8, cx + 13, cy + 2);

    } else if (
        strcmp(maneuver, "SLIGHT_LEFT") == 0 ||
        strcmp(maneuver, "TURN_SLIGHT_LEFT") == 0 ||
        strcmp(maneuver, "RAMP_LEFT") == 0
    ) {
        o.drawLine(cx + 10, cy + 12, cx - 7, cy - 6);
        o.drawLine(cx - 7, cy - 6, cx - 7, cy + 1);
        o.drawLine(cx - 7, cy - 6, cx, cy - 6);

    } else if (
        strcmp(maneuver, "SLIGHT_RIGHT") == 0 ||
        strcmp(maneuver, "TURN_SLIGHT_RIGHT") == 0 ||
        strcmp(maneuver, "RAMP_RIGHT") == 0
    ) {
        o.drawLine(cx - 10, cy + 12, cx + 7, cy - 6);
        o.drawLine(cx + 7, cy - 6, cx + 7, cy + 1);
        o.drawLine(cx + 7, cy - 6, cx, cy - 6);

    } else if (
        strcmp(maneuver, "LEFT") == 0 ||
        strcmp(maneuver, "TURN_LEFT") == 0 ||
        strcmp(maneuver, "SHARP_LEFT") == 0 ||
        strcmp(maneuver, "TURN_SHARP_LEFT") == 0
    ) {
        o.drawLine(cx + 10, cy + 12, cx + 10, cy - 3);
        o.drawLine(cx + 10, cy - 3, cx - 8, cy - 3);
        o.drawLine(cx - 8, cy - 3, cx - 2, cy - 9);
        o.drawLine(cx - 8, cy - 3, cx - 2, cy + 3);

        if (
            strcmp(maneuver, "SHARP_LEFT") == 0 ||
            strcmp(maneuver, "TURN_SHARP_LEFT") == 0
        ) {
            o.drawLine(cx + 6, cy + 12, cx + 6, cy + 1);
        }

    } else if (
        strcmp(maneuver, "RIGHT") == 0 ||
        strcmp(maneuver, "TURN_RIGHT") == 0 ||
        strcmp(maneuver, "SHARP_RIGHT") == 0 ||
        strcmp(maneuver, "TURN_SHARP_RIGHT") == 0
    ) {
        o.drawLine(cx - 10, cy + 12, cx - 10, cy - 3);
        o.drawLine(cx - 10, cy - 3, cx + 8, cy - 3);
        o.drawLine(cx + 8, cy - 3, cx + 2, cy - 9);
        o.drawLine(cx + 8, cy - 3, cx + 2, cy + 3);

        if (
            strcmp(maneuver, "SHARP_RIGHT") == 0 ||
            strcmp(maneuver, "TURN_SHARP_RIGHT") == 0
        ) {
            o.drawLine(cx - 6, cy + 12, cx - 6, cy + 1);
        }

    } else if (
        strcmp(maneuver, "KEEP_LEFT") == 0 ||
        strcmp(maneuver, "FORK_LEFT") == 0
    ) {
        o.drawLine(cx, cy + 12, cx, cy - 2);
        o.drawLine(cx, cy - 2, cx - 11, cy - 12);
        o.drawLine(cx - 11, cy - 12, cx - 5, cy - 12);
        o.drawLine(cx - 11, cy - 12, cx - 11, cy - 6);
        o.drawLine(cx, cy - 2, cx + 11, cy - 12);

    } else if (
        strcmp(maneuver, "KEEP_RIGHT") == 0 ||
        strcmp(maneuver, "FORK_RIGHT") == 0
    ) {
        o.drawLine(cx, cy + 12, cx, cy - 2);
        o.drawLine(cx, cy - 2, cx + 11, cy - 12);
        o.drawLine(cx + 11, cy - 12, cx + 5, cy - 12);
        o.drawLine(cx + 11, cy - 12, cx + 11, cy - 6);
        o.drawLine(cx, cy - 2, cx - 11, cy - 12);

    } else if (
        strcmp(maneuver, "ROUNDABOUT_ENTER") == 0 ||
        strcmp(maneuver, "ROUNDABOUT") == 0
    ) {
        
        o.drawCircle(cx, cy, 9);
        o.drawLine(cx - 14, cy + 10, cx - 5, cy + 5);
        o.drawLine(cx - 5, cy + 5, cx - 10, cy + 4);
        o.drawLine(cx - 5, cy + 5, cx - 5, cy + 10);

    } else if (strcmp(maneuver, "ROUNDABOUT_EXIT") == 0) {
        o.drawCircle(cx, cy, 9);
        o.drawLine(cx + 7, cy - 7, cx + 14, cy - 10);
        o.drawLine(cx + 14, cy - 10, cx + 13, cy - 3);
        o.drawLine(cx + 8, cy - 7, cx + 15, cy - 7);
        o.drawLine(cx + 15, cy - 7, cx + 9, cy - 13);
        o.drawLine(cx + 15, cy - 7, cx + 9, cy - 1);

    } else if (strcmp(maneuver, "MERGE") == 0) {
        o.drawLine(cx - 12, cy + 12, cx - 2, cy - 2);
        o.drawLine(cx + 12, cy + 12, cx + 2, cy - 2);
        o.drawLine(cx, cy + 12, cx, cy - 10);
        o.drawLine(cx, cy - 10, cx - 6, cy - 4);
        o.drawLine(cx, cy - 10, cx + 6, cy - 4);

    } else {
        
        o.drawLine(cx, cy + 12, cx, cy - 9);
        o.drawLine(cx, cy - 9, cx - 7, cy - 2);
        o.drawLine(cx, cy - 9, cx + 7, cy - 2);
    }
}


static const char* navFriendlyManeuver(const char* maneuver) {
    if (!maneuver || !maneuver[0]) return "NO INSTRUCTION";
    if (strcmp(maneuver, "UNKNOWN") == 0) return "UNKNOWN TURN";

    if (strcmp(maneuver, "DEPART") == 0) return "START NAVIGATION";

    if (
        strcmp(maneuver, "STRAIGHT") == 0 ||
        strcmp(maneuver, "CONTINUE_STRAIGHT") == 0
    ) return "GO STRAIGHT";

    if (
        strcmp(maneuver, "SLIGHT_LEFT") == 0 ||
        strcmp(maneuver, "TURN_SLIGHT_LEFT") == 0
    ) return "SLIGHT LEFT";

    if (
        strcmp(maneuver, "LEFT") == 0 ||
        strcmp(maneuver, "TURN_LEFT") == 0
    ) return "TURN LEFT";

    if (
        strcmp(maneuver, "SHARP_LEFT") == 0 ||
        strcmp(maneuver, "TURN_SHARP_LEFT") == 0
    ) return "SHARP LEFT";

    if (strcmp(maneuver, "U_TURN_LEFT") == 0) return "U-TURN LEFT";

    if (
        strcmp(maneuver, "SLIGHT_RIGHT") == 0 ||
        strcmp(maneuver, "TURN_SLIGHT_RIGHT") == 0
    ) return "SLIGHT RIGHT";

    if (
        strcmp(maneuver, "RIGHT") == 0 ||
        strcmp(maneuver, "TURN_RIGHT") == 0
    ) return "TURN RIGHT";

    if (
        strcmp(maneuver, "SHARP_RIGHT") == 0 ||
        strcmp(maneuver, "TURN_SHARP_RIGHT") == 0
    ) return "SHARP RIGHT";

    if (strcmp(maneuver, "U_TURN_RIGHT") == 0) return "U-TURN RIGHT";

    if (strcmp(maneuver, "KEEP_LEFT") == 0) return "KEEP LEFT";
    if (strcmp(maneuver, "KEEP_RIGHT") == 0) return "KEEP RIGHT";

    if (strcmp(maneuver, "FORK_LEFT") == 0) return "TAKE LEFT FORK";
    if (strcmp(maneuver, "FORK_RIGHT") == 0) return "TAKE RIGHT FORK";

    if (
        strcmp(maneuver, "ROUNDABOUT_ENTER") == 0 ||
        strcmp(maneuver, "ROUNDABOUT") == 0
    ) return "ENTER ROUNDABOUT";

    if (strcmp(maneuver, "ROUNDABOUT_EXIT") == 0) return "EXIT ROUNDABOUT";

    if (strcmp(maneuver, "MERGE") == 0) return "MERGE";

    if (strcmp(maneuver, "RAMP_LEFT") == 0) return "RAMP LEFT";
    if (strcmp(maneuver, "RAMP_RIGHT") == 0) return "RAMP RIGHT";

    if (strcmp(maneuver, "ARRIVE") == 0) return "ARRIVE";

    if (strcmp(maneuver, "REROUTING") == 0) return "REROUTING";
    if (strcmp(maneuver, "STOPPED") == 0) return "NAV STOPPED";

    return "CONTINUE";
}


static void navDrawManeuverAlert(U8G2& o, const NavigationSnapshot& guidance) {
    if (!guidance.active || !guidance.hasDistanceToManeuver ||
        guidance.distanceToManeuverM > 150U || !guidance.maneuver[0] ||
        strcmp(guidance.maneuver, "UNKNOWN") == 0 || strcmp(guidance.maneuver, "ARRIVE") == 0) return;
    if (((millis() / 450UL) & 1UL) == 0) return;

    o.setFont(CY_FONT_MIN);
    char label[20];
    snprintf(label, sizeof(label), "%s", navFriendlyManeuver(guidance.maneuver));
    const int textWidth = o.getStrWidth(label);
    const int boxWidth = (textWidth + 8 > 62) ? 62 : textWidth + 8;
    const int x = 127 - boxWidth;
    const int y = 50;
    o.setDrawColor(0);
    o.drawBox(x, y, boxWidth, 13);
    o.setDrawColor(1);
    o.drawFrame(x, y, boxWidth, 13);
    cyFit(o, label, boxWidth - 6);
    o.drawStr(x + 3, y + 10, label);
}

void DisplayManager::showNavigationInstructions(const NavigationSnapshot& nav, bool bleConnected, bool mtuCompatible, int batteryPercent) {
    display.clearBuffer();
    navDrawStatus(display, bleConnected, nav.stale || nav.linkLost, nav.linkLost, batteryPercent, "TURN");
    if (!nav.active) {
        const char* message = (bleConnected && !mtuCompatible) ? "MTU TOO SMALL" : (nav.stopped ? "NAVIGATION STOPPED" : "NO ACTIVE ROUTE");
        navDrawNotice(display, message);
        present(); return;
    }
    if (nav.state == NAV_ROUTE_REROUTING) {
        navDrawRerouting(display);
        present(); return;
    }

    
    cyChamferFrame(display, 0, 12, 42, 30, 4);
    navDrawManeuverIcon(display, nav.maneuver, 21, 27);

    display.setFont(CY_FONT_SMALL);
    char label[24];
    snprintf(label, sizeof(label), "%s", navFriendlyManeuver(nav.maneuver));
    cyFit(display, label, 80);
    display.drawStr(47, 24, label);
    drawDashedHLine(display, 47, 28, 78, 2, 3);

    if (nav.hasDistanceToManeuver) {
        char distance[16];
        if (nav.distanceToManeuverM >= 1000) {
            const uint32_t tenths = nav.distanceToManeuverM / 100 + ((nav.distanceToManeuverM % 100) >= 50 ? 1U : 0U);
            snprintf(distance, sizeof(distance), "%lu.%lu km", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
        } else snprintf(distance, sizeof(distance), "%lu m", (unsigned long)nav.distanceToManeuverM);
        display.setFont(u8g2_font_7x14B_tf);
        cyFit(display, distance, 80);
        display.drawStr(47, 40, distance);
    } else {
        display.setFont(CY_FONT_SMALL); display.drawStr(47, 40, "DISTANCE N/A");
    }

    
    drawDashedHLine(display, 0, 43, 128, 2, 3);
    display.setFont(CY_FONT_MIN);
    char road[40]; 
    if (nav.road[0]) snprintf(road, sizeof(road), "ROAD: %s", nav.road);
    else snprintf(road, sizeof(road), "ROAD: N/A");
    navFitUtf8(display, road, 125);
    display.drawStr(2, 52, road);
    char trip[36] = "TRIP: ";
    size_t used = strlen(trip);
    if (nav.hasRemainingDistance) {
        char distance[16];
        if (nav.remainingDistanceM >= 1000) {
            const uint32_t tenths = nav.remainingDistanceM / 100 + ((nav.remainingDistanceM % 100) >= 50 ? 1U : 0U);
            snprintf(distance, sizeof(distance), "%lu.%lu km", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
        } else snprintf(distance, sizeof(distance), "%lu m", (unsigned long)nav.remainingDistanceM);
        strncat(trip, distance, sizeof(trip) - used - 1); used = strlen(trip);
    }
    if (nav.hasRemainingSeconds && used < sizeof(trip) - 1) {
        if (used > 6) strncat(trip, "  ", sizeof(trip) - used - 1);
        const uint32_t minutes = nav.remainingSeconds / 60 + ((nav.remainingSeconds % 60) >= 30 ? 1U : 0U);
        char timeText[12]; snprintf(timeText, sizeof(timeText), "%lum", (unsigned long)minutes);
        strncat(trip, timeText, sizeof(trip) - strlen(trip) - 1);
    }
    if (!nav.hasRemainingDistance && !nav.hasRemainingSeconds) snprintf(trip, sizeof(trip), "TRIP: N/A");
    cyFit(display, trip, 125);
    display.setFont(CY_FONT_MIN); display.drawStr(2, 62, trip);
    
    
    present();
}

void DisplayManager::showNavigationNoRoute(bool bleConnected, bool stopped) {
    NavigationSnapshot empty = {};
    empty.stopped = stopped;
    showNavigationInstructions(empty, bleConnected, true, -1);
}

void DisplayManager::showSoundSettings(int selectedItem, bool master, bool buttons, bool games, uint8_t volume) {
    display.clearBuffer();
    static const char* const labels[] = {"Sound", "Buttons", "Games", "Volume"};
    const char* values[] = {master ? "ON" : "OFF", buttons ? "ON" : "OFF", games ? "ON" : "OFF",
                            volume < 80 ? "LOW" : (volume < 150 ? "MED" : "HIGH")};
    cyList(display, "SOUND", selectedItem, 4, labels, values, nullptr);
    present();
}

void DisplayManager::showDialAppsSettings(int selectedItem, const bool enabled[], const char* const names[], int count) {
    display.clearBuffer();
    
    cyList(display, "DIAL APPS", selectedItem, count, names, nullptr, enabled);
    present();
}




static void cyDigitPair(U8G2& o, int x, int baseline, uint32_t value, int activeDigit) {
    const int cell = o.getStrWidth("0");
    char t[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        t[0] = (char)('0' + (i == 0 ? (value / 10UL) % 10UL : value % 10UL));
        o.drawStr(x + i * cell, baseline, t);
        if (activeDigit == i) o.drawBox(x + i * cell, baseline + 3, cell, 2);
    }
}


void DisplayManager::showAlarm(uint16_t minuteOfDay, bool enabled, uint8_t field, uint8_t tone, bool alert) {
    display.clearBuffer();
    display.setDrawColor(1);
    cyHeader(display, alert ? "ALARM!" : "ALARM", nullptr);

    display.setFont(u8g2_font_logisoso24_tn);
    const int pair = display.getStrWidth("0") * 2;
    const int colon = display.getStrWidth(":");
    const int total = pair * 2 + colon + 6;
    int x = (128 - total) / 2;
    cyDigitPair(display, x, 41, minuteOfDay / 60, field <= 1 ? (int)field : -1);
    x += pair + 3;
    display.drawStr(x, 41, ":");
    x += colon + 3;
    cyDigitPair(display, x, 41, minuteOfDay % 60, (field >= 2 && field <= 3) ? (int)(field - 2) : -1);

    char toneText[10];
    snprintf(toneText, sizeof(toneText), "TONE %u", (unsigned)(tone + 1));
    cyChip(display, 3, 49, 54, enabled ? "ON" : "OFF", field == 4);
    cyChip(display, 71, 49, 54, toneText, field == 5);
    present();
}



void DisplayManager::showTimer(uint32_t remainingMs, bool running, bool completed, uint8_t field, uint8_t tone, bool alert) {
    display.clearBuffer();
    display.setDrawColor(1);
    cyHeader(display, alert ? "DONE!" : "TIMER", (!running && !alert) ? (completed ? "DONE" : "READY") : nullptr);

    const uint32_t sec = remainingMs / 1000UL;
    display.setFont(u8g2_font_logisoso24_tn);
    const int pair = display.getStrWidth("0") * 2;
    const int colon = display.getStrWidth(":");
    const int total = pair * 3 + colon * 2 + 8;
    int x = (128 - total) / 2;
    cyDigitPair(display, x, 41, sec / 3600UL, field <= 1 ? (int)field : -1);
    x += pair + 2;
    display.drawStr(x, 41, ":");
    x += colon + 2;
    cyDigitPair(display, x, 41, (sec / 60UL) % 60UL, (field >= 2 && field <= 3) ? (int)(field - 2) : -1);
    x += pair + 2;
    display.drawStr(x, 41, ":");
    x += colon + 2;
    cyDigitPair(display, x, 41, sec % 60UL, (field >= 4 && field <= 5) ? (int)(field - 4) : -1);

    
    
    const char* leftText = "PRESET";
    if (running) leftText = "RUN";
    else if (remainingMs == 60000UL) leftText = "1 MIN";
    else if (remainingMs == 300000UL) leftText = "5 MIN";
    else if (remainingMs == 600000UL) leftText = "10 MIN";

    char toneText[10];
    snprintf(toneText, sizeof(toneText), "TONE %u", (unsigned)(tone + 1));
    cyChip(display, 3, 49, 54, leftText, !running && field == 6);
    cyChip(display, 71, 49, 54, toneText, field == 7);
    present();
}



void DisplayManager::showLocalAlert(bool alarm, bool timer) {
    display.clearBuffer();
    display.setDrawColor(1);
    const int phase = (int)((millis() / 70UL) % 8UL);
    cyHazard(display, 0, 6, phase);
    cyHazard(display, 58, 6, 7 - phase);

    cyChamferFrame(display, 2, 9, 124, 34, 5);

    
    const int shake = (phase & 1) ? 1 : -1;
    const int cx = 24 + shake, cy = 26;
    if (alarm) {
        display.drawCircle(cx, cy, 10);
        display.drawCircle(cx, cy, 9);
        display.drawLine(cx, cy, cx, cy - 6);
        display.drawLine(cx, cy, cx + 4, cy + 3);
        display.drawLine(cx - 8, cy - 10, cx - 12, cy - 13);
        display.drawLine(cx + 8, cy - 10, cx + 12, cy - 13);
    } else {
        display.drawHLine(cx - 8, cy - 11, 17);
        display.drawHLine(cx - 8, cy + 11, 17);
        display.drawLine(cx - 7, cy - 10, cx, cy);
        display.drawLine(cx + 7, cy - 10, cx, cy);
        display.drawLine(cx - 7, cy + 10, cx, cy);
        display.drawLine(cx + 7, cy + 10, cx, cy);
        display.drawTriangle(cx - 4, cy + 10, cx + 4, cy + 10, cx, cy + 4);
    }

    const char* big = alarm ? "ALARM" : "TIMER";
    const char* sub = (alarm && timer) ? "+ TIMER" : (alarm ? "WAKE UP" : "TIME'S UP");
    display.setFont(u8g2_font_9x15B_tf);
    display.drawStr(84 - display.getStrWidth(big) / 2, 28, big);
    display.setFont(CY_FONT_BODY);
    display.drawStr(84 - display.getStrWidth(sub) / 2, 40, sub);

    cyChip(display, 16, 44, 96, "SELECT = ACK", ((millis() / 400UL) & 1UL) != 0);
    present();
}







void DisplayManager::showCallScreen(const char* name, const char* phone, const char* status,
                                    const char* feedback, bool ringing, bool selectedAccept,
                                    bool bleConnected, uint32_t activeDurationSeconds, uint32_t nowMs) {
    display.clearBuffer();
    display.setDrawColor(1);

    const bool isActive = status && strcmp(status, "ACTIVE") == 0;
    const bool isTerminal = status && (strcmp(status, "ENDED") == 0 ||
        strcmp(status, "REJECTED") == 0 || strcmp(status, "MISSED") == 0);
    const bool isAnswerRequested = status && strcmp(status, "ANSWER_REQUESTED") == 0;
    const bool isRejectRequested = status && strcmp(status, "REJECT_REQUESTED") == 0;
    const bool waiting = isAnswerRequested || isRejectRequested;

    
    
    const uint8_t phase = (uint8_t)((nowMs / 150UL) % 4UL);
    const int radius = 2 + phase; 
    display.drawCircle(10, 8, radius);
    display.drawCircle(10, 8, radius + 1);
    cyNode(display, 118, 7, bleConnected);

    char heading[20];
    const char* headingText = isActive ? "IN CALL" :
        isTerminal ? "CALL STATUS" :
        isAnswerRequested ? "ANSWERING" :
        isRejectRequested ? "REJECTING" : "INCOMING";
    snprintf(heading, sizeof(heading), "%s", headingText);
    display.setFont(CY_FONT_BODY);
    cyFit(display, heading, 84); 
    display.drawStr(24, 11, heading);
    display.drawHLine(0, 15, 128);

    
    
    char caller[40];
    snprintf(caller, sizeof(caller), "%s", (name && name[0]) ? name : "UNKNOWN CALLER");
    display.setFont(u8g2_font_7x13B_tf);
    cyFit(display, caller, 116);
    display.drawStr((128 - display.getStrWidth(caller)) / 2, 28, caller);

    
    
    
    char detail[32];
    const char* detailText = (ringing && feedback && feedback[0]) ? feedback :
        ((phone && phone[0]) ? phone : "NUMBER UNAVAILABLE");
    snprintf(detail, sizeof(detail), "%s", detailText);
    display.setFont(CY_FONT_SMALL);
    cyFit(display, detail, 116);
    display.drawStr((128 - display.getStrWidth(detail)) / 2, 42, detail);

    if (isActive) {
        
        
        char activeLine[32];
        const uint32_t hours = activeDurationSeconds / 3600UL;
        const uint32_t minutes = (activeDurationSeconds / 60UL) % 60UL;
        const uint32_t seconds = activeDurationSeconds % 60UL;
        if (hours > 0) {
            snprintf(activeLine, sizeof(activeLine), "BACK:CLOSE %02lu:%02lu:%02lu",
                     (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
        } else {
            snprintf(activeLine, sizeof(activeLine), "BACK:CLOSE %02lu:%02lu",
                     (unsigned long)minutes, (unsigned long)seconds);
        }
        display.setFont(CY_FONT_SMALL);
        cyFit(display, activeLine, 124);
        display.drawStr((128 - display.getStrWidth(activeLine)) / 2, 60, activeLine);
    } else if (isTerminal) {
        const char* terminalText = strcmp(status, "REJECTED") == 0 ? "REJECTED" :
            strcmp(status, "MISSED") == 0 ? "MISSED" : "CALL ENDED";
        display.setFont(CY_FONT_BODY);
        display.drawStr((128 - display.getStrWidth(terminalText)) / 2, 60, terminalText);
    } else if (waiting) {
        
        
        char waitingLine[32];
        const char* waitingText = (feedback && feedback[0]) ? feedback :
            (isAnswerRequested ? "ANSWERING..." : "REJECTING...");
        snprintf(waitingLine, sizeof(waitingLine), "%s", waitingText);
        display.setFont(CY_FONT_SMALL);
        cyFit(display, waitingLine, 124);
        display.drawStr((128 - display.getStrWidth(waitingLine)) / 2, 60, waitingLine);
    } else if (ringing) {
        
        
        cyChip(display, 3, 49, 58, "ACCEPT", selectedAccept);
        cyChip(display, 67, 49, 58, "REJECT", !selectedAccept);
    }
    present();
}

void DisplayManager::showGameList(int selectedGame, const char* const gameNames[], int gameCount) {
    display.clearBuffer();
    cyList(display, "GAMES", selectedGame, gameCount, gameNames, nullptr, nullptr);
    present();
}

void DisplayManager::showDinoGame(int dinoX, int dinoY, int cactusX, int score, bool gameOver) {
    display.clearBuffer();
    display.setFont(u8g2_font_5x7_tf);
    char scoreText[12];
    snprintf(scoreText, sizeof(scoreText), "S:%d", score);
    display.drawStr(0, 8, scoreText);
    display.drawHLine(0, 50, 128);

    
    display.drawBox(dinoX + 3, dinoY, 6, 5);
    display.drawBox(dinoX, dinoY + 5, 8, 6);
    display.drawPixel(dinoX + 7, dinoY + 2); 
    display.drawBox(dinoX + 1, dinoY + 10, 2, 2);
    display.drawBox(dinoX + 5, dinoY + 10, 2, 2);

    
    display.drawBox(cactusX + 2, 38, 3, 12);
    display.drawBox(cactusX, 41, 2, 4);
    display.drawBox(cactusX + 5, 40, 2, 4);

    if (gameOver) {
        display.drawBox(22, 17, 84, 25);
        display.setDrawColor(0);
        display.setFont(u8g2_font_6x10_tf);
        drawCenteredLabel(display, "GAME OVER", 27);
        char finalScore[18];
        snprintf(finalScore, sizeof(finalScore), "SCORE: %d", score);
        drawCenteredLabel(display, finalScore, 37);
        display.setDrawColor(1);
        display.setFont(u8g2_font_5x7_tf);
        drawCenteredLabel(display, "SELECT: RETRY  HOLD: EXIT", 63);
    } else {
        display.setFont(u8g2_font_5x7_tf);
        drawCenteredLabel(display, "SELECT: JUMP  HOLD: EXIT", 63);
    }
    present();
}


void DisplayManager::showMinesweeper(const bool mines[16], const bool revealed[16], const uint8_t adjacent[16],
                                    uint8_t cursor, uint8_t state, uint8_t safeRevealed) {
    display.clearBuffer();
    display.setFont(u8g2_font_5x7_tf);
    char header[24];
    if (state == 1) snprintf(header, sizeof(header), "MINE HIT!  SELECT RETRY");
    else if (state == 2) snprintf(header, sizeof(header), "BOARD CLEARED!");
    else snprintf(header, sizeof(header), "MINES 3  SAFE %u/13", safeRevealed);
    drawCenteredLabel(display, header, 8);

    const int cell = 10, gap = 2, startX = 41, startY = 12;
    for (uint8_t i = 0; i < 16; ++i) {
        int row = i / 4, col = i % 4;
        int x = startX + col * (cell + gap);
        int y = startY + row * (cell + gap);
        if (revealed[i] || state != 0) {
            if (mines[i]) {
                display.drawFrame(x, y, cell, cell);
                display.drawLine(x + 2, y + 2, x + 7, y + 7);
                display.drawLine(x + 7, y + 2, x + 2, y + 7);
            } else {
                display.drawFrame(x, y, cell, cell);
                if (adjacent[i] > 0) {
                    char digit[2] = {(char)('0' + adjacent[i]), '\0'};
                    display.setFont(u8g2_font_5x7_tf);
                    display.drawStr(x + 3, y + 8, digit);
                } else display.drawPixel(x + 5, y + 5);
            }
        } else {
            display.drawRBox(x, y, cell, cell, 1);
            display.setDrawColor(0);
            display.drawPixel(x + 5, y + 5);
            display.setDrawColor(1);
        }
        if (i == cursor && state == 0) display.drawFrame(x - 1, y - 1, cell + 2, cell + 2);
    }
    display.setFont(u8g2_font_5x7_tf);
    display.setFont(u8g2_font_4x6_tf);
    if (state == 0) drawCenteredLabel(display, "N< B> MOVE  S DIG  HOLD EXIT", 63);
    else drawCenteredLabel(display, "SELECT REPLAY  HOLD EXIT", 63);
    present();
}

void DisplayManager::showMiniRacing(const int lanes[3], const int obstacleLanes[3], const int obstacleY[3],
                                   const bool obstacleActive[3], uint16_t score, bool shieldActive, uint8_t state,
                                   bool hitEffect, bool shieldHitEffect) {
    display.clearBuffer();
    display.setFont(u8g2_font_5x7_tf);
    char header[24];
    snprintf(header, sizeof(header), "S%03u  HP%d  SH%d", score, lanes[1], lanes[2]);
    display.drawStr(2, 8, header);
    if (shieldActive) display.drawDisc(119, 5, 2);
    display.drawHLine(29, 11, 70);
    
    display.drawVLine(30, 13, 43);
    display.drawVLine(52, 13, 43);
    display.drawVLine(75, 13, 43);
    display.drawVLine(97, 13, 43);
    for (int y = 15; y < 56; y += 9) {
        display.drawVLine(63, y, 4);
    }
    const int laneX[3] = {41, 63, 85};
    for (uint8_t i = 0; i < 3; ++i) {
        if (!obstacleActive[i] || obstacleLanes[i] < 0 || obstacleLanes[i] > 2) continue;
        int x = laneX[obstacleLanes[i]] - 4;
        int y = obstacleY[i];
        display.drawFrame(x, y, 9, 8);
        display.drawHLine(x + 2, y + 2, 5);
    }
    int carX = laneX[lanes[0]] - 4;
    if (hitEffect) {
        
        display.drawFrame(carX - 4, 40, 17, 20);
        display.drawLine(carX - 3, 42, carX - 1, 44);
        display.drawLine(carX + 10, 42, carX + 12, 44);
        display.drawLine(carX - 3, 57, carX - 1, 55);
        display.drawLine(carX + 10, 55, carX + 12, 57);
    }
    if (shieldHitEffect) {
        display.drawCircle(carX + 4, 51, 10);
        display.drawCircle(carX + 4, 51, 7);
    }
    display.drawRBox(carX, 45, 9, 12, 2);
    display.drawBox(carX + 2, 42, 5, 4);
    display.setDrawColor(0); display.drawBox(carX + 2, 48, 5, 3); display.setDrawColor(1);
    if (state == 1) {
        display.drawBox(28, 23, 72, 20); display.setDrawColor(0);
        display.setFont(u8g2_font_6x10_tf); drawCenteredLabel(display, "CRASHED!", 36); display.setDrawColor(1);
    }
    display.setFont(u8g2_font_5x7_tf);
    display.setFont(u8g2_font_4x6_tf);
    if (state == 1) drawCenteredLabel(display, "SELECT RETRY  HOLD EXIT", 63);
    else drawCenteredLabel(display, "N< B> MOVE  S SHIELD  HOLD EXIT", 63);
    present();
}

void DisplayManager::showReactionTimer(uint8_t state, uint8_t difficulty, uint8_t round, uint16_t reactionMs,
                                      uint16_t averageMs, uint16_t bestMs, uint8_t falseStarts, bool falseStart) {
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf); drawCenteredLabel(display, "REACTION TEST", 10);
    display.drawHLine(0, 13, 128);
    display.setFont(u8g2_font_5x7_tf);
    const char* levels[3] = {"EASY", "NORMAL", "HARD"};
    if (state == 4) {
        drawCenteredLabel(display, "SESSION COMPLETE", 25);
        char avg[20], best[20], misses[18];
        snprintf(avg, sizeof(avg), "AVG %u ms", averageMs);
        snprintf(best, sizeof(best), "BEST %u ms", bestMs);
        snprintf(misses, sizeof(misses), "FALSE STARTS %u", falseStarts);
        drawCenteredLabel(display, avg, 34); drawCenteredLabel(display, best, 44); drawCenteredLabel(display, misses, 54);
        display.setFont(u8g2_font_4x6_tf); drawCenteredLabel(display, "SELECT REPLAY  HOLD EXIT", 63);
    } else {
        char roundText[18]; snprintf(roundText, sizeof(roundText), "ROUND %u/3  %s", round, levels[difficulty]);
        drawCenteredLabel(display, roundText, 23);
        if (state == 0) {
            display.setFont(u8g2_font_helvB10_tf); drawCenteredLabel(display, "TAP TO START", 43);
            display.setFont(u8g2_font_4x6_tf); drawCenteredLabel(display, "N< LEVEL  SEL ARM  B> LEVEL", 63);
        } else if (state == 1) {
            display.setFont(u8g2_font_logisoso24_tn); drawCenteredLabel(display, "WAIT", 47);
            display.setFont(u8g2_font_5x7_tf); drawCenteredLabel(display, "DON'T TAP YET!", 63);
        } else if (state == 2) {
            display.setFont(u8g2_font_logisoso24_tn); drawCenteredLabel(display, "GO!", 48);
            display.setFont(u8g2_font_5x7_tf); drawCenteredLabel(display, "TAP SELECT NOW", 63);
        } else {
            if (falseStart) {
                display.setFont(u8g2_font_helvB10_tf); drawCenteredLabel(display, "TOO EARLY!", 43);
            } else {
                char value[16]; snprintf(value, sizeof(value), "%u ms", reactionMs);
                display.setFont(u8g2_font_logisoso20_tn); drawCenteredLabel(display, value, 47);
            }
            display.setFont(u8g2_font_4x6_tf);
            drawCenteredLabel(display, round >= 3 ? "SELECT SUMMARY  HOLD EXIT" : "SELECT NEXT ROUND  HOLD EXIT", 63);
        }
    }
    present();
}

void DisplayManager::showSpaceInvaders(const bool invaders[8], int16_t baseX, int16_t baseY, int16_t playerX,
                                      bool playerBulletActive, int16_t playerBulletX, int16_t playerBulletY,
                                      bool enemyBulletActive, int16_t enemyBulletX, int16_t enemyBulletY,
                                      uint16_t score, uint8_t lives, uint8_t wave, uint8_t state) {
    display.clearBuffer(); display.setFont(u8g2_font_5x7_tf);
    char header[20]; snprintf(header, sizeof(header), "S%u W%u HP%u", score, wave, lives);
    display.drawStr(2, 8, header); display.drawHLine(0, 11, 128);
    for (uint8_t i = 0; i < 8; ++i) {
        if (!invaders[i]) continue;
        int x = baseX + (i % 4) * 17, y = baseY + (i / 4) * 9;
        display.drawFrame(x, y, 9, 6);
        display.drawPixel(x + 2, y + 2); display.drawPixel(x + 6, y + 2);
        display.drawHLine(x + 2, y + 5, 5);
    }
    if (playerBulletActive) display.drawBox(playerBulletX, playerBulletY, 2, 4);
    if (enemyBulletActive) { display.drawPixel(enemyBulletX, enemyBulletY); display.drawPixel(enemyBulletX, enemyBulletY + 1); }
    display.drawBox(playerX + 2, 54, 7, 4); display.drawBox(playerX + 4, 51, 3, 4);
    display.drawPixel(playerX + 1, 57); display.drawPixel(playerX + 9, 57);
    if (state != 0) {
        display.drawBox(25, 24, 78, 18); display.setDrawColor(0); display.setFont(u8g2_font_6x10_tf);
        drawCenteredLabel(display, state == 2 ? "SECTOR CLEAR" : "SHIP DESTROYED", 36); display.setDrawColor(1);
    }
    display.setFont(u8g2_font_5x7_tf);
    display.setFont(u8g2_font_4x6_tf);
    drawCenteredLabel(display, state == 0 ? "N< B> MOVE  S FIRE  HOLD EXIT" : "SELECT RESTART  HOLD EXIT", 63);
    present();
}

void DisplayManager::showBrickBreaker(const bool bricks[3][10], int16_t paddleX, int ballX, int ballY, bool launched,
                                     uint16_t score, uint8_t lives, uint8_t level, uint8_t state) {
    display.clearBuffer(); display.setFont(u8g2_font_5x7_tf);
    char header[24]; snprintf(header, sizeof(header), "S%03u  L%u  BALLS %u", score, level, lives);
    display.drawStr(2, 8, header); display.drawHLine(0, 11, 128);
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < 10; ++col) {
            if (bricks == nullptr || !bricks[row][col]) continue;
            int x = 4 + col * 12, y = 14 + row * 7;
            display.drawBox(x, y, 10, 4);
            if ((row + col) % 2 == 0) display.drawPixel(x + 5, y + 1);
        }
    }
    display.drawRBox(paddleX, 57, 24, 3, 1);
    display.drawDisc(ballX, ballY, 1);
    (void)launched;
    if (state != 0) {
        display.drawBox(25, 26, 78, 18); display.setDrawColor(0); display.setFont(u8g2_font_6x10_tf);
        drawCenteredLabel(display, state == 2 ? "LEVELS CLEARED" : "GAME OVER", 38); display.setDrawColor(1);
    }
    display.setFont(u8g2_font_4x6_tf);
    if (state != 0) drawCenteredLabel(display, "SELECT REPLAY  HOLD EXIT", 63);
    else drawCenteredLabel(display, launched ? "N< B> PADDLE  HOLD EXIT" : "N< B> PADDLE  SELECT LAUNCH", 63);
    present();
}

void DisplayManager::showWeatherWindow(int windowIndex, const WatchData& data) {
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredLabel(display, "WEATHER", 10);
    display.drawHLine(0, 14, 128);

    switch (windowIndex) {
        case 0: {
            display.setFont(u8g2_font_6x10_tf);
            drawCenteredLabel(display, "TEMPERATURE", 27);
            display.setFont(u8g2_font_logisoso24_tn);
            char value[10];
            if (data.weather[0] == '\0' || strcmp(data.weather, "NO DATA") == 0)
                snprintf(value, sizeof(value), "--");
            else
                snprintf(value, sizeof(value), "%d", data.temperature);
            display.drawStr(20, 55, value);
            display.setFont(u8g2_font_6x10_tf);
            display.drawStr(67, 45, "C");
            display.setFont(u8g2_font_5x7_tf);
            display.drawStr(76, 55, data.weather);
            break;
        }
        case 1: {
            display.setFont(u8g2_font_6x10_tf);
            drawCenteredLabel(display, "HUMIDITY", 27);
            display.setFont(u8g2_font_logisoso24_tn);
            char value[8];
            snprintf(value, sizeof(value), "%u", data.humidity);
            display.drawStr(20, 55, value);
            display.setFont(u8g2_font_6x10_tf);
            display.drawStr(65, 45, "%");
            display.drawFrame(78, 35, 44, 12);
            display.drawBox(80, 37, map(constrain((int)data.humidity, 0, 100), 0, 100, 0, 40), 8);
            break;
        }
        case 2: {
            display.setFont(u8g2_font_6x10_tf);
            drawCenteredLabel(display, "AIR QUALITY INDEX", 27);
            display.setFont(u8g2_font_logisoso24_tn);
            char value[10];
            if (data.aqi < 0) snprintf(value, sizeof(value), "--");
            else snprintf(value, sizeof(value), "%d", data.aqi);
            drawCenteredLabel(display, value, 49);
            display.setFont(u8g2_font_6x10_tf);
            
            drawCenteredLabel(display, data.aqiStatus, 62);
            break;
        }
        case 3: {
            display.setFont(u8g2_font_6x10_tf);
            drawCenteredLabel(display, "PRECIPITATION", 27);
            display.setFont(u8g2_font_logisoso24_tn);
            char value[8];
            snprintf(value, sizeof(value), "%u", data.precipitationChance);
            display.drawStr(18, 55, value);
            display.setFont(u8g2_font_6x10_tf);
            display.drawStr(62, 45, "%");
            display.setFont(u8g2_font_5x7_tf);
            display.drawStr(72, 55, data.precipitation);
            break;
        }
        default:
            break;
    }

    display.drawHLine(0, 62, 128);
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(0, 63, "SELECT: NEXT");
    present();
}

void DisplayManager::showStepsWindow(int windowIndex, const WatchData& data, bool hasData) {
    display.clearBuffer();

    
    
    const uint8_t page = (uint8_t)((windowIndex < 0 ? 0 : windowIndex) % 3);
    display.setFont(u8g2_font_6x10_tf);

    if (page == 0) {
        
        display.drawStr(3, 10, "ACTIVITY");
        display.setFont(u8g2_font_4x6_tf);
        display.drawStr(99, 9, "01 / 03");
        display.drawHLine(0, 13, 128);

        display.setFont(u8g2_font_5x7_tf);
        display.drawStr(4, 21, "TODAY'S STEPS");
        char goalText[16];
        if (hasData) snprintf(goalText, sizeof(goalText), "GOAL %u%%", (unsigned)data.goalPercent);
        else snprintf(goalText, sizeof(goalText), "NO SYNC");
        int goalWidth = display.getStrWidth(goalText);
        display.drawStr(124 - goalWidth, 21, goalText);

        char stepsText[16];
        if (hasData && data.steps >= 0) snprintf(stepsText, sizeof(stepsText), "%ld", (long)data.steps);
        else snprintf(stepsText, sizeof(stepsText), "--");
        display.setFont(u8g2_font_logisoso18_tn);
        display.drawStr(3, 42, stepsText);

        
        
        const int gx = 5, gy = 46, gw = 118, gb = 57;
        drawDashedHLine(display, gx, gy, gw, 2, 4);
        drawDashedHLine(display, gx, (gy + gb) / 2, gw, 2, 4);
        drawDashedHLine(display, gx, gb, gw, 2, 4);

        int32_t maxSteps = data.dailyGoal > 0 ? data.dailyGoal : 10000;
        for (uint8_t i = 0; i < 7; ++i) {
            if (data.stepHistory[i] > maxSteps) maxSteps = data.stepHistory[i];
        }
        if (maxSteps < 1000) maxSteps = 1000;

        int px[7], py[7];
        for (uint8_t i = 0; i < 7; ++i) {
            px[i] = gx + 3 + ((int)i * (gw - 6)) / 6;
            const int32_t value = clampStepValue(data.stepHistory[i], maxSteps);
            py[i] = gb - (int)(((int64_t)value * (gb - gy - 2)) / maxSteps);
            if (py[i] < gy + 1) py[i] = gy + 1;
        }
        if (hasData) {
            for (uint8_t i = 0; i < 6; ++i) display.drawLine(px[i], py[i], px[i + 1], py[i + 1]);
            for (uint8_t i = 0; i < 7; ++i) {
                if (i == 6) {
                    display.drawCircle(px[i], py[i], 3);
                    display.drawDisc(px[i], py[i], 1);
                } else {
                    display.drawDisc(px[i], py[i], 1);
                }
            }
        } else {
            display.setFont(u8g2_font_4x6_tf);
            display.drawStr(43, 53, "WAITING FOR PHONE");
        }

        for (uint8_t i = 0; i < 7; ++i) drawHistoryDayLabel(display, data, i, px[i], 63);

    } else if (page == 1) {
        
        display.drawStr(3, 10, "WEEKLY ACTIVITY");
        display.setFont(u8g2_font_4x6_tf);
        display.drawStr(99, 9, "02 / 03");
        display.drawHLine(0, 13, 128);

        int32_t maxSteps = data.dailyGoal > 0 ? data.dailyGoal : 10000;
        for (uint8_t i = 0; i < 7; ++i) {
            if (data.stepHistory[i] > maxSteps) maxSteps = data.stepHistory[i];
        }
        if (maxSteps < 3000) maxSteps = 3000;
        
        maxSteps = ((maxSteps + 999) / 1000) * 1000;
        if (maxSteps > 200000) maxSteps = 200000;

        const int chartLeft = 19, chartRight = 125, chartTop = 21, chartBottom = 54;
        const int chartHeight = chartBottom - chartTop;
        for (int level = 0; level < 4; ++level) {
            const int y = chartTop + (chartHeight * level) / 3;
            drawDashedHLine(display, chartLeft, y, chartRight - chartLeft + 1, 2, 4);
        }

        
        display.setFont(u8g2_font_4x6_tf);
        for (int level = 0; level < 4; ++level) {
            const int32_t tickValue = (maxSteps * (3 - level)) / 3;
            char tick[8];
            if (tickValue == 0) snprintf(tick, sizeof(tick), "0");
            else snprintf(tick, sizeof(tick), "%ldk", (long)((tickValue + 500) / 1000));
            const int y = chartTop + (chartHeight * level) / 3;
            display.drawStr(0, y + 2, tick);
        }

        for (uint8_t i = 0; i < 7; ++i) {
            const int centerX = chartLeft + 7 + (int)i * 15;
            const int barWidth = 8;
            const int32_t value = hasData ? clampStepValue(data.stepHistory[i], maxSteps) : 0;
            int barHeight = (int)(((int64_t)value * (chartHeight - 2)) / maxSteps);
            if (hasData && value > 0 && barHeight < 2) barHeight = 2;
            const int barTop = chartBottom - barHeight;
            if (hasData && value > 0) {
                if (i == 6) {
                    
                    display.drawBox(centerX - barWidth / 2, barTop, barWidth, barHeight);
                    display.drawHLine(centerX - 2, barTop - 2, 5);
                } else {
                    
                    if (barHeight >= 3) {
                        display.drawFrame(centerX - barWidth / 2, barTop, barWidth, barHeight);
                        for (int y = barTop + 2; y < chartBottom - 1; y += 3)
                            display.drawHLine(centerX - 1, y, 3);
                    } else {
                        display.drawVLine(centerX, barTop, barHeight);
                    }
                }
            }
            drawHistoryDayLabel(display, data, i, centerX, 63);
        }

        if (!hasData) {
            display.setFont(u8g2_font_4x6_tf);
            display.drawStr(40, 38, "NO ACTIVITY DATA");
        }

    } else {
        
        display.drawStr(3, 10, "GOAL TARGET");
        display.setFont(u8g2_font_4x6_tf);
        display.drawStr(99, 9, "03 / 03");
        display.drawHLine(0, 13, 128);

        display.setFont(u8g2_font_5x7_tf);
        display.drawStr(5, 24, "DAILY COMPLETION");
        char progressText[8];
        snprintf(progressText, sizeof(progressText), "%u", hasData ? (unsigned)data.goalPercent : 0U);
        display.setFont(u8g2_font_logisoso24_tn);
        display.drawStr(8, 53, progressText);
        const int progressWidth = display.getStrWidth(progressText);
        display.setFont(u8g2_font_6x10_tf);
        display.drawStr(12 + progressWidth, 48, "%");

        char targetText[24];
        if (hasData) {
            const long goalThousands = (long)((data.dailyGoal + 500) / 1000);
            snprintf(targetText, sizeof(targetText), "TARGET %ldK", goalThousands);
        } else {
            snprintf(targetText, sizeof(targetText), "PHONE SYNC");
        }
        display.setFont(u8g2_font_5x7_tf);
        display.drawStr(70, 38, targetText);

        const int progress = hasData ? constrain((int)data.goalPercent, 0, 100) : 0;
        display.drawFrame(8, 57, 112, 6);
        if (progress > 0) display.drawBox(10, 59, map(progress, 0, 100, 0, 108), 2);
    }

    present();
}

void DisplayManager::showSystemWindow(int windowIndex, bool bleConnected, bool phoneDataFresh) {

    display.clearBuffer();

    
    display.setFont(u8g2_font_6x10_tf);
    drawCenteredLabel(display, "SYSTEM", 10);

    display.drawHLine(0, 14, 128);

    switch (windowIndex) {

        

        case 0:

            display.setFont(u8g2_font_6x10_tf);
            drawCenteredLabel(display, "DEVICE INFO", 27);

            display.setFont(u8g2_font_helvB10_tf);
            display.drawStr(5, 43, "CYBERNODE S3");

            display.setFont(u8g2_font_6x10_tf);
            display.drawStr(25, 56, "ESP32 MCU");

            break;

        
        
        case 1:

            display.setFont(u8g2_font_6x10_tf);
            drawCenteredLabel(display, "HARDWARE INFO", 27);

            display.drawStr(2, 40, "MCU: ESP32");
            display.drawStr(2, 50, "OLED: SH1106");
            display.drawStr(2, 60, "3 BUTTONS");

            break;

        

          case 2: {

              display.setFont(u8g2_font_6x10_tf);
              drawCenteredLabel(display, "BLE STATUS", 27);

              
              if (bleConnected) {
                  display.drawStr(15, 43, "BLE: CONNECTED");
              } else {
                  display.drawStr(15, 43, "BLE: DISCONNECTED");
              }

              
              if (bleConnected && phoneDataFresh) {
                  display.drawStr(15, 56, "DATA: LIVE");
              } else if (bleConnected && !phoneDataFresh) {
                  display.drawStr(15, 56, "DATA: STALE");
              } else {
                  display.drawStr(15, 56, "PHONE: NOT LINKED");
              }

              break;
          }


        default:
            break;
    }

    display.drawHLine(0, 63, 128);

    present();
}


void DisplayManager::captureTransitionSourceFrame() {
    memcpy(transitionSourceBuffer, display.getBufferPtr(), sizeof(transitionSourceBuffer));
}


void DisplayManager::captureTransitionFrame() {
    memcpy(transitionBaseBuffer, display.getBufferPtr(), sizeof(transitionBaseBuffer));
}





void DisplayManager::drawTransitionMask(int revealWidth, int direction) {
    revealWidth = constrain(revealWidth, 0, 128);
    uint8_t* buffer = display.getBufferPtr();

    const bool radialForward = direction == TRANSITION_RADIAL_REVEAL;
    const bool radialReverse = direction == TRANSITION_RADIAL_REVEAL_REVERSE;
    const bool orbitNext = direction == TRANSITION_CAROUSEL_NEXT;
    const bool orbitPrevious = direction == TRANSITION_CAROUSEL_PREVIOUS;

    if (radialForward || radialReverse || orbitNext || orbitPrevious) {
        
        
        
        const int progress = revealWidth;
        const int directionSign = orbitNext ? 1 : (orbitPrevious ? -1 : 0);
        const int centreOffset = directionSign * ((128 - progress) * 16 / 128);
        const int centreX = 64 + centreOffset;
        const int centreY = 32;
        const int maxRadius = (orbitNext || orbitPrevious) ? 92 : 90;
        const int radius = (progress * maxRadius) / 128;
        const int boundary = radialReverse ? (maxRadius - radius) : radius;
        const int phase = orbitNext ? 0 : (orbitPrevious ? 5 : 2);

        for (int y = 0; y < 64; ++y) {
            const int dy = abs(y - centreY);
            const int pageOffset = (y >> 3) * 128;
            const uint8_t bit = (uint8_t)(1U << (y & 7));
            for (int x = 0; x < 128; ++x) {
                const int dx = abs(x - centreX);
                const int hi = dx > dy ? dx : dy;
                const int lo = dx > dy ? dy : dx;
                const int octagonalDistance = hi + (lo * 414) / 1000;

                bool revealDestination;
                if (radialReverse) revealDestination = octagonalDistance >= boundary;
                else revealDestination = octagonalDistance <= radius;
                if (progress >= 128) revealDestination = true;

                const uint8_t* chosen = revealDestination ? transitionBaseBuffer : transitionSourceBuffer;
                bool pixel = (chosen[pageOffset + x] & bit) != 0;

                
                
                if (progress > 0 && progress < 128 && abs(octagonalDistance - boundary) <= 1) {
                    const int segment = (x + (y * 2) + phase) % 17;
                    if (segment < 11) pixel = true;
                }

                uint8_t& destinationByte = buffer[pageOffset + x];
                if (pixel) destinationByte |= bit;
                else destinationByte &= (uint8_t)~bit;
            }
        }

        
        
        if (progress > 8 && progress < 128) {
            const int markRadius = radialReverse ? boundary : radius;
            const int points[4][2] = {
                {centreX, centreY - markRadius},
                {centreX + markRadius, centreY},
                {centreX, centreY + markRadius},
                {centreX - markRadius, centreY}
            };
            for (uint8_t i = 0; i < 4; ++i) {
                const int px = points[i][0];
                const int py = points[i][1];
                for (int k = -1; k <= 1; ++k) {
                    const int tx = (i == 0 || i == 2) ? px + k : px;
                    const int ty = (i == 1 || i == 3) ? py + k : py;
                    if (tx < 0 || tx >= 128 || ty < 0 || ty >= 64) continue;
                    buffer[(ty >> 3) * 128 + tx] |= (uint8_t)(1U << (ty & 7));
                }
            }
        }

        present();
        return;
    }

    
    for (int x = 0; x < 128; ++x) {
        bool revealDestination = false;
        if (direction == TRANSITION_FROM_LEFT) revealDestination = (x < revealWidth);
        else if (direction == TRANSITION_FROM_RIGHT) revealDestination = (x >= (128 - revealWidth));
        else {
            const int halfWidth = revealWidth / 2;
            revealDestination = (x >= (64 - halfWidth) && x < (64 + halfWidth));
            if (revealWidth >= 128) revealDestination = true;
        }
        const uint8_t* chosen = revealDestination ? transitionBaseBuffer : transitionSourceBuffer;
        for (int page = 0; page < 8; ++page) buffer[page * 128 + x] = chosen[page * 128 + x];
    }
    present();
}
void DisplayManager::showNotifications(int selected, const WatchData& data) {
    display.clearBuffer();
    display.setDrawColor(1);
    char counter[16];
    snprintf(counter, sizeof(counter), "%u/%u", data.notificationCount ? selected + 1 : 0, data.notificationCount);
    cyHeader(display, "ALERTS", counter);

    if (!data.notificationCount) {
        display.drawFrame(50, 18, 28, 16);
        display.drawLine(50, 18, 64, 28);
        display.drawLine(78, 18, 64, 28);
        display.setFont(CY_FONT_BODY);
        drawCenteredLabel(display, "NO ALERTS", 47);
        display.setFont(CY_FONT_SMALL);
        drawCenteredLabel(display, "Waiting for phone", 61);
        present();
        return;
    }

    if (selected < 0) selected = 0;
    if (selected >= data.notificationCount) selected = data.notificationCount - 1;
    const WatchNotification& n = data.notifications[selected];

    
    cyChamferFrame(display, 0, 16, 122, 47, 4);
    display.setDrawColor(1);
    cyChamferBox(display, 0, 16, 122, 15, 4);
    display.setDrawColor(0);
    display.setFont(CY_FONT_BODY);
    char app[16];
    snprintf(app, sizeof(app), "%.12s", n.app[0] ? n.app : "MESSAGE");
    cyFit(display, app, 70);
    display.drawStr(6, 28, app);
    char clock[6];
    if (cyExtractClock(n.timestamp, clock)) {
        display.setFont(CY_FONT_SMALL);
        display.drawStr(117 - display.getStrWidth(clock), 28, clock);
    }
    display.setDrawColor(1);

    display.setFont(CY_FONT_BODY);
    char title[40];
    snprintf(title, sizeof(title), "%.30s", n.title[0] ? n.title : n.content);
    cyFit(display, title, 110);
    display.drawStr(6, 46, title);

    display.setFont(CY_FONT_SMALL);
    char preview[40];
    snprintf(preview, sizeof(preview), "%.34s", n.title[0] ? n.content : "");
    cyFit(display, preview, 110);
    display.drawStr(6, 59, preview);

    cyScroll(display, 124, 16, 47, selected, data.notificationCount);
    present();
}

void DisplayManager::showNotificationPopup(int selected, const WatchData& data) {
    display.clearBuffer();
    display.setDrawColor(1);
    cyChamferFrame(display, 1, 1, 126, 62, 5);
    cyChamferBox(display, 2, 2, 124, 14, 3);
    display.setDrawColor(0);
    display.setFont(CY_FONT_BODY);
    display.drawStr(8, 13, "NEW NOTIFICATION");
    display.setDrawColor(1);

    if (!data.notificationCount || selected < 0 || selected >= data.notificationCount) {
        display.setFont(CY_FONT_BODY);
        drawCenteredLabel(display, "NO DETAILS", 42);
        present();
        return;
    }

    const WatchNotification& n = data.notifications[selected];
    char app[24];
    snprintf(app, sizeof(app), "%.20s", n.app[0] ? n.app : "MESSAGE");
    display.setFont(CY_FONT_SMALL);
    cyFit(display, app, 116);
    display.drawStr(6, 28, app);

    char title[40];
    snprintf(title, sizeof(title), "%.36s", n.title[0] ? n.title : n.content);
    display.setFont(CY_FONT_BODY);
    cyFit(display, title, 116);
    display.drawStr(6, 44, title);

    char preview[40];
    snprintf(preview, sizeof(preview), "%.36s", n.title[0] ? n.content : "");
    display.setFont(CY_FONT_SMALL);
    cyFit(display, preview, 116);
    display.drawStr(6, 59, preview);
    present();
}

void DisplayManager::showNotificationDetail(int selected, const WatchData& data, int scrollLine) {
    display.clearBuffer();
    display.setDrawColor(1);
    if (!data.notificationCount || selected < 0 || selected >= data.notificationCount) {
        cyHeader(display, "ALERT", nullptr);
        display.setFont(CY_FONT_BODY);
        drawCenteredLabel(display, "NOT FOUND", 40);
        present();
        return;
    }
    const WatchNotification& n = data.notifications[selected];

    const int charsPerLine = 19;
    int lineStart[24], lineLen[24];
    const int totalLines = cyWrap(n.content, charsPerLine, lineStart, lineLen, 24);
    if (scrollLine < 0) scrollLine = 0;
    if (scrollLine > totalLines - 1) scrollLine = totalLines - 1;

    char app[12];
    snprintf(app, sizeof(app), "%.9s", n.app[0] ? n.app : "ALERT");
    char clock[6];
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%d/%d", scrollLine + 1, totalLines);
    cyHeader(display, app, cyExtractClock(n.timestamp, clock) ? clock : pageText);

    display.setFont(CY_FONT_BODY);
    char title[36];
    snprintf(title, sizeof(title), "%.30s", n.title);
    cyFit(display, title, 118);
    display.drawStr(3, 26, title);
    drawDashedHLine(display, 3, 29, 118, 3, 2);

    display.setFont(CY_FONT_MIN);
    for (int row = 0; row < 3; ++row) {
        const int line = scrollLine + row;
        if (line >= totalLines) break;
        char lineText[24];
        int amount = lineLen[line];
        if (amount > 22) amount = 22;
        memcpy(lineText, n.content + lineStart[line], amount);
        lineText[amount] = '\0';
        display.drawStr(3, 41 + row * 10, lineText);
    }
    cyScroll(display, 124, 31, 32, scrollLine, totalLines);
    present();
}

void DisplayManager::showStopwatch(uint32_t elapsedMs, bool running, uint8_t lapCount, const uint32_t laps[2]) {
    display.clearBuffer();
    display.setDrawColor(1);
    cyHeader(display, "STOPWATCH", running ? "RUN" : "PAUSE");

    uint32_t cs = (elapsedMs / 10) % 100, sec = (elapsedMs / 1000) % 60, min = (elapsedMs / 60000) % 100;
    char timeText[16];
    snprintf(timeText, sizeof(timeText), "%02lu:%02lu.%02lu", (unsigned long)min, (unsigned long)sec, (unsigned long)cs);
    display.setFont(u8g2_font_logisoso24_tn);
    drawCenteredLabel(display, timeText, 41);

    display.setFont(CY_FONT_MIN);
    for (uint8_t i = 0; i < 2; ++i) {
        char t[24];
        if (i < lapCount) {
            const uint32_t l = laps[i];
            snprintf(t, sizeof(t), "LAP%u %02lu:%02lu.%02lu", (unsigned)(i + 1),
                     (unsigned long)((l / 60000) % 100), (unsigned long)((l / 1000) % 60), (unsigned long)((l / 10) % 100));
        } else {
            snprintf(t, sizeof(t), "LAP%u --:--.--", (unsigned)(i + 1));
        }
        display.drawStr(3, 52 + i * 10, t);
    }
    present();
}
