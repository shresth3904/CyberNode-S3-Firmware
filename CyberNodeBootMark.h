#ifndef CYBERNODE_BOOT_ANIMATION_H
#define CYBERNODE_BOOT_ANIMATION_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <string.h>
#include "DisplayManager.h"
#include "SoundManager.h"
#include "HapticManager.h"
#include "SettingsConfig.h"
#include "CyberNodeBootMark.h"

#ifndef pgm_read_byte
#define pgm_read_byte(address) (*(const uint8_t *)(address))
#endif

namespace CyberNodeBoot {


static const uint8_t SRC_W = CYBERNODE_S3_MARK_WIDTH;                 
static const uint8_t SRC_H = CYBERNODE_S3_MARK_HEIGHT;                
static const uint8_t SRC_BYTES_PER_ROW = CYBERNODE_S3_MARK_BYTES_PER_ROW;


static const uint8_t DRAW_W = 64;
static const uint8_t DRAW_H = 52;
static const uint8_t DRAW_X = (128 - DRAW_W) / 2;                     
static const uint8_t DRAW_Y = (64 - DRAW_H) / 2;                      
static const uint8_t DRAW_BYTES_PER_ROW = (DRAW_W + 7) / 8;           


static const char WORDMARK[] = "CYBERNODE";
static const uint8_t WORDMARK_LEN = 9;
static const uint8_t WORDMARK_ADVANCE = 9;
static const uint8_t WORDMARK_WIDTH = WORDMARK_LEN * WORDMARK_ADVANCE - 2;  

class Animation {
public:
    
    
    
    static void run(DisplayManager& displayManager,
                    SoundManager& soundManager,
                    HapticManager& hapticManager,
                    uint8_t buzzerPin,
                    uint8_t animationStyle) {
        soundManager.stop();
        buildScaledLogo();
        const bool aperture =
            (animationStyle == CyberNodeSettings::BOOT_LOGO_ANIMATION_GLITCH);

        
        drawBlank(displayManager);
        serviceFor(hapticManager, 120);

        
        playTick(buzzerPin, soundManager);
        if (aperture) {
            for (uint8_t step = 1; step <= 8; ++step) {
                drawHairlineFrame(displayManager, step, 8);
                serviceFor(hapticManager, 20);
            }
            for (uint8_t step = 1; step <= 14; ++step) {
                drawApertureFrame(displayManager, step, 14);
                serviceFor(hapticManager, 22);
            }
        } else {
            for (uint8_t step = 1; step <= 16; ++step) {
                drawWipeFrame(displayManager, step, 16);
                serviceFor(hapticManager, 20);
            }
        }

        
        drawLogoFrame(displayManager, 16);
        hapticManager.trigger(HapticEvent::BOOT);
        playChime(buzzerPin, soundManager, hapticManager);
        serviceFor(hapticManager, 260);

        
        for (int8_t level = 14; level >= 0; level -= 2) {
            drawLogoFrame(displayManager, (uint8_t)level);
            serviceFor(hapticManager, 28);
        }
        drawBlank(displayManager);
        serviceFor(hapticManager, 80);

        
        for (uint8_t chars = 1; chars <= WORDMARK_LEN; ++chars) {
            drawWordmarkFrame(displayManager, chars, false);
            serviceFor(hapticManager, 45);
        }
        serviceFor(hapticManager, 160);
        drawWordmarkFrame(displayManager, WORDMARK_LEN, true);

        
        serviceFor(hapticManager, 1100);
        noTone(buzzerPin);
        soundManager.stop();
    }

private:
    struct ToneStep {
        uint16_t frequencyHz;
        uint16_t durationMs;
        uint16_t gapMs;
    };

    

    
    static void playTick(uint8_t buzzerPin, SoundManager& sound) {
        if (!sound.isEnabled()) return;
        tone(buzzerPin, 2093, 6);
    }

    
    static void playChime(uint8_t buzzerPin, SoundManager& sound,
                          HapticManager& haptics) {
        static const ToneStep cue[] = {{1319, 60, 30}, {1976, 150, 0}};
        if (!sound.isEnabled()) return;
        for (uint8_t i = 0; i < (uint8_t)(sizeof(cue) / sizeof(cue[0])); ++i) {
            tone(buzzerPin, cue[i].frequencyHz, cue[i].durationMs);
            serviceFor(haptics, cue[i].durationMs);
            noTone(buzzerPin);
            if (cue[i].gapMs) serviceFor(haptics, cue[i].gapMs);
        }
        noTone(buzzerPin);
    }

    

