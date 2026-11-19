#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "CyberNodeTypes.h"
#include "NavigationManager.h"
#include "NavigationStream.h"

class DisplayManager {
public:
    DisplayManager();

    void begin();
    void clear();
    
    void setDisplaySleep(bool sleeping);
    bool isDisplaySleeping() const;

    
    void showBootScreen();
    void showStartupAnimation();
    void showSuperiorBootLogo(uint8_t animationStyle); 
    
    
    U8G2& bootDisplay();
    void presentBootFrame();
    void setHomeFace(uint8_t face);
    bool homeFaceAnimated() const;
    
    
    uint16_t homeFaceRefreshIntervalMs() const;
    void setFacePickerOverlay(bool active, bool showName);
    static uint8_t homeFaceCount();
    static const char* homeFaceName(uint8_t face);

    
    void showHomeScreen(
        const char* time,
        const char* date,
        int temperature,
        const char* weather,
        int steps,
        bool hasCentralData,
        bool bleConnected,
        bool phoneDataFresh,
        int64_t epochSeconds,
        uint32_t nowMs,
        int batteryPercent = -1
    );
    void showMenu(int selectedItem);
    void showSettingsMenu(int selectedItem);
    void showBootLogoSettings(int selectedItem, bool superiorEnabled, uint8_t superiorStyle,
                              bool cyberNodeEnabled, uint8_t cyberNodeStyle);
    void showNavigationSettings(int selectedItem, bool autoPopup, bool keepScreenAwake);
    void showHapticSettings(int selectedItem, bool master, bool navigation, bool calls,
                            bool notifications, bool alerts, uint8_t strength);
    void showNavigationMap(const NavigationSnapshot& nav, bool bleConnected, bool mtuCompatible, int batteryPercent, bool recentered);
    void showNavigationStreamMap(const NavStreamSnapshot& nav, const NavigationSnapshot& guidance, bool bleConnected, int batteryPercent = -1);
    void showNavigationInstructions(const NavigationSnapshot& nav, bool bleConnected, bool mtuCompatible, int batteryPercent);
    void showNavigationNoRoute(bool bleConnected, bool stopped);
    void showNotificationSettings(int selectedItem, bool alertsEnabled, bool popupInApps, bool beepEnabled);
    void showNotificationPopup(int selected, const WatchData& data);
    void showSleepSettings(int selectedItem, bool sleepEnabled, uint8_t timeoutMinutes, uint8_t contrast);
    void setContrast(uint8_t contrast);
    void showSoundSettings(int selectedItem, bool master, bool buttons, bool games, uint8_t volume);
    void showDialAppsSettings(int selectedItem, const bool enabled[], const char* const names[], int count);
    void showAlarm(uint16_t minuteOfDay, bool enabled, uint8_t field, uint8_t tone, bool alert);
    void showTimer(uint32_t remainingMs, bool running, bool completed, uint8_t field, uint8_t tone, bool alert);
    void showLocalAlert(bool alarm, bool timer);
    
    void showCallScreen(const char* name, const char* phone, const char* status,
                        const char* feedback, bool ringing, bool selectedAccept,
                        bool bleConnected, uint32_t activeDurationSeconds, uint32_t nowMs);
    void showGameList(int selectedGame, const char* const gameNames[], int gameCount);
    void showDinoGame(int dinoX, int dinoY, int cactusX, int score, bool gameOver);
    void showMinesweeper(const bool mines[16], const bool revealed[16], const uint8_t adjacent[16],
                         uint8_t cursor, uint8_t state, uint8_t safeRevealed);
    void showMiniRacing(const int lanes[3], const int obstacleLanes[3], const int obstacleY[3],
                        const bool obstacleActive[3], uint16_t score, bool shieldActive, uint8_t state,
                        bool hitEffect, bool shieldHitEffect);
    void showReactionTimer(uint8_t state, uint8_t difficulty, uint8_t round, uint16_t reactionMs,
                           uint16_t averageMs, uint16_t bestMs, uint8_t falseStarts, bool falseStart);
    void showSpaceInvaders(const bool invaders[8], int16_t baseX, int16_t baseY, int16_t playerX,
                           bool playerBulletActive, int16_t playerBulletX, int16_t playerBulletY,
                           bool enemyBulletActive, int16_t enemyBulletX, int16_t enemyBulletY,
                           uint16_t score, uint8_t lives, uint8_t wave, uint8_t state);
    void showBrickBreaker(const bool bricks[3][10], int16_t paddleX, int ballX, int ballY, bool launched,
                          uint16_t score, uint8_t lives, uint8_t level, uint8_t state);
    void showAppDialScreen(int selectedEntry, const char* appName, int entryCount, int appId,
                        const WatchData& data, bool hasCentralData, bool bleConnected, bool phoneDataFresh,
                        uint8_t gameCount, uint16_t alarmMinuteOfDay, bool alarmEnabled,
                        bool timerRunning, bool timerCompleted, uint32_t timerRemainingMs,
                        uint32_t timerDurationMs, bool stopwatchRunning, uint32_t stopwatchElapsedMs);

    
    void showWeatherScreen();
    void showStepsScreen();
    void showSystemInfoScreen();

    
    void showWeatherWindow(int windowIndex, const WatchData& data);
    void showStepsWindow(int windowIndex, const WatchData& data, bool hasData = true);
    void showSystemWindow(int windowIndex, bool bleConnected, bool phoneDataFresh);
    void showNotifications(int selected, const WatchData& data);
    void showNotificationDetail(int selected, const WatchData& data, int scrollLine);
    void showStopwatch(uint32_t elapsedMs, bool running, uint8_t lapCount, const uint32_t laps[2]);
    void showSuperiorFortune(uint8_t stage, uint8_t pointer, const int ratings[3],
                             bool glitch = false, uint8_t glitchFrame = 0);
    void showSuperiorCEO(uint8_t stage);

    
    void captureTransitionSourceFrame();
    void captureTransitionFrame();
    void drawTransitionMask(int revealWidth, int direction);
    void setPresentationDeferred(bool deferred);

private:
    void present();
    U8G2_SH1106_128X64_NONAME_F_HW_I2C display;
    uint8_t transitionSourceBuffer[1024];
    uint8_t transitionBaseBuffer[1024];
    bool presentationDeferred;
    bool displaySleeping_;
    uint8_t homeFace_ = 0;
    bool facePickerActive_ = false;
    bool facePickerShowName_ = false;
    char lastHomeTime_[6] = {0};
    bool lastHomeTimeValid_ = false;
    uint32_t minuteEffectUntilMs_ = 0;
};

#endif 
