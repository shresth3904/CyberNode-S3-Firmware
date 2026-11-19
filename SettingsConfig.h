#ifndef CYBERNODE_SETTINGS_CONFIG_H
#define CYBERNODE_SETTINGS_CONFIG_H

#include <stdint.h>



namespace CyberNodeSettings {

static const uint8_t DEFAULT_OLED_SLEEP_MINUTES = 1;
static const uint8_t DEFAULT_OLED_CONTRAST = 160;
static const uint8_t MIN_OLED_CONTRAST = 128;
static const uint8_t MAX_OLED_CONTRAST = 176;
static const uint8_t OLED_CONTRAST_STEP = 16;




static const bool DEFAULT_SUPERIOR_BOOT_LOGO_ENABLED = true;
static const bool DEFAULT_BOOT_LOGO_ENABLED = true; 
static const uint8_t BOOT_LOGO_ANIMATION_TYPEWRITER = 0; 
static const uint8_t BOOT_LOGO_ANIMATION_GLITCH = 1;
static const uint8_t DEFAULT_BOOT_LOGO_ANIMATION = BOOT_LOGO_ANIMATION_GLITCH;
static const uint8_t DEFAULT_SUPERIOR_BOOT_ANIMATION = BOOT_LOGO_ANIMATION_GLITCH;
static const uint8_t DEFAULT_CYBERNODE_BOOT_ANIMATION = BOOT_LOGO_ANIMATION_GLITCH;

inline bool isSupportedSleepMinutes(uint8_t minutes) {
    return minutes == 1 || minutes == 2 || minutes == 5;
}

inline uint8_t sanitizeSleepMinutes(uint8_t minutes) {
    return isSupportedSleepMinutes(minutes) ? minutes : DEFAULT_OLED_SLEEP_MINUTES;
}

inline uint8_t nextSleepMinutes(uint8_t current) {
    current = sanitizeSleepMinutes(current);
    if (current == 1) return 2;
    if (current == 2) return 5;
    return 1;
}

inline uint8_t sanitizeOledContrast(uint8_t contrast) {
    if (contrast < MIN_OLED_CONTRAST) return MIN_OLED_CONTRAST;
    if (contrast > MAX_OLED_CONTRAST) return MAX_OLED_CONTRAST;
    
    const uint8_t steps = (uint8_t)((contrast - MIN_OLED_CONTRAST + OLED_CONTRAST_STEP / 2) / OLED_CONTRAST_STEP);
    return (uint8_t)(MIN_OLED_CONTRAST + steps * OLED_CONTRAST_STEP);
}

inline uint8_t nextOledContrast(uint8_t current) {
    current = sanitizeOledContrast(current);
    return current >= MAX_OLED_CONTRAST
        ? MIN_OLED_CONTRAST
        : (uint8_t)(current + OLED_CONTRAST_STEP);
}

inline uint8_t sanitizeBootLogoAnimation(uint8_t animation) {
    return animation <= BOOT_LOGO_ANIMATION_GLITCH
        ? animation : BOOT_LOGO_ANIMATION_TYPEWRITER;
}

inline uint8_t nextBootLogoAnimation(uint8_t current) {
    current = sanitizeBootLogoAnimation(current);
    return current == BOOT_LOGO_ANIMATION_TYPEWRITER
        ? BOOT_LOGO_ANIMATION_GLITCH : BOOT_LOGO_ANIMATION_TYPEWRITER;
}

inline uint32_t sleepTimeoutMs(bool enabled, uint8_t minutes) {
    if (!enabled) return 0UL;
    return (uint32_t)sanitizeSleepMinutes(minutes) * 60000UL;
}

} 

#endif 