    static uint8_t* scaledBuf() {
        static uint8_t buf[DRAW_H * DRAW_BYTES_PER_ROW];
        return buf;
    }

    static bool srcPixel(uint8_t x, uint8_t y) {
        const uint16_t index = (uint16_t)y * SRC_BYTES_PER_ROW + (x >> 3);
        const uint8_t packed = pgm_read_byte(&CYBERNODE_S3_MARK_XBM[index]);
        return (packed & (uint8_t)(1U << (x & 7U))) != 0;
    }

    
    
    static void buildScaledLogo() {
        uint8_t* buf = scaledBuf();
        memset(buf, 0, DRAW_H * DRAW_BYTES_PER_ROW);
        for (uint8_t dy = 0; dy < DRAW_H; ++dy) {
            const uint8_t sy0 = (uint8_t)((uint16_t)dy * SRC_H / DRAW_H);
            uint8_t sy1 = (uint8_t)(((uint16_t)(dy + 1) * SRC_H + DRAW_H - 1) / DRAW_H);
            if (sy1 > SRC_H) sy1 = SRC_H;
            for (uint8_t dx = 0; dx < DRAW_W; ++dx) {
                const uint8_t sx0 = (uint8_t)((uint16_t)dx * SRC_W / DRAW_W);
                uint8_t sx1 = (uint8_t)(((uint16_t)(dx + 1) * SRC_W + DRAW_W - 1) / DRAW_W);
                if (sx1 > SRC_W) sx1 = SRC_W;
                uint16_t lit = 0, area = 0;
                for (uint8_t y = sy0; y < sy1; ++y) {
                    for (uint8_t x = sx0; x < sx1; ++x) {
                        ++area;
                        if (srcPixel(x, y)) ++lit;
                    }
                }
                if (area && (lit * 2U >= area)) {
                    buf[(uint16_t)dy * DRAW_BYTES_PER_ROW + (dx >> 3)] |=
                        (uint8_t)(1U << (dx & 7U));
                }
            }
        }
    }

    static bool logoPixel(uint8_t x, uint8_t y) {
        if (x >= DRAW_W || y >= DRAW_H) return false;
        return (scaledBuf()[(uint16_t)y * DRAW_BYTES_PER_ROW + (x >> 3)] &
                (uint8_t)(1U << (x & 7U))) != 0;
    }

    
    
    static bool ditherPass(uint8_t x, uint8_t y, uint8_t level) {
        static const uint8_t bayer[4][4] = {
            { 0,  8,  2, 10},
            {12,  4, 14,  6},
            { 3, 11,  1,  9},
            {15,  7, 13,  5}
        };
        return bayer[y & 3U][x & 3U] < level;
    }

    
    
    static void drawLogoRaster(U8G2& oled, uint8_t x0, uint8_t x1,
                               uint8_t y0, uint8_t y1, uint8_t level = 16) {
        if (x1 > DRAW_W) x1 = DRAW_W;
        if (y1 > DRAW_H) y1 = DRAW_H;
        if (level == 0) return;
        for (uint8_t y = y0; y < y1; ++y) {
            for (uint8_t x = x0; x < x1; ++x) {
                if (!logoPixel(x, y)) continue;
                const uint8_t sx = DRAW_X + x;
                const uint8_t sy = DRAW_Y + y;
                if (level < 16 && !ditherPass(sx, sy, level)) continue;
                oled.drawPixel(sx, sy);
            }
        }
    }

    
    static uint16_t easeOut(uint8_t step, uint8_t total) {
        const uint16_t t = (uint16_t)step * 256U / total;
        const uint16_t inv = 256U - t;
        return 256U - ((inv * inv) >> 8);
    }

    

    static void drawBlank(DisplayManager& dm) {
        dm.bootDisplay().clearBuffer();
        dm.presentBootFrame();
    }

    
    static void drawHairlineFrame(DisplayManager& dm, uint8_t step,
                                  uint8_t total) {
        U8G2& oled = dm.bootDisplay();
        oled.clearBuffer();
        const uint8_t w = (uint8_t)((uint32_t)DRAW_W * easeOut(step, total) / 256U);
        if (w > 0) {
            oled.drawHLine(DRAW_X + (DRAW_W - w) / 2, DRAW_Y + DRAW_H / 2, w);
        }
        dm.presentBootFrame();
    }

    
    static void drawApertureFrame(DisplayManager& dm, uint8_t step,
                                  uint8_t total) {
        U8G2& oled = dm.bootDisplay();
        oled.clearBuffer();
        const uint8_t half = (uint8_t)((uint16_t)(DRAW_H / 2) *
                                       easeOut(step, total) / 256U);
        const uint8_t y0 = (DRAW_H / 2) - half;
        const uint8_t y1 = (DRAW_H / 2) + half;
        drawLogoRaster(oled, 0, DRAW_W, y0, y1);
        if (half < DRAW_H / 2) {
            oled.drawHLine(DRAW_X, DRAW_Y + y0, DRAW_W);
            oled.drawHLine(DRAW_X, DRAW_Y + y1 - 1, DRAW_W);
        }
        dm.presentBootFrame();
    }

    
    static void drawWipeFrame(DisplayManager& dm, uint8_t step, uint8_t total) {
        U8G2& oled = dm.bootDisplay();
        oled.clearBuffer();
        const uint8_t cols = (uint8_t)((uint32_t)DRAW_W *
                                       easeOut(step, total) / 256U);
        drawLogoRaster(oled, 0, cols, 0, DRAW_H);
        if (cols < DRAW_W) {
            oled.drawVLine(DRAW_X + cols, DRAW_Y, DRAW_H);
        }
        dm.presentBootFrame();
    }

    static void drawLogoFrame(DisplayManager& dm, uint8_t level) {
        U8G2& oled = dm.bootDisplay();
        oled.clearBuffer();
        drawLogoRaster(oled, 0, DRAW_W, 0, DRAW_H, level);
        dm.presentBootFrame();
    }

    
    static void drawWordmarkFrame(DisplayManager& dm, uint8_t visibleChars,
                                  bool showSuffix) {
        U8G2& oled = dm.bootDisplay();
        if (visibleChars > WORDMARK_LEN) visibleChars = WORDMARK_LEN;
        oled.clearBuffer();
        oled.setFont(u8g2_font_7x13B_tf);

        const uint8_t startX = (128 - WORDMARK_WIDTH) / 2;
        for (uint8_t i = 0; i < visibleChars; ++i) {
            oled.drawGlyph(startX + i * WORDMARK_ADVANCE, 30, WORDMARK[i]);
        }

        const uint8_t lineW =
            (uint8_t)((uint16_t)WORDMARK_WIDTH * visibleChars / WORDMARK_LEN);
        if (lineW > 0) {
            oled.drawHLine((128 - lineW) / 2, 37, lineW);
        }

        if (showSuffix) {
            
            const uint8_t sx = (128 - 16) / 2;
            oled.drawGlyph(sx, 54, 'S');
            oled.drawGlyph(sx + 9, 54, '3');
        }
        dm.presentBootFrame();
    }

    static void serviceFor(HapticManager& haptics, uint16_t durationMs) {
        const uint32_t deadline = millis() + durationMs;
        while ((int32_t)(millis() - deadline) < 0) {
            haptics.update(millis());
            delay(8);
        }
    }
};

} 

#endif 