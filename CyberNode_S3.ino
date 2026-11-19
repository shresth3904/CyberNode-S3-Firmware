#include "DisplayManager.h"
#include "CyberNodeBootAnimation.h"
#include "InputManager.h"
#include "Games/GameManager.h"
#include "Games/GameMenu.h"
#include "CyberNodeTypes.h"
#include "NodeBot.h"
#include "SoundManager.h"
#include "HapticManager.h"
#include "ClockSyncUtils.h"
#include "SettingsConfig.h"
#include "NavigationManager.h"
#include "NavigationStream.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <time.h>
#include <string>
#include <esp_system.h>



#ifndef CYBERNODE_DEBUG
#define CYBERNODE_DEBUG 0
#endif
#if CYBERNODE_DEBUG
#define CN_DEBUG_PRINTLN(message) Serial.println(message)
#define CN_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define CN_DEBUG_PRINTLN(message) do { (void)sizeof(message); } while (0)
#define CN_DEBUG_PRINTF(...) do { } while (0)
#endif

constexpr uint8_t BUZZER_PIN = 7; 




#ifndef CYBERNODE_HAPTIC_PIN
#define CYBERNODE_HAPTIC_PIN 10
#endif
constexpr uint8_t HAPTIC_MOTOR_PIN = CYBERNODE_HAPTIC_PIN;

SoundManager soundManager;
HapticManager hapticManager;

DisplayManager displayManager;
InputManager inputManager;
GameManager gameManager;
GameMenu gameMenu;



QueueHandle_t buttonEventQueue = nullptr;
TaskHandle_t inputTaskHandle = nullptr;
TaskHandle_t uiTaskHandle = nullptr;



static inline void notifyUiTask() {
    TaskHandle_t target = uiTaskHandle;
    if (target != nullptr) xTaskNotifyGive(target);
}



volatile bool oledSleeping = false;
static bool oledSleepEnabled = true; 
static uint8_t oledSleepTimeoutMinutes = CyberNodeSettings::DEFAULT_OLED_SLEEP_MINUTES;
static uint8_t oledContrast = CyberNodeSettings::DEFAULT_OLED_CONTRAST;
static uint32_t configuredOledSleepTimeoutMs() {
    return CyberNodeSettings::sleepTimeoutMs(oledSleepEnabled, oledSleepTimeoutMinutes);
}


static bool notificationAlertsEnabled = true;
static bool notificationPopupInApps = false;
static bool notificationBeepEnabled = true;
static bool notificationBaselineReady = false;
static bool notificationPopupActive = false;
static uint8_t notificationPopupSelected = 0;
static uint32_t notificationPopupUntilMs = 0;
static bool pendingNotificationPopup = false;
static uint8_t pendingNotificationIndex = 0;
static uint8_t notificationBeepQueueCount = 0;
static uint32_t nextNotificationBeepAllowedMs = 0;
static constexpr uint8_t NOTIFICATION_BEEP_QUEUE_MAX = 10;


static bool bleNotificationSnapshotPending = false;
static constexpr uint32_t NOTIFICATION_POPUP_DURATION_MS = 5000UL;

static const char* BLE_DEVICE_NAME = "CyberNode S3";
static const char* BLE_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* BLE_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; 
static const char* BLE_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; 
BLEServer* bleServer = nullptr;
BLECharacteristic* bleTxCharacteristic = nullptr;
volatile bool bleConnected = false;



static constexpr uint16_t BLE_CONFIGURED_LOCAL_MTU = 517;
static constexpr uint16_t BLE_DEFAULT_ATT_MTU = 23;
static volatile uint16_t blePeerAttMtu = BLE_DEFAULT_ATT_MTU;
static volatile bool blePeerMtuNegotiated = false;
static volatile bool bleNavigationPayloadSupported = false;
static volatile uint8_t bleLastTxNotifyResult = 0; 

static volatile uint8_t pendingBleSoundEvent = 0; 




enum CallBleEventKind : uint8_t {
    CALL_BLE_INCOMING,
    CALL_BLE_STATE_CHANGED,
    CALL_BLE_ENDED,
    CALL_BLE_COMMAND_RESULT
};



static constexpr size_t CALL_ID_CAPACITY = 64;

struct CallBleEvent {
    CallBleEventKind kind;
    char callId[CALL_ID_CAPACITY];
    char state[16];
    char name[32];
    char phone[20];
    char reason[32];
    char action[12];
    uint32_t requestId;
    bool success;
};
static uint8_t parseCallBlePayload(const String& json, CallBleEvent& event);
static void beginIncomingCall(const CallBleEvent& event);
static void applyCallBleEvent(const CallBleEvent& event);
static bool callOverlayVisible();
QueueHandle_t callEventQueue = nullptr;

enum CallUiState : uint8_t {
    CALL_UI_IDLE,
    CALL_UI_RINGING,
    CALL_UI_ANSWER_REQUESTED,
    CALL_UI_REJECT_REQUESTED,
    CALL_UI_ACTIVE,
    CALL_UI_TERMINAL
};
static CallUiState callUiState = CALL_UI_IDLE;
static char currentCallId[CALL_ID_CAPACITY] = {0};
static char currentCallerName[32] = {0};
static char currentCallerPhone[20] = {0};
static char callTerminalLabel[16] = {0};
static char callFeedback[28] = {0};
static uint8_t callSelection = 1; 
static uint32_t callRequestId = 0;
static uint32_t pendingCallRequestId = 0;
static uint32_t callRequestDeadlineMs = 0;
static uint32_t callFeedbackUntilMs = 0;
static uint32_t callTerminalUntilMs = 0;
static uint32_t callActiveStartedMs = 0;
static uint32_t lastCallRenderMs = 0;
static bool callLinkLost = false;
static bool callOverlayDismissed = false;
static constexpr uint32_t CALL_CONTROL_TIMEOUT_MS = 5000UL;
static constexpr uint32_t CALL_TERMINAL_DISPLAY_MS = 1600UL;
static constexpr uint32_t CALL_FEEDBACK_DISPLAY_MS = 1800UL;

WatchData watchData = {};
WatchData bleStagingData = {}; 
Preferences watchPreferences;
static constexpr uint32_t WATCH_STORAGE_MAGIC = 0x43594E33UL; 
static constexpr uint16_t WATCH_STORAGE_VERSION = 2;
static constexpr const char* WATCH_STORAGE_NAMESPACE = "cybernode";
static int64_t phoneEpochAnchor = 0;
static uint32_t phoneEpochAnchorMillis = 0;
static bool snapshotStored = false;
static bool firstPhoneSyncPersistedThisBoot = false;
static bool snapshotWritePending = false;
static uint32_t lastSnapshotPersistMs = 0;
static uint32_t lastClockMaintenanceMs = 0;
static uint32_t lastClockPersistMs = 0;
static uint32_t lastClockPersistAttemptMs = 0;
static constexpr uint32_t CLOCK_FLASH_CHECKPOINT_MS = 120000UL; 
static constexpr uint32_t CLOCK_FLASH_RETRY_MS = 10000UL;
QueueHandle_t watchDataQueue = nullptr;
SemaphoreHandle_t watchDataMutex = nullptr;


int phoneTimeAnchorSeconds = 0;
uint32_t phoneTimeAnchorMillis = 0;
bool localClockInitialized = false;


volatile uint32_t lastPhoneDataUpdateMs = 0;
bool phoneDataFresh = false;
bool staleStateReported = false;
const uint32_t PHONE_DATA_STALE_TIMEOUT_MS = 60000;



enum DialPreviewApp : int {
    DIAL_PREVIEW_FULL_MENU = -1,
    DIAL_PREVIEW_WEATHER = 0,
    DIAL_PREVIEW_STEPS,
    DIAL_PREVIEW_SYSTEM,
    DIAL_PREVIEW_GAMES,
    DIAL_PREVIEW_ALERTS,
    DIAL_PREVIEW_STOPWATCH,
    DIAL_PREVIEW_FORTUNE,
    DIAL_PREVIEW_ALARM,
    DIAL_PREVIEW_TIMER
};

static constexpr uint8_t DIAL_APP_COUNT = 9;

enum ScreenState {
    HOME_SCREEN,
    APP_DIAL_SCREEN,
    MENU_LIST_SCREEN,
    WEATHER_SCREEN,
    STEPS_SCREEN,
    SYSTEM_SCREEN,
    GAME_LIST_SCREEN,
    GAME_PLAY_SCREEN,
    SETTINGS_MENU_SCREEN,
    BOOT_LOGO_SETTINGS_SCREEN,
    DIAL_APPS_SETTINGS_SCREEN,
    SLEEP_SETTINGS_SCREEN,
    NAVIGATION_SETTINGS_SCREEN,
    HAPTIC_SETTINGS_SCREEN,
    NAVIGATION_SCREEN,
    SOUND_SETTINGS_SCREEN,
    NOTIFICATION_SETTINGS_SCREEN,
    NOTIFICATIONS_SCREEN,
    NOTIFICATION_DETAIL_SCREEN,
    STOPWATCH_SCREEN,
    SUPERIOR_SCREEN,
    ALARM_SCREEN,
    TIMER_SCREEN,
    FACE_PICKER_SCREEN
};

ScreenState currentScreen = HOME_SCREEN;
void showCurrentScreen();
static bool copyNavStreamSnapshot(NavStreamSnapshot& out);
static void navStreamTask(void* parameter);


static ScreenState notificationPopupReturnScreen = HOME_SCREEN;
static bool notificationDetailOpenedFromPopup = false;



static NavigationManager navigationManager;


struct NavStreamRxItem { uint16_t length; uint8_t bytes[NAV_STREAM_MAX_PACKET]; };
static QueueHandle_t navStreamRxQueue = nullptr;
static SemaphoreHandle_t navStreamSnapshotMutex = nullptr;
static NavigationStream navStreamManager;
static NavStreamSnapshot navStreamPublishedSnapshot = {};
static TaskHandle_t navStreamTaskHandle = nullptr;



static uint32_t navHapticSessionId = 0;
static char navHapticManeuver[20] = {0};
static bool navHapticApproachSent = false;
static bool navHapticTurnSent = false;
static bool navHapticHasPreviousDistance = false;
static uint32_t navHapticPreviousDistanceM = 0;
static portMUX_TYPE navigationMailboxMux = portMUX_INITIALIZER_UNLOCKED;
static NavigationMessage navMailboxState = {};
static NavigationMessage navMailboxMap = {};
static NavigationMessage navMailboxPosition = {};
static bool navMailboxHasState = false;
static bool navMailboxHasMap = false;
static bool navMailboxHasPosition = false;
static bool navigationAutoPopup = false;       
static bool navigationKeepScreenAwake = false;
static bool pendingNavigationPopup = false;
static bool navigationStayAwakeOverride = false;
static bool navigationManuallyExited = false;
static bool navigationMapRecentered = false;
static uint8_t navigationPage = 0;             
static ScreenState navigationReturnScreen = HOME_SCREEN;




static bool isNotificationAppScreen(int screen) {
    switch (screen) {
        case WEATHER_SCREEN:
        case STEPS_SCREEN:
        case SYSTEM_SCREEN:
        case GAME_LIST_SCREEN:
        case GAME_PLAY_SCREEN:
        case STOPWATCH_SCREEN:
        case SUPERIOR_SCREEN:
        case ALARM_SCREEN:
        case TIMER_SCREEN:
            return true;
        default:
            
            
            return false;
    }
}



int selectedMenuItem = 0;
uint8_t settingsMenuIndex = 0;
uint8_t bootLogoSettingIndex = 0;
static bool bootLogoEnabled = CyberNodeSettings::DEFAULT_BOOT_LOGO_ENABLED;
static bool bootSuperiorLogoEnabled = CyberNodeSettings::DEFAULT_SUPERIOR_BOOT_LOGO_ENABLED;
static uint8_t superiorBootLogoAnimation = CyberNodeSettings::DEFAULT_SUPERIOR_BOOT_ANIMATION;
static uint8_t cyberNodeBootAnimation = CyberNodeSettings::DEFAULT_CYBERNODE_BOOT_ANIMATION;
uint8_t sleepSettingIndex = 0;
uint8_t navigationSettingIndex = 0;
uint8_t hapticSettingIndex = 0;
int soundSettingIndex = 0;
uint8_t notificationSettingIndex = 0;
int notificationSelected = 0;
int notificationDetailScrollLine = 0;
bool stopwatchRunning = false;
uint32_t stopwatchStartMs = 0, stopwatchAccumulatedMs = 0;
uint8_t stopwatchLapCount = 0;
uint32_t stopwatchLaps[2] = {0,0};


struct LocalTimerState { uint32_t durationMs; uint32_t deadlineMs; bool running; bool completed; uint8_t tone; };
struct LocalAlarmState { uint16_t minuteOfDay; bool enabled; int32_t lastFiredDay; uint8_t tone; };
LocalTimerState localTimer = {300000UL, 0, false, false, 0}; 
LocalAlarmState localAlarm = {420, false, -1, 0}; 
uint8_t dialAppsSettingsIndex = 0;
uint8_t homeFace = 0;               
uint8_t homeFacePreview = 0;        
uint32_t facePickerNameUntilMs = 0; 
uint8_t alarmEditField = 0; 
uint8_t timerEditField = 0; 
bool alarmAlertActive = false, timerAlertActive = false;
uint32_t alertStartedMs = 0;
static bool dialAppEnabled[DIAL_APP_COUNT] = {true,true,true,true,true,true,true,true,true};



struct DialAppDescriptor {
    DialPreviewApp id;
    const char* name;
    const char* preferenceKey;
    bool dialEligible;
};
static const DialAppDescriptor dialAppRegistry[DIAL_APP_COUNT] = {
    {DIAL_PREVIEW_WEATHER,   "WEATHER",        "dyWeather",   true},
    {DIAL_PREVIEW_STEPS,     "STEP COUNT",     "dySteps",     true},
    {DIAL_PREVIEW_SYSTEM,    "SYSTEM INFO",    "dySystem",    true},
    {DIAL_PREVIEW_GAMES,     "GAME ENGINE",    "dyGames",     true},
    {DIAL_PREVIEW_ALERTS,    "ALERTS",         "dyAlerts",    true},
    {DIAL_PREVIEW_STOPWATCH, "STOPWATCH",      "dyStopwatch", true},
    {DIAL_PREVIEW_FORTUNE,   "FORTUNE TELLER", "dyFortune",   true},
    {DIAL_PREVIEW_ALARM,     "ALARM",          "dyAlarm",     true},
    {DIAL_PREVIEW_TIMER,     "TIMER",          "dyTimer",     true}
};

static const uint8_t legacyDialPreferenceIndex[DIAL_APP_COUNT] = {0,1,2,3,5,6,7,8,9};
int dialCursor = 0;

enum LauncherOrigin : uint8_t { LAUNCHER_ORIGIN_DIAL, LAUNCHER_ORIGIN_MENU };
static LauncherOrigin appLauncherOrigin = LAUNCHER_ORIGIN_DIAL;
static bool appLauncherContextValid = false;
static int appLauncherDialCursor = 0;
static int appLauncherDialAppId = DIAL_PREVIEW_WEATHER;
static int appLauncherMenuItem = 0;
static ScreenState menuParentScreen = APP_DIAL_SCREEN;
static int menuParentDialCursor = 0;
static int menuParentDialAppId = DIAL_PREVIEW_FULL_MENU;



uint8_t superiorStage = 0; 
uint8_t superiorPointer = 2;
int superiorRatings[3] = {0, 50, 100};
bool superiorCeoMode = false;
uint32_t superiorStageDeadline = 0;
bool superiorSwapActive = false;
uint8_t superiorSwapFrom = 2;
uint8_t superiorSwapTo = 2;
uint8_t superiorGlitchFrame = 0;
uint32_t superiorSwapDeadline = 0;
uint32_t superiorLastGlitchFrame = 0;

int weatherWindowIndex = 0;
int stepsWindowIndex = 0;
int systemWindowIndex = 0;



unsigned long lastHomeRefresh = 0;




bool transitionActive = false;
int transitionStep = 0;
unsigned long lastTransitionFrame = 0;
const unsigned long TRANSITION_FRAME_INTERVAL = 18; 
const int TRANSITION_TOTAL_STEPS = 8;

int activeTransitionDirection = TRANSITION_FROM_LEFT;



ButtonEvent pendingTransitionEvent = BUTTON_NONE;





void renderWithTransition(TransitionDirection direction);

struct PersistedWatchSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t dataSize;
    uint32_t checksum;
    WatchData data;
};

uint32_t calculateCrc32(const uint8_t* bytes, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
    return ~crc;
}

void initializeWatchDefaults() {
    memset(&watchData, 0, sizeof(watchData));
    strlcpy(watchData.time, "--:--", sizeof(watchData.time));
    strlcpy(watchData.date, "NOT SYNCED", sizeof(watchData.date));
    strlcpy(watchData.weather, "NO DATA", sizeof(watchData.weather));
    strlcpy(watchData.aqiStatus, "NO DATA", sizeof(watchData.aqiStatus));
    strlcpy(watchData.precipitation, "NO DATA", sizeof(watchData.precipitation));
    watchData.aqi = -1;
    watchData.dailyGoal = 10000;
    const char* days[7] = {"M", "T", "W", "T", "F", "S", "S"};
    for (uint8_t i = 0; i < 7; ++i) strlcpy(watchData.stepHistoryDays[i], days[i], sizeof(watchData.stepHistoryDays[i]));
}

bool snapshotLooksValid(const WatchData& data) {
    if (data.steps < 0 || data.steps > 200000 || data.temperature < -50 || data.temperature > 80 ||
        data.humidity > 100 || data.aqi < -1 || data.aqi > 1000 ||
        data.precipitationChance > 100 || data.goalPercent > 100 ||
        data.dailyGoal < 100 || data.dailyGoal > 100000 ||
        data.notificationCount > WATCH_NOTIFICATION_MAX ||
        data.timezoneOffsetMinutes < -720 || data.timezoneOffsetMinutes > 840 ||
        data.epochSeconds < 0 || data.epochSeconds > 4102444800LL) return false;
    if (!memchr(data.time, '\0', sizeof(data.time)) ||
        !memchr(data.date, '\0', sizeof(data.date)) ||
        !memchr(data.weather, '\0', sizeof(data.weather)) ||
        !memchr(data.aqiStatus, '\0', sizeof(data.aqiStatus)) ||
        !memchr(data.precipitation, '\0', sizeof(data.precipitation))) return false;
    if (strcmp(data.time, "--:--") != 0 && !(strlen(data.time) == 5 && data.time[2] == ':' &&
        isDigit(data.time[0]) && isDigit(data.time[1]) && isDigit(data.time[3]) && isDigit(data.time[4]))) return false;
    for (uint8_t i = 0; i < 7; ++i) {
        if (data.stepHistory[i] < 0 || data.stepHistory[i] > 200000 ||
            !memchr(data.stepHistoryDays[i], '\0', sizeof(data.stepHistoryDays[i]))) return false;
    }
    for (uint8_t i = 0; i < data.notificationCount; ++i) {
        const WatchNotification& n = data.notifications[i];
        if (!memchr(n.id, '\0', sizeof(n.id)) || !memchr(n.app, '\0', sizeof(n.app)) ||
            !memchr(n.title, '\0', sizeof(n.title)) || !memchr(n.content, '\0', sizeof(n.content)) ||
            !memchr(n.timestamp, '\0', sizeof(n.timestamp))) return false;
    }
    return true;
}

bool persistWatchSnapshot() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) {
        Serial.println("[NVS] ERROR: could not open preferences for snapshot write");
        return false;
    }
    PersistedWatchSnapshot record{};
    record.magic = WATCH_STORAGE_MAGIC;
    record.version = WATCH_STORAGE_VERSION;
    record.dataSize = sizeof(WatchData);
    record.data = watchData;
    record.checksum = calculateCrc32(reinterpret_cast<const uint8_t*>(&record.data), sizeof(record.data));
    size_t written = watchPreferences.putBytes("snapshot", &record, sizeof(record));
    watchPreferences.end();
    if (written == sizeof(record)) {
        snapshotStored = true;
        lastSnapshotPersistMs = millis();
        lastClockPersistMs = lastSnapshotPersistMs;
        lastClockPersistAttemptMs = lastSnapshotPersistMs;
        Serial.printf("[NVS] Watch data persisted (%u bytes)\n", (unsigned)written);
        return true;
    }
    Serial.println("[NVS] ERROR: snapshot write failed");
    return false;
}

bool restoreWatchSnapshot() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) {
        Serial.println("[NVS] Preferences unavailable; using defaults");
        return false;
    }
    size_t storedSize = watchPreferences.getBytesLength("snapshot");
    if (storedSize != sizeof(PersistedWatchSnapshot)) {
        watchPreferences.end();
        Serial.println("[NVS] No compatible saved watch snapshot; using defaults");
        return false;
    }
    PersistedWatchSnapshot record{};
    size_t readSize = watchPreferences.getBytes("snapshot", &record, sizeof(record));
    watchPreferences.end();
    if (readSize != sizeof(record) || record.magic != WATCH_STORAGE_MAGIC ||
        record.version != WATCH_STORAGE_VERSION || record.dataSize != sizeof(WatchData) ||
        record.checksum != calculateCrc32(reinterpret_cast<const uint8_t*>(&record.data), sizeof(record.data)) ||
        !snapshotLooksValid(record.data)) {
        Serial.println("[NVS] Saved snapshot failed validation; using defaults");
        return false;
    }
    watchData = record.data;
    snapshotStored = true;
    lastSnapshotPersistMs = millis();
    lastClockPersistMs = lastSnapshotPersistMs;
    lastClockPersistAttemptMs = lastSnapshotPersistMs;
    Serial.printf("[NVS] Restored watch snapshot: %ld steps, %u notifications\n",
                  (long)watchData.steps, watchData.notificationCount);
    return true;
}

bool shouldPersistWatchSnapshot(const WatchData& previous, const WatchData& current) {
    if (!snapshotStored) return true;

    
    if (previous.notificationCount != current.notificationCount ||
        memcmp(previous.notifications, current.notifications, sizeof(current.notifications)) != 0 ||
        previous.dailyGoal != current.dailyGoal ||
        previous.goalPercent != current.goalPercent ||
        (previous.epochSeconds > 0 && current.epochSeconds == 0) ||
        previous.temperature != current.temperature ||
        previous.humidity != current.humidity ||
        previous.aqi != current.aqi || strcmp(previous.aqiStatus, current.aqiStatus) != 0 ||
        previous.precipitationChance != current.precipitationChance ||
        strcmp(previous.precipitation, current.precipitation) != 0 ||
        strcmp(previous.weather, current.weather) != 0 ||
        strcmp(previous.date, current.date) != 0 ||
        previous.timezoneOffsetMinutes != current.timezoneOffsetMinutes) return true;

    
    for (uint8_t i = 0; i < 6; ++i) {
        if (previous.stepHistory[i] != current.stepHistory[i] ||
            strcmp(previous.stepHistoryDays[i], current.stepHistoryDays[i]) != 0) return true;
    }

    uint32_t elapsedSinceSave = millis() - lastSnapshotPersistMs;
    long stepDelta = labs((long)current.steps - (long)previous.steps);
    long historyTodayDelta = labs((long)current.stepHistory[6] - (long)previous.stepHistory[6]);
    
    
    if ((current.steps != previous.steps && stepDelta >= 100) ||
        (current.stepHistory[6] != previous.stepHistory[6] && historyTodayDelta >= 100) ||
        ((current.steps != previous.steps || current.stepHistory[6] != previous.stepHistory[6]) &&
         elapsedSinceSave >= 300000UL)) return true;

    
    
    
    return false;
}

void restoreSoundSettings() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) return;
    bool master = watchPreferences.getBool("sndMaster", true);
    bool buttons = watchPreferences.getBool("sndButtons", true);
    bool games = watchPreferences.getBool("sndGames", true);
    uint8_t volume = watchPreferences.getUChar("sndVolume", 100);
    watchPreferences.end();
    soundManager.setEnabled(master);
    soundManager.setButtonSounds(buttons);
    soundManager.setGameSounds(games);
    soundManager.setVolume(volume);
}

void persistSoundSettings() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) return;
    watchPreferences.putBool("sndMaster", soundManager.isEnabled());
    watchPreferences.putBool("sndButtons", soundManager.buttonSoundsEnabled());
    watchPreferences.putBool("sndGames", soundManager.gameSoundsEnabled());
    watchPreferences.putUChar("sndVolume", soundManager.volume());
    watchPreferences.end();
    Serial.println("[NVS] Sound settings saved");
}

void restoreHapticSettings() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, true)) return;
    const bool master = watchPreferences.getBool("hapMaster", true);
    const bool navigation = watchPreferences.getBool("hapNav", true);
    const bool calls = watchPreferences.getBool("hapCalls", true);
    const bool notifications = watchPreferences.getBool("hapNotif", true);
    const bool alerts = watchPreferences.getBool("hapAlerts", true);
    const uint8_t level = watchPreferences.getUChar("hapLevel", 1);
    watchPreferences.end();

    hapticManager.setEnabled(master);
    hapticManager.setNavigationEnabled(navigation);
    hapticManager.setCallsEnabled(calls);
    hapticManager.setNotificationsEnabled(notifications);
    hapticManager.setAlertsEnabled(alerts);
    hapticManager.setStrength(level);
}

void persistHapticSettings() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) return;
    watchPreferences.putBool("hapMaster", hapticManager.isEnabled());
    watchPreferences.putBool("hapNav", hapticManager.navigationEnabled());
    watchPreferences.putBool("hapCalls", hapticManager.callsEnabled());
    watchPreferences.putBool("hapNotif", hapticManager.notificationsEnabled());
    watchPreferences.putBool("hapAlerts", hapticManager.alertsEnabled());
    watchPreferences.putUChar("hapLevel", hapticManager.strength());
    watchPreferences.end();
    Serial.println("[NVS] Haptic settings saved");
}

bool updateLocalClock();

void restoreLocalAppSettings() {
    watchPreferences.begin(WATCH_STORAGE_NAMESPACE, true);
    for (uint8_t i=0;i<DIAL_APP_COUNT;i++) {
        const char* preferenceKey = dialAppRegistry[i].preferenceKey;
        if (watchPreferences.isKey(preferenceKey)) {
            dialAppEnabled[i] = watchPreferences.getBool(preferenceKey, true);
        } else {
            char legacyKey[8];
            snprintf(legacyKey, sizeof(legacyKey), "dial%u", (unsigned)legacyDialPreferenceIndex[i]);
            dialAppEnabled[i] = watchPreferences.getBool(legacyKey, true);
        }
    }
    localAlarm.minuteOfDay = watchPreferences.getUShort("alarmMin", 420);
    localAlarm.enabled = watchPreferences.getBool("alarmOn", false);
    localAlarm.tone = watchPreferences.getUChar("alarmTone", 0);
    localTimer.durationMs = watchPreferences.getUInt("timerDur", 300000UL);
    localTimer.tone = watchPreferences.getUChar("timerTone", 0);
    const bool hasLegacyBootLogoSetting = watchPreferences.isKey("bootLogo");
    bootLogoEnabled = watchPreferences.getBool("bootLogo", CyberNodeSettings::DEFAULT_BOOT_LOGO_ENABLED);
    
    
    
    bootSuperiorLogoEnabled = watchPreferences.getBool(
        "bootSup",
        hasLegacyBootLogoSetting ? bootLogoEnabled : CyberNodeSettings::DEFAULT_SUPERIOR_BOOT_LOGO_ENABLED);
    
    
    
    const uint8_t legacyBootStyle = CyberNodeSettings::sanitizeBootLogoAnimation(
        watchPreferences.getUChar("bootAnim", CyberNodeSettings::DEFAULT_BOOT_LOGO_ANIMATION));
    superiorBootLogoAnimation = CyberNodeSettings::sanitizeBootLogoAnimation(
        watchPreferences.getUChar("bootSupAnim", legacyBootStyle));
    cyberNodeBootAnimation = CyberNodeSettings::sanitizeBootLogoAnimation(
        watchPreferences.getUChar("bootCybAnim", legacyBootStyle));
    oledSleepEnabled = watchPreferences.getBool("sleepOn", true);
    oledSleepTimeoutMinutes = CyberNodeSettings::sanitizeSleepMinutes(
        watchPreferences.getUChar("sleepMin", CyberNodeSettings::DEFAULT_OLED_SLEEP_MINUTES));
    oledContrast = CyberNodeSettings::sanitizeOledContrast(
        watchPreferences.getUChar("oledContrast", CyberNodeSettings::DEFAULT_OLED_CONTRAST));
    notificationAlertsEnabled = watchPreferences.getBool("notifOn", true);
    notificationPopupInApps = watchPreferences.getBool("notifApps", false);
    notificationBeepEnabled = watchPreferences.getBool("notifBeep", true);
    navigationAutoPopup = watchPreferences.getBool("navPopup", false);
    
    
    navigationKeepScreenAwake = watchPreferences.isKey("navAwake")
        ? watchPreferences.getBool("navAwake", true) : true;
    const bool hasFaceVersion = watchPreferences.isKey("homeFaceV");
    const bool hasStoredFace = watchPreferences.isKey("homeFace");
    const uint8_t savedFaceVersion = watchPreferences.getUChar("homeFaceV", 0);
    const uint8_t savedFaceId = watchPreferences.getUChar("homeFace", 0);
    homeFace = savedFaceId;
    watchPreferences.end();
    if (localAlarm.minuteOfDay >= 1440) localAlarm.minuteOfDay = 420;
    if (localTimer.durationMs > 86399000UL || localTimer.durationMs < 1000UL) localTimer.durationMs = 300000UL;
    if (localAlarm.tone > 3) localAlarm.tone=0;
    if (localTimer.tone > 3) localTimer.tone=0;
    if (!hasFaceVersion || savedFaceVersion < 2) {
        
        
        
        if (!hasStoredFace) homeFace = 0; 
        else switch (savedFaceId) {
            case 0: homeFace = 6; break; 
            case 1: homeFace = 1; break; 
            case 2: homeFace = 3; break; 
            case 3: homeFace = 2; break; 
            case 4: homeFace = 4; break; 
            default: homeFace = 6; break;
        }
        if (watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) {
            watchPreferences.putUChar("homeFace", homeFace);
            watchPreferences.putUChar("homeFaceV", 2);
            watchPreferences.end();
        }
    } else if (savedFaceVersion != 2 || homeFace >= DisplayManager::homeFaceCount()) {
        homeFace = 6;
        if (watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) {
            watchPreferences.putUChar("homeFace", homeFace);
            watchPreferences.putUChar("homeFaceV", 2);
            watchPreferences.end();
        }
    }
    displayManager.setHomeFace(homeFace);
}

void persistLocalAppSettings() {
    watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false);
    for (uint8_t i=0;i<DIAL_APP_COUNT;i++) watchPreferences.putBool(dialAppRegistry[i].preferenceKey, dialAppEnabled[i]);
    watchPreferences.putUShort("alarmMin",localAlarm.minuteOfDay);
    watchPreferences.putBool("alarmOn",localAlarm.enabled);
    watchPreferences.putUChar("alarmTone",localAlarm.tone);
    watchPreferences.putUInt("timerDur",localTimer.durationMs);
    watchPreferences.putUChar("timerTone",localTimer.tone);
    watchPreferences.end();
}

void persistSleepSettings() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) {
        Serial.println("[NVS] ERROR: unable to open settings namespace for sleep settings");
        return;
    }
    watchPreferences.putBool("sleepOn", oledSleepEnabled);
    watchPreferences.putUChar("sleepMin", CyberNodeSettings::sanitizeSleepMinutes(oledSleepTimeoutMinutes));
    watchPreferences.putUChar("oledContrast", CyberNodeSettings::sanitizeOledContrast(oledContrast));
    watchPreferences.end();
    Serial.printf("[NVS] OLED settings saved: sleep=%s, timeout=%u min, contrast=%u\n",
                  oledSleepEnabled ? "ON" : "OFF", (unsigned)oledSleepTimeoutMinutes,
                  (unsigned)oledContrast);
}

void persistBootLogoSettings() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) {
        Serial.println("[NVS] ERROR: unable to save boot-logo settings");
        return;
    }
    superiorBootLogoAnimation = CyberNodeSettings::sanitizeBootLogoAnimation(superiorBootLogoAnimation);
    cyberNodeBootAnimation = CyberNodeSettings::sanitizeBootLogoAnimation(cyberNodeBootAnimation);
    watchPreferences.putBool("bootLogo", bootLogoEnabled);
    watchPreferences.putBool("bootSup", bootSuperiorLogoEnabled);
    watchPreferences.putUChar("bootSupAnim", superiorBootLogoAnimation);
    watchPreferences.putUChar("bootCybAnim", cyberNodeBootAnimation);
    
    watchPreferences.putUChar("bootAnim", cyberNodeBootAnimation);
    watchPreferences.end();
    Serial.printf("[NVS] Boot logo settings: superior=%s/%s cybernode=%s/%s\n",
                  bootSuperiorLogoEnabled ? "ON" : "OFF",
                  superiorBootLogoAnimation == CyberNodeSettings::BOOT_LOGO_ANIMATION_GLITCH ? "GLITCH" : "SCAN",
                  bootLogoEnabled ? "ON" : "OFF",
                  cyberNodeBootAnimation == CyberNodeSettings::BOOT_LOGO_ANIMATION_GLITCH ? "GLITCH" : "SCAN");
}

void persistNavigationSettings() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) {
        Serial.println("[NVS] ERROR: unable to save navigation settings");
        return;
    }
    watchPreferences.putBool("navPopup", navigationAutoPopup);
    watchPreferences.putBool("navAwake", navigationKeepScreenAwake);
    watchPreferences.end();
}

void persistNotificationSettings() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) {
        Serial.println("[NVS] ERROR: unable to save notification settings");
        return;
    }
    watchPreferences.putBool("notifOn", notificationAlertsEnabled);
    watchPreferences.putBool("notifApps", notificationPopupInApps);
    watchPreferences.putBool("notifBeep", notificationBeepEnabled);
    watchPreferences.end();
    Serial.printf("[NVS] Notification settings: alerts=%s, appPopup=%s, beep=%s\n",
                  notificationAlertsEnabled ? "ON" : "OFF",
                  notificationPopupInApps ? "ON" : "OFF",
                  notificationBeepEnabled ? "ON" : "OFF");
}

void persistHomeFace() {
    if (!watchPreferences.begin(WATCH_STORAGE_NAMESPACE, false)) return;
    watchPreferences.putUChar("homeFace", homeFace);
    watchPreferences.putUChar("homeFaceV", 2);
    watchPreferences.end();
}

void playSelectedTone(uint8_t tone, bool alarm) {
    
    static const SoundEvent alarmTones[4] = {SoundEvent::ALARM, SoundEvent::LOW_BATTERY, SoundEvent::NOTIFICATION, SoundEvent::BLE_CONNECTED};
    static const SoundEvent timerTones[4] = {SoundEvent::TIMER_DONE, SoundEvent::ALARM, SoundEvent::NOTIFICATION, SoundEvent::LOW_BATTERY};
    soundManager.play(alarm ? alarmTones[tone & 3] : timerTones[tone & 3]);
}

bool updateLocalAlarmTimer() {
    uint32_t now=millis();
    bool alertTriggered = false;
    if (localTimer.running && (int32_t)(now-localTimer.deadlineMs)>=0) {
        localTimer.running=false; localTimer.completed=true; timerAlertActive=true; alertStartedMs=now;
        alertTriggered = true;
        playSelectedTone(localTimer.tone,false);
    }
    if (localAlarm.enabled && localClockInitialized) {
        updateLocalClock();
        int hour=0, minute=0;
        if (sscanf(watchData.time,"%d:%d",&hour,&minute)==2) {
            int current=hour*60+minute;
            int32_t dayKey=0;
            
            if (watchData.epochSeconds>0) dayKey=(int32_t)((watchData.epochSeconds + (int64_t)watchData.timezoneOffsetMinutes*60)/86400);
            else { for (const char* p=watchData.date;*p;p++) dayKey=dayKey*31+*p; }
            if (current==localAlarm.minuteOfDay && localAlarm.lastFiredDay!=dayKey) {
                localAlarm.lastFiredDay=dayKey; alarmAlertActive=true; alertStartedMs=now;
                alertTriggered = true;
                playSelectedTone(localAlarm.tone,true);
            }
        }
    }
    
    if ((timerAlertActive || alarmAlertActive) && now-alertStartedMs>=12000UL) {
        timerAlertActive=false;
        alarmAlertActive=false;
    }
    return alertTriggered;
}




void syncLocalClockFromPhone(const char* phoneTime) {
    
    
    if (watchData.epochSeconds > 0) {
        phoneEpochAnchor = watchData.epochSeconds;
        phoneEpochAnchorMillis = millis();
        localClockInitialized = true;
    } else {
        phoneEpochAnchor = 0;
        phoneEpochAnchorMillis = millis();
    }
    if (phoneTime == nullptr || strlen(phoneTime) != 5 || phoneTime[2] != ':') return;
    if (!isDigit(phoneTime[0]) || !isDigit(phoneTime[1]) ||
        !isDigit(phoneTime[3]) || !isDigit(phoneTime[4])) return;
    int hours = (phoneTime[0] - '0') * 10 + (phoneTime[1] - '0');
    int minutes = (phoneTime[3] - '0') * 10 + (phoneTime[4] - '0');
    if (hours > 23 || minutes > 59) return;
    phoneTimeAnchorSeconds = hours * 3600 + minutes * 60;
    phoneTimeAnchorMillis = millis();
    localClockInitialized = true;
}

bool updateLocalClock() {
    if (!localClockInitialized) return false;
    char updatedTime[6];
    char updatedDate[16];
    if (phoneEpochAnchor > 0) {
        int64_t currentEpoch = phoneEpochAnchor + (int64_t)((uint32_t)(millis() - phoneEpochAnchorMillis) / 1000UL);
        watchData.epochSeconds = currentEpoch;
        time_t localEpoch = (time_t)(currentEpoch + (int64_t)watchData.timezoneOffsetMinutes * 60LL);
        struct tm parts;
        if (gmtime_r(&localEpoch, &parts) != nullptr) {
            snprintf(updatedTime, sizeof(updatedTime), "%02d:%02d", parts.tm_hour, parts.tm_min);
            static const char* weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
            static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
            snprintf(updatedDate, sizeof(updatedDate), "%.3s %02d %.3s %04d",
                     weekdays[parts.tm_wday], parts.tm_mday, months[parts.tm_mon], parts.tm_year + 1900);
        } else {
            return false;
        }
    } else {
        uint32_t elapsedSeconds = (uint32_t)(millis() - phoneTimeAnchorMillis) / 1000UL;
        uint32_t currentSeconds = (phoneTimeAnchorSeconds + elapsedSeconds) % 86400UL;
        int hours = currentSeconds / 3600UL;
        int minutes = (currentSeconds % 3600UL) / 60UL;
        snprintf(updatedTime, sizeof(updatedTime), "%02d:%02d", hours, minutes);
        strlcpy(updatedDate, watchData.date, sizeof(updatedDate));
    }

    bool changed = strcmp(watchData.time, updatedTime) != 0 || strcmp(watchData.date, updatedDate) != 0;
    strlcpy(watchData.time, updatedTime, sizeof(watchData.time));
    strlcpy(watchData.date, updatedDate, sizeof(watchData.date));
    return changed;
}

void showHome() {
    updateLocalClock();
    NodeBot::controller().setHomeContext(
        watchData, snapshotStored || phoneDataFresh, alarmAlertActive, timerAlertActive);
    displayManager.showHomeScreen(
        watchData.time,
        watchData.date,
        watchData.temperature,
        watchData.weather,
        watchData.steps,
        snapshotStored || phoneDataFresh,
        bleConnected,
        phoneDataFresh,
        watchData.epochSeconds,
        millis()
    );
}


void applyFacePreview(bool showName) {
    displayManager.setHomeFace(homeFacePreview);
    displayManager.setFacePickerOverlay(true, showName);
    facePickerNameUntilMs = showName ? millis() + 1800UL : 0;
}

void openFacePicker() {
    homeFacePreview = homeFace;
    currentScreen = FACE_PICKER_SCREEN;
    applyFacePreview(true);
    renderWithTransition(TRANSITION_RADIAL_REVEAL);
}


void closeFacePicker(bool save) {
    if (save && homeFace != homeFacePreview) {
        homeFace = homeFacePreview;
        persistHomeFace();
    }
    displayManager.setHomeFace(homeFace);
    displayManager.setFacePickerOverlay(false, false);
    facePickerNameUntilMs = 0;
}

int dialEntryCount() {
    int count = 1; 
    for (uint8_t i = 0; i < DIAL_APP_COUNT; ++i)
        if (dialAppRegistry[i].dialEligible && dialAppEnabled[i]) ++count;
    return count;
}

int dialAppAtEntry(int entry) {
    int visibleIndex = 0;
    for (uint8_t i = 0; i < DIAL_APP_COUNT; ++i) {
        if (!dialAppRegistry[i].dialEligible || !dialAppEnabled[i]) continue;
        if (visibleIndex == entry) return (int)dialAppRegistry[i].id;
        ++visibleIndex;
    }
    return DIAL_PREVIEW_FULL_MENU;
}

const char* dialEntryName(int entry) {
    const int appId = dialAppAtEntry(entry);
    if (appId < 0) return "ALL APPS";
    for (uint8_t i = 0; i < DIAL_APP_COUNT; ++i)
        if ((int)dialAppRegistry[i].id == appId) return dialAppRegistry[i].name;
    return "APP";
}

int dialEntryForApp(int appId) {
    const int count = dialEntryCount();
    for (int entry = 0; entry < count; ++entry)
        if (dialAppAtEntry(entry) == appId) return entry;
    return -1;
}

void rememberDialLaunch() {
    appLauncherOrigin = LAUNCHER_ORIGIN_DIAL;
    appLauncherDialCursor = dialCursor;
    appLauncherDialAppId = dialAppAtEntry(dialCursor);
    appLauncherContextValid = true;
}

void rememberMenuLaunch(int menuItem) {
    appLauncherOrigin = LAUNCHER_ORIGIN_MENU;
    appLauncherMenuItem = menuItem;
    appLauncherContextValid = true;
}

void returnToAppLauncher() {
    if (!appLauncherContextValid) {
        
        
        currentScreen = HOME_SCREEN;
        renderWithTransition(TRANSITION_RADIAL_REVEAL);
        return;
    }
    if (appLauncherOrigin == LAUNCHER_ORIGIN_MENU) {
        currentScreen = MENU_LIST_SCREEN;
        selectedMenuItem = appLauncherMenuItem;
        appLauncherContextValid = false;
        renderWithTransition(TRANSITION_FROM_RIGHT);
    } else {
        currentScreen = APP_DIAL_SCREEN;
        const int restoredEntry = dialEntryForApp(appLauncherDialAppId);
        dialCursor = restoredEntry >= 0 ? restoredEntry : appLauncherDialCursor;
        appLauncherContextValid = false;
        renderWithTransition(TRANSITION_RADIAL_REVEAL);
    }
}

void returnToMenuParent() {
    if (menuParentScreen == APP_DIAL_SCREEN) {
        currentScreen = APP_DIAL_SCREEN;
        const int restoredEntry = dialEntryForApp(menuParentDialAppId);
        dialCursor = restoredEntry >= 0 ? restoredEntry : menuParentDialCursor;
        renderWithTransition(TRANSITION_RADIAL_REVEAL);
    } else {
        currentScreen = HOME_SCREEN;
        renderWithTransition(TRANSITION_FROM_CENTER);
    }
}

static void syncNavigationAwakeLease(uint32_t nowMs) {
    const bool shouldHold = navigationRequiresAwakeDisplay(
        navigationKeepScreenAwake, navigationManager.snapshot(), nowMs);
    if (shouldHold != navigationStayAwakeOverride) {
        navigationStayAwakeOverride = shouldHold;
        
        
        inputManager.markActivity(nowMs);
        Serial.printf("[NAV] Display awake lease %s\n", shouldHold ? "ACQUIRED" : "RELEASED");
    }
}

static void openNavigationScreen(bool automaticOpen, bool forceWake) {
    if (currentScreen != NAVIGATION_SCREEN) navigationReturnScreen = currentScreen;
    navigationManuallyExited = false;
    navigationMapRecentered = false;
    navigationPage = 0;
    currentScreen = NAVIGATION_SCREEN;
    if (forceWake && oledSleeping) {
        oledSleeping = false;
        displayManager.setDisplaySleep(false);
        Serial.println("[NAV] OLED woken by Keep Screen Awake policy");
    }
    syncNavigationAwakeLease(millis());
    
    
    if (automaticOpen && !oledSleeping && !notificationPopupActive &&
        !callOverlayVisible() && !(alarmAlertActive || timerAlertActive)) {
        static bool firstNavOpenRenderDiag = false;
        const bool logFirstOpen = !firstNavOpenRenderDiag;
        if (logFirstOpen) {
            firstNavOpenRenderDiag = true;
            Serial.printf("[NAV_DIAG] navigation screen open render begin oledSleeping=%u ui_stack_hwm=%u free_heap=%u\n",
                          (unsigned)oledSleeping,
                          (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                          (unsigned)esp_get_free_heap_size());
        }
        showCurrentScreen();
        if (logFirstOpen) {
            Serial.printf("[NAV_DIAG] navigation screen open render complete ui_stack_hwm=%u free_heap=%u\n",
                          (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                          (unsigned)esp_get_free_heap_size());
        }
    }
}

static void closeNavigationScreen(bool userInitiated) {
    
    
    if (userInitiated) navigationManuallyExited = true;
    currentScreen = navigationReturnScreen;
    if (currentScreen == NAVIGATION_SCREEN) currentScreen = HOME_SCREEN;
    soundManager.play(SoundEvent::BACK);
    if (!oledSleeping) showCurrentScreen();
}

static bool navigationManeuverNeedsHaptic(const char* maneuver) {
    if (!maneuver || !maneuver[0]) return false;
    static const char* const turningManeuvers[] = {
        "SLIGHT_LEFT", "TURN_SLIGHT_LEFT", "LEFT", "TURN_LEFT", "SHARP_LEFT",
        "TURN_SHARP_LEFT", "U_TURN_LEFT", "SLIGHT_RIGHT", "TURN_SLIGHT_RIGHT",
        "RIGHT", "TURN_RIGHT", "SHARP_RIGHT", "TURN_SHARP_RIGHT", "U_TURN_RIGHT",
        "KEEP_LEFT", "KEEP_RIGHT", "FORK_LEFT", "FORK_RIGHT", "ROUNDABOUT",
        "ROUNDABOUT_ENTER", "ROUNDABOUT_EXIT", "RAMP_LEFT", "RAMP_RIGHT"
    };
    for (size_t i = 0; i < sizeof(turningManeuvers) / sizeof(turningManeuvers[0]); ++i) {
        if (strcmp(maneuver, turningManeuvers[i]) == 0) return true;
    }
    return false;
}

static void resetNavigationHapticCues(uint32_t sessionId = 0) {
    navHapticSessionId = sessionId;
    navHapticManeuver[0] = '\0';
    navHapticApproachSent = false;
    navHapticTurnSent = false;
    navHapticHasPreviousDistance = false;
    navHapticPreviousDistanceM = 0;
}

static void updateNavigationHaptics() {
    const NavigationSnapshot& nav = navigationManager.snapshot();
    if (!nav.active || nav.stopped) {
        if (navHapticSessionId != 0 || navHapticManeuver[0]) resetNavigationHapticCues();
        return;
    }
    if (navHapticSessionId != nav.sessionId) resetNavigationHapticCues(nav.sessionId);

    if (strcmp(navHapticManeuver, nav.maneuver) != 0) {
        strlcpy(navHapticManeuver, nav.maneuver, sizeof(navHapticManeuver));
        navHapticApproachSent = false;
        navHapticTurnSent = false;
        navHapticHasPreviousDistance = false;
    }
    if (nav.stale || !nav.hasDistanceToManeuver ||
        !navigationManeuverNeedsHaptic(nav.maneuver)) return;

    const uint32_t distanceToManeuverM = nav.distanceToManeuverM;
    
    
    const bool distanceReset = navHapticHasPreviousDistance && navHapticTurnSent &&
        distanceToManeuverM >= 80UL &&
        distanceToManeuverM > navHapticPreviousDistanceM + 60UL;
    if (distanceReset) {
        navHapticApproachSent = false;
        navHapticTurnSent = false;
    }

    if (distanceToManeuverM <= 30UL && !navHapticTurnSent) {
        hapticManager.trigger(HapticEvent::NAV_TURN);
        navHapticTurnSent = true;
        navHapticApproachSent = true;
    } else if (distanceToManeuverM <= 100UL && !navHapticApproachSent) {
        hapticManager.trigger(HapticEvent::NAV_APPROACH);
        navHapticApproachSent = true;
    }
    navHapticPreviousDistanceM = distanceToManeuverM;
    navHapticHasPreviousDistance = true;
}

static void consumeNavigationMailbox() {
    NavigationMessage stateMessage, mapMessage, positionMessage;
    bool haveState = false, haveMap = false, havePosition = false;
    portENTER_CRITICAL(&navigationMailboxMux);
    if (navMailboxHasState) { stateMessage = navMailboxState; navMailboxHasState = false; haveState = true; }
    if (navMailboxHasMap) { mapMessage = navMailboxMap; navMailboxHasMap = false; haveMap = true; }
    if (navMailboxHasPosition) { positionMessage = navMailboxPosition; navMailboxHasPosition = false; havePosition = true; }
    portEXIT_CRITICAL(&navigationMailboxMux);

    bool redraw = false;
    bool screenChanged = false;
    const uint32_t now = millis();
    if (haveState) {
        const NavigationSnapshot& stateBeforeUpdate = navigationManager.snapshot();
        const bool recoveringStaleGuidance = stateBeforeUpdate.active &&
            !navigationManager.isGuidanceFresh(now);
        const NavigationUpdateResult result = navigationManager.apply(stateMessage, now);
        if (result == NAV_UPDATE_SESSION_STARTED) {
            static bool firstNavSessionUiDiag = false;
            if (!firstNavSessionUiDiag) {
                firstNavSessionUiDiag = true;
                Serial.printf("[NAV_DIAG] UI accepted session sid=%lu keepAwake=%u ui_stack_hwm=%u free_heap=%u\n",
                              (unsigned long)stateMessage.sessionId,
                              (unsigned)navigationKeepScreenAwake,
                              (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                              (unsigned)esp_get_free_heap_size());
            }
            pendingNavigationPopup = false;
            navigationManuallyExited = false;
            if (navigationKeepScreenAwake) {
                openNavigationScreen(true, true); 
                screenChanged = true;
            } else if (navigationAutoPopup && currentScreen == HOME_SCREEN) {
                if (oledSleeping) {
                    navigationReturnScreen = HOME_SCREEN;
                    pendingNavigationPopup = true; 
                } else { openNavigationScreen(true, false); screenChanged = true; }
            }
            redraw = true;
        } else if (result == NAV_UPDATE_SESSION_STOPPED) {
            pendingNavigationPopup = false;
            
            
            inputManager.markActivity(now);
            if (currentScreen == NAVIGATION_SCREEN) {
                currentScreen = navigationReturnScreen;
                screenChanged = true;
            }
            
            
            redraw = true;
        } else if (result == NAV_UPDATE_ACCEPTED) {
            if (recoveringStaleGuidance && navigationKeepScreenAwake) {
                if (!navigationManuallyExited) {
                    
                    
                    openNavigationScreen(true, true);
                } else {
                    
                    
                    
                    
                    if (oledSleeping) {
                        oledSleeping = false;
                        displayManager.setDisplaySleep(false);
                    }
                    inputManager.markActivity(now);
                }
                screenChanged = true;
            }
            redraw = true;
        }
    }
    if (haveMap && navigationManager.apply(mapMessage, now) == NAV_UPDATE_ACCEPTED) redraw = true;
    if (havePosition && navigationManager.apply(positionMessage, now) == NAV_UPDATE_ACCEPTED) redraw = true;

    const bool staleChanged = navigationManager.refreshStaleState(now);
    if (staleChanged && navigationManager.snapshot().stale) {
        
        
        inputManager.markActivity(now);
        Serial.println("[NAV] Guidance stale; normal OLED sleep policy resumed");
    }
    syncNavigationAwakeLease(now);
    updateNavigationHaptics();
    if (staleChanged) redraw = true;
    if (!oledSleeping && !notificationPopupActive && !callOverlayVisible() &&
        !(alarmAlertActive || timerAlertActive) &&
        ((redraw && currentScreen == NAVIGATION_SCREEN) || screenChanged)) {
        static bool firstNavMapRenderDiag = false;
        const bool logFirstRender = !firstNavMapRenderDiag && navigationManager.snapshot().hasMap;
        if (logFirstRender) {
            firstNavMapRenderDiag = true;
            Serial.printf("[NAV_DIAG] first map render begin points=%u frame=%u ui_stack_hwm=%u free_heap=%u\n",
                          (unsigned)navigationManager.snapshot().pointCount,
                          (unsigned)navigationManager.snapshot().mapFrame,
                          (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                          (unsigned)esp_get_free_heap_size());
        }
        showCurrentScreen();
        if (logFirstRender) {
            Serial.printf("[NAV_DIAG] first map render complete ui_stack_hwm=%u free_heap=%u\n",
                          (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                          (unsigned)esp_get_free_heap_size());
        }
    }
}

bool dialPreviewDataChanged(const WatchData& before, const WatchData& after, int appId) {
    switch (appId) {
        case DIAL_PREVIEW_WEATHER:
            return before.temperature != after.temperature || before.humidity != after.humidity ||
                   before.aqi != after.aqi || strcmp(before.weather, after.weather) != 0;
        case DIAL_PREVIEW_STEPS:
            return before.steps != after.steps || before.dailyGoal != after.dailyGoal ||
                   before.goalPercent != after.goalPercent ||
                   memcmp(before.stepHistory, after.stepHistory, sizeof(before.stepHistory)) != 0;
        case DIAL_PREVIEW_ALERTS:
            return before.notificationCount != after.notificationCount ||
                   memcmp(before.notifications, after.notifications, sizeof(before.notifications)) != 0;
        default:
            return false;
    }
}

void launchDialEntry(int entry) {
    const int appId = dialAppAtEntry(entry);
    if (appId < 0) {
        menuParentScreen = APP_DIAL_SCREEN;
        menuParentDialCursor = dialCursor;
        menuParentDialAppId = dialAppAtEntry(dialCursor);
        currentScreen = MENU_LIST_SCREEN;
        selectedMenuItem = 0;
        return;
    }

    rememberDialLaunch();
    switch (appId) {
      case DIAL_PREVIEW_WEATHER: currentScreen=WEATHER_SCREEN; weatherWindowIndex=0; break;
      case DIAL_PREVIEW_STEPS: currentScreen=STEPS_SCREEN; stepsWindowIndex=0; break;
      case DIAL_PREVIEW_SYSTEM: currentScreen=SYSTEM_SCREEN; systemWindowIndex=0; break;
      case DIAL_PREVIEW_GAMES: currentScreen=GAME_LIST_SCREEN; gameMenu.reset(); break;
      case DIAL_PREVIEW_ALERTS: currentScreen=NOTIFICATIONS_SCREEN; notificationSelected=0; notificationDetailScrollLine=0; break;
      case DIAL_PREVIEW_STOPWATCH: currentScreen=STOPWATCH_SCREEN; break;
      case DIAL_PREVIEW_FORTUNE: currentScreen=SUPERIOR_SCREEN; superiorStage=0; superiorPointer=2; superiorCeoMode=false; superiorStageDeadline=0; superiorSwapActive=false; superiorSwapDeadline=0; soundManager.play(SoundEvent::FORTUNE_INTRO); break;
      case DIAL_PREVIEW_ALARM: currentScreen=ALARM_SCREEN; alarmEditField=0; break;
      case DIAL_PREVIEW_TIMER: currentScreen=TIMER_SCREEN; timerEditField=0; break;
      default: appLauncherContextValid=false; currentScreen=APP_DIAL_SCREEN; break;
    }
}

void showCurrentScreen() {
    if (notificationPopupActive) {
        displayManager.showNotificationPopup(notificationPopupSelected, watchData);
        return;
    }
    switch (currentScreen) {

        case HOME_SCREEN:
            showHome();
            break;

        case APP_DIAL_SCREEN: {
            int entryCount = dialEntryCount();
            if (entryCount < 1) entryCount = 1;
            if (dialCursor < 0 || dialCursor >= entryCount) dialCursor = 0;
            int appId = dialAppAtEntry(dialCursor);
            uint32_t timerRemaining = localTimer.durationMs;
            if (localTimer.running) {
                int32_t remaining = (int32_t)(localTimer.deadlineMs - millis());
                timerRemaining = remaining > 0 ? (uint32_t)remaining : 0;
            }
            uint32_t stopwatchElapsed = stopwatchAccumulatedMs + (stopwatchRunning ? millis() - stopwatchStartMs : 0);
            displayManager.showAppDialScreen(dialCursor, dialEntryName(dialCursor), entryCount, appId,
                                          watchData, (snapshotStored || phoneDataFresh), bleConnected, phoneDataFresh,
                                          GameManager::GAME_COUNT, localAlarm.minuteOfDay, localAlarm.enabled,
                                          localTimer.running, localTimer.completed, timerRemaining,
                                          localTimer.durationMs, stopwatchRunning, stopwatchElapsed);
            break;
        }

        case MENU_LIST_SCREEN:
            displayManager.showMenu(selectedMenuItem);
            break;

        case WEATHER_SCREEN:
            displayManager.showWeatherWindow(weatherWindowIndex, watchData);
            break;

        case STEPS_SCREEN:
            displayManager.showStepsWindow(stepsWindowIndex, watchData, snapshotStored || phoneDataFresh);
            break;

        case SYSTEM_SCREEN:
            displayManager.showSystemWindow(systemWindowIndex, bleConnected, phoneDataFresh);
            break;

        case GAME_LIST_SCREEN:
            gameMenu.draw(displayManager);
            break;

        case GAME_PLAY_SCREEN:
            gameManager.draw(displayManager);
            break;

        case NOTIFICATIONS_SCREEN:
            displayManager.showNotifications(notificationSelected, watchData);
            break;

        case NOTIFICATION_DETAIL_SCREEN:
            displayManager.showNotificationDetail(notificationSelected, watchData, notificationDetailScrollLine);
            break;

        case STOPWATCH_SCREEN:
            displayManager.showStopwatch(stopwatchAccumulatedMs + (stopwatchRunning ? millis()-stopwatchStartMs : 0), stopwatchRunning, stopwatchLapCount, stopwatchLaps);
            break;

        case SUPERIOR_SCREEN:
            if (superiorCeoMode) displayManager.showSuperiorCEO(superiorStage);
            else displayManager.showSuperiorFortune(superiorStage, superiorPointer, superiorRatings,
                                                     superiorSwapActive, superiorGlitchFrame);
            break;

        case SETTINGS_MENU_SCREEN:
            displayManager.showSettingsMenu(settingsMenuIndex);
            break;

        case BOOT_LOGO_SETTINGS_SCREEN:
            displayManager.showBootLogoSettings(bootLogoSettingIndex, bootSuperiorLogoEnabled,
                                                 superiorBootLogoAnimation, bootLogoEnabled,
                                                 cyberNodeBootAnimation);
            break;

        case DIAL_APPS_SETTINGS_SCREEN: {
            const char* names[DIAL_APP_COUNT];
            for (uint8_t i = 0; i < DIAL_APP_COUNT; ++i) names[i] = dialAppRegistry[i].name;
            displayManager.showDialAppsSettings(dialAppsSettingsIndex, dialAppEnabled, names, DIAL_APP_COUNT);
            break;
        }

        case SLEEP_SETTINGS_SCREEN:
            displayManager.showSleepSettings(sleepSettingIndex, oledSleepEnabled, oledSleepTimeoutMinutes, oledContrast);
            break;

        case NAVIGATION_SETTINGS_SCREEN:
            displayManager.showNavigationSettings(navigationSettingIndex, navigationAutoPopup, navigationKeepScreenAwake);
            break;

        case HAPTIC_SETTINGS_SCREEN:
            displayManager.showHapticSettings(hapticSettingIndex, hapticManager.isEnabled(),
                                              hapticManager.navigationEnabled(), hapticManager.callsEnabled(),
                                              hapticManager.notificationsEnabled(), hapticManager.alertsEnabled(),
                                              hapticManager.strength());
            break;

        case NAVIGATION_SCREEN: {
            const NavigationSnapshot& nav = navigationManager.snapshot();
            static bool firstNavPageRenderDiag = false;
            const bool logFirstPage = !firstNavPageRenderDiag;
            if (logFirstPage) {
                firstNavPageRenderDiag = true;
                Serial.printf("[NAV_DIAG] first NAVIGATION_SCREEN page render begin hasMap=%u points=%u page=%u ui_stack_hwm=%u free_heap=%u\n",
                              (unsigned)nav.hasMap, (unsigned)nav.pointCount, (unsigned)navigationPage,
                              (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                              (unsigned)esp_get_free_heap_size());
            }
            if (navigationPage == 0) {
                NavStreamSnapshot streamSnapshot;
                if (copyNavStreamSnapshot(streamSnapshot)) displayManager.showNavigationStreamMap(streamSnapshot, nav, bleConnected, -1);
                else displayManager.showNavigationMap(nav, bleConnected, bleNavigationPayloadSupported, -1, navigationMapRecentered);
            } else displayManager.showNavigationInstructions(nav, bleConnected, bleNavigationPayloadSupported, -1);
            if (logFirstPage) {
                Serial.printf("[NAV_DIAG] first NAVIGATION_SCREEN page render complete ui_stack_hwm=%u free_heap=%u\n",
                              (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                              (unsigned)esp_get_free_heap_size());
            }
            break;
        }
        case NOTIFICATION_SETTINGS_SCREEN:
            displayManager.showNotificationSettings(notificationSettingIndex, notificationAlertsEnabled,
                                                    notificationPopupInApps, notificationBeepEnabled);
            break;
        case ALARM_SCREEN:
            displayManager.showAlarm(localAlarm.minuteOfDay, localAlarm.enabled, alarmEditField, localAlarm.tone, alarmAlertActive);
            break;
        case FACE_PICKER_SCREEN:
            showHome();
            break;
        case TIMER_SCREEN:
            displayManager.showTimer(localTimer.running ? (localTimer.deadlineMs-millis()) : localTimer.durationMs, localTimer.running, localTimer.completed, timerEditField, localTimer.tone, timerAlertActive);
            break;
        case SOUND_SETTINGS_SCREEN:
            displayManager.showSoundSettings(soundSettingIndex, soundManager.isEnabled(), soundManager.buttonSoundsEnabled(), soundManager.gameSoundsEnabled(), soundManager.volume());
            break;
    }
}


void renderWithTransition(TransitionDirection direction) {
    if (oledSleeping) {
        transitionActive = false;
        pendingTransitionEvent = BUTTON_NONE;
        displayManager.setPresentationDeferred(false);
        return;
    }

    
    displayManager.captureTransitionSourceFrame();

    
    
    
    displayManager.setPresentationDeferred(true);
    showCurrentScreen();
    displayManager.captureTransitionFrame();
    displayManager.setPresentationDeferred(false);

    transitionActive = true;
    transitionStep = 0;
    activeTransitionDirection = direction;
    lastTransitionFrame = millis();
    displayManager.drawTransitionMask(0, activeTransitionDirection);
}







static void incrementTimeDigit(uint8_t* d, uint8_t pos) {
    uint8_t maxValue;
    if (pos == 0) maxValue = 2;                      
    else if (pos == 1) maxValue = (d[0] == 2) ? 3 : 9; 
    else if (pos == 2 || pos == 4) maxValue = 5;     
    else maxValue = 9;
    d[pos] = (d[pos] >= maxValue) ? 0 : (uint8_t)(d[pos] + 1);
    if (d[0] == 2 && d[1] > 3) d[1] = 3;             
}

static void alarmEditDigit(uint8_t pos) {
    const uint8_t h = localAlarm.minuteOfDay / 60, m = localAlarm.minuteOfDay % 60;
    uint8_t d[4] = {(uint8_t)(h / 10), (uint8_t)(h % 10), (uint8_t)(m / 10), (uint8_t)(m % 10)};
    incrementTimeDigit(d, pos);
    localAlarm.minuteOfDay = (uint16_t)((d[0] * 10 + d[1]) * 60 + d[2] * 10 + d[3]);
}

static void timerEditDigit(uint8_t pos) {
    uint32_t sec = localTimer.durationMs / 1000UL;
    if (sec > 86399UL) sec = 86399UL;
    const uint8_t h = (uint8_t)(sec / 3600UL), m = (uint8_t)((sec / 60UL) % 60UL), s = (uint8_t)(sec % 60UL);
    uint8_t d[6] = {(uint8_t)(h / 10), (uint8_t)(h % 10), (uint8_t)(m / 10), (uint8_t)(m % 10),
                    (uint8_t)(s / 10), (uint8_t)(s % 10)};
    incrementTimeDigit(d, pos);
    sec = (uint32_t)(d[0] * 10 + d[1]) * 3600UL + (uint32_t)(d[2] * 10 + d[3]) * 60UL + (uint32_t)(d[4] * 10 + d[5]);
    localTimer.durationMs = sec * 1000UL;
}



static void timerCyclePreset() {
    static const uint32_t presetsMs[3] = {60000UL, 300000UL, 600000UL};
    uint8_t next = 0;
    for (uint8_t i = 0; i < 3; ++i)
        if (localTimer.durationMs == presetsMs[i]) { next = (uint8_t)((i + 1) % 3); break; }
    localTimer.durationMs = presetsMs[next];
}

void beginSuperiorRatingMove(uint8_t target) {
    if (superiorSwapActive || target > 2 || target == superiorPointer) return;
    superiorSwapFrom = superiorPointer;
    superiorSwapTo = target;
    superiorPointer = target; 
    superiorSwapActive = true;
    superiorGlitchFrame = 0;
    superiorSwapDeadline = millis() + 280;
    superiorLastGlitchFrame = millis();
    soundManager.play(SoundEvent::FORTUNE_GLITCH);
    displayManager.showSuperiorFortune(superiorStage, superiorPointer, superiorRatings, true, superiorGlitchFrame);
}

void goNextScreen() {
    if (transitionActive) return;

    if (currentScreen == NAVIGATION_SCREEN) {
        navigationPage = (navigationPage + 1) % 2;
        soundManager.play(SoundEvent::NEXT);
        showCurrentScreen();
        return;
    }
    if (currentScreen == GAME_PLAY_SCREEN) {
        soundManager.play(SoundEvent::NEXT);
        gameManager.handleInput(BUTTON_NEXT, millis());
        gameManager.draw(displayManager);
        return;
    }
    if (currentScreen == SUPERIOR_SCREEN) {
        if (!superiorCeoMode && superiorStage == 1) {
            if (!superiorSwapActive && superiorPointer > 0) beginSuperiorRatingMove(superiorPointer - 1);
            else if (!superiorSwapActive) soundManager.play(SoundEvent::INVALID);
        }
        return;
    }

    
    if (currentScreen == HOME_SCREEN) {
        currentScreen = APP_DIAL_SCREEN;
        soundManager.play(SoundEvent::NEXT);
        renderWithTransition(TRANSITION_RADIAL_REVEAL);
        return;
    }

    
    
    if (currentScreen == APP_DIAL_SCREEN) {
        const int count = dialEntryCount();
        if (count > 0) dialCursor = (dialCursor + 1) % count;
        soundManager.play(SoundEvent::NEXT);
        renderWithTransition(TRANSITION_CAROUSEL_NEXT);
        return;
    }
    if (currentScreen == FACE_PICKER_SCREEN) {
        homeFacePreview = (homeFacePreview + 1) % DisplayManager::homeFaceCount();
        applyFacePreview(true);
        soundManager.play(SoundEvent::NEXT);
        renderWithTransition(TRANSITION_CAROUSEL_NEXT);
        return;
    }

    switch (currentScreen) {
        case WEATHER_SCREEN:
            currentScreen = STEPS_SCREEN;
            stepsWindowIndex = 0;
            break;
        case STEPS_SCREEN:
            currentScreen = SYSTEM_SCREEN;
            systemWindowIndex = 0;
            break;
        case SYSTEM_SCREEN:
            currentScreen = HOME_SCREEN;
            break;
        case GAME_LIST_SCREEN:
            gameMenu.next();
            break;
        case SETTINGS_MENU_SCREEN:
            settingsMenuIndex = (settingsMenuIndex + 1) % 7;
            break;
        case BOOT_LOGO_SETTINGS_SCREEN:
            bootLogoSettingIndex = (bootLogoSettingIndex + 1) % 4;
            break;
        case NOTIFICATION_SETTINGS_SCREEN:
            notificationSettingIndex = (notificationSettingIndex + 1) % 3;
            break;
        case DIAL_APPS_SETTINGS_SCREEN:
            dialAppsSettingsIndex = (dialAppsSettingsIndex + 1) % DIAL_APP_COUNT;
            break;
        case SLEEP_SETTINGS_SCREEN:
            sleepSettingIndex = (sleepSettingIndex + 1) % 3;
            break;
        case NAVIGATION_SETTINGS_SCREEN:
            navigationSettingIndex = (navigationSettingIndex + 1) % 2;
            break;
        case HAPTIC_SETTINGS_SCREEN:
            hapticSettingIndex = (hapticSettingIndex + 1) % 7;
            break;
        case SOUND_SETTINGS_SCREEN:
            soundSettingIndex = (soundSettingIndex + 1) % 4;
            break;
        case ALARM_SCREEN:
            if (alarmEditField <= 3) alarmEditDigit(alarmEditField);
            else if (alarmEditField == 4) localAlarm.enabled = !localAlarm.enabled;
            else localAlarm.tone = (localAlarm.tone + 1) % 4;
            persistLocalAppSettings();
            break;
        case TIMER_SCREEN:
            if (!localTimer.running) timerEditField = (timerEditField + 1) % 8;
            break;
        case NOTIFICATIONS_SCREEN:
            if (watchData.notificationCount) notificationSelected = (notificationSelected + 1) % watchData.notificationCount;
            break;
        case NOTIFICATION_DETAIL_SCREEN:
            notificationDetailScrollLine++;
            break;
        case STOPWATCH_SCREEN:
            if (stopwatchRunning) {
                const uint32_t current = stopwatchAccumulatedMs + (millis() - stopwatchStartMs);
                if (stopwatchLapCount < 2) stopwatchLaps[stopwatchLapCount++] = current;
                else { stopwatchLaps[0] = stopwatchLaps[1]; stopwatchLaps[1] = current; }
            }
            break;
        case MENU_LIST_SCREEN:
            selectedMenuItem = (selectedMenuItem + 1) % 11;
            break;
        default:
            break;
    }

    soundManager.play(SoundEvent::NEXT);
    renderWithTransition(TRANSITION_FROM_LEFT);
}

void goPreviousScreen() {
    if (transitionActive) return;

    if (currentScreen == NAVIGATION_SCREEN) {
        closeNavigationScreen(true); 
        return;
    }
    if (currentScreen == GAME_PLAY_SCREEN) {
        
        soundManager.play(SoundEvent::BACK);
        gameManager.handleInput(BUTTON_BACK, millis());
        gameManager.draw(displayManager);
        return;
    }
    if (currentScreen == SUPERIOR_SCREEN && !superiorCeoMode && superiorStage == 1) {
        if (!superiorSwapActive && superiorPointer < 2) beginSuperiorRatingMove(superiorPointer + 1);
        else if (!superiorSwapActive) soundManager.play(SoundEvent::INVALID);
        return;
    }

    if (currentScreen == APP_DIAL_SCREEN) {
        const int count = dialEntryCount();
        if (count > 0) dialCursor = (dialCursor + count - 1) % count;
        soundManager.play(SoundEvent::BACK);
        renderWithTransition(TRANSITION_CAROUSEL_PREVIOUS);
        return;
    }
    if (currentScreen == FACE_PICKER_SCREEN) {
        const uint8_t count = DisplayManager::homeFaceCount();
        homeFacePreview = (homeFacePreview + count - 1) % count;
        applyFacePreview(true);
        soundManager.play(SoundEvent::BACK);
        renderWithTransition(TRANSITION_CAROUSEL_PREVIOUS);
        return;
    }

    
    if (currentScreen == BOOT_LOGO_SETTINGS_SCREEN ||
        currentScreen == DIAL_APPS_SETTINGS_SCREEN ||
        currentScreen == SLEEP_SETTINGS_SCREEN ||
        currentScreen == NAVIGATION_SETTINGS_SCREEN ||
        currentScreen == HAPTIC_SETTINGS_SCREEN ||
        currentScreen == SOUND_SETTINGS_SCREEN ||
        currentScreen == NOTIFICATION_SETTINGS_SCREEN) {
        soundManager.play(SoundEvent::BACK);
        currentScreen = SETTINGS_MENU_SCREEN;
        renderWithTransition(TRANSITION_FROM_RIGHT);
        return;
    }

    
    if (currentScreen == NOTIFICATION_DETAIL_SCREEN) {
        soundManager.play(SoundEvent::BACK);
        if (notificationDetailOpenedFromPopup) {
            notificationDetailOpenedFromPopup = false;
            currentScreen = notificationPopupReturnScreen;
        } else {
            currentScreen = NOTIFICATIONS_SCREEN;
        }
        notificationDetailScrollLine = 0;
        renderWithTransition(TRANSITION_FROM_RIGHT);
        return;
    }

    
    
    const bool isLauncherApp =
        currentScreen == WEATHER_SCREEN || currentScreen == STEPS_SCREEN ||
        currentScreen == SYSTEM_SCREEN || currentScreen == GAME_LIST_SCREEN ||
        currentScreen == SETTINGS_MENU_SCREEN || currentScreen == NOTIFICATIONS_SCREEN ||
        currentScreen == STOPWATCH_SCREEN || currentScreen == SUPERIOR_SCREEN ||
        currentScreen == ALARM_SCREEN ||
        currentScreen == TIMER_SCREEN;
    if (isLauncherApp) {
        soundManager.play(SoundEvent::BACK);
        if (currentScreen == SUPERIOR_SCREEN) {
            superiorCeoMode = false;
            superiorStageDeadline = 0;
            superiorSwapActive = false;
            superiorSwapDeadline = 0;
        }
        returnToAppLauncher();
        return;
    }

    if (currentScreen == MENU_LIST_SCREEN) {
        soundManager.play(SoundEvent::BACK);
        returnToMenuParent();
        return;
    }

    soundManager.play(SoundEvent::BACK);
    if (currentScreen == HOME_SCREEN) {
        
        
        currentScreen = APP_DIAL_SCREEN;
        renderWithTransition(TRANSITION_RADIAL_REVEAL_REVERSE);
        return;
    }
    currentScreen = HOME_SCREEN;
    renderWithTransition(TRANSITION_FROM_RIGHT);
}



void handleSelect() {
    if (alarmAlertActive || timerAlertActive) { alarmAlertActive=false; timerAlertActive=false; soundManager.stop(); showCurrentScreen(); return; }

    if (currentScreen == SUPERIOR_SCREEN && (superiorCeoMode || superiorStage >= 2)) return;
    if (currentScreen == SUPERIOR_SCREEN && superiorStage == 1 && superiorSwapActive) return;
    if (currentScreen != GAME_PLAY_SCREEN) soundManager.play(SoundEvent::SELECT); 

    switch (currentScreen) {

        case APP_DIAL_SCREEN:
            launchDialEntry(dialCursor);
            break;

        case MENU_LIST_SCREEN:
            rememberMenuLaunch(selectedMenuItem);

            if (selectedMenuItem == 0) {
                currentScreen = WEATHER_SCREEN;
                weatherWindowIndex = 0;
            }
            else if (selectedMenuItem == 1) {
                currentScreen = STEPS_SCREEN;
                stepsWindowIndex = 0;
            }
            else if (selectedMenuItem == 2) {
                currentScreen = SYSTEM_SCREEN;
                systemWindowIndex = 0;
            }
            else if (selectedMenuItem == 3) {
                currentScreen = GAME_LIST_SCREEN;
                gameMenu.reset();
            }
            else if (selectedMenuItem == 4) { currentScreen = NOTIFICATIONS_SCREEN; notificationSelected = 0; }
            else if (selectedMenuItem == 5) { currentScreen = STOPWATCH_SCREEN; stopwatchRunning=false; stopwatchAccumulatedMs=0; stopwatchLapCount=0; }
            else if (selectedMenuItem == 7) { currentScreen = SETTINGS_MENU_SCREEN; settingsMenuIndex = 0; }
            else if (selectedMenuItem == 8) { currentScreen = ALARM_SCREEN; alarmEditField=0; }
            else if (selectedMenuItem == 9) { currentScreen = TIMER_SCREEN; timerEditField=0; }
            else if (selectedMenuItem == 10) { openNavigationScreen(false, false); }
            else if (selectedMenuItem == 6) {
                currentScreen = SUPERIOR_SCREEN; superiorStage=0; superiorPointer=2;
                superiorRatings[0]=0; superiorRatings[1]=50; superiorRatings[2]=100;
                superiorCeoMode=false; superiorStageDeadline=0; superiorSwapActive=false; superiorSwapDeadline=0;
                soundManager.play(SoundEvent::FORTUNE_INTRO);
            }

            break;

        case WEATHER_SCREEN:
            weatherWindowIndex++;

            if (weatherWindowIndex > 3) {
                weatherWindowIndex = 0;
            }

            break;

        case STEPS_SCREEN:
            
            stepsWindowIndex++;

            if (stepsWindowIndex > 2) {
                stepsWindowIndex = 0;
            }
            break;

        case SYSTEM_SCREEN:
            
            systemWindowIndex++;

            if (systemWindowIndex > 2) {
                systemWindowIndex = 0;
            }
            break;

        case SUPERIOR_SCREEN:
            if (superiorCeoMode) {  }
            else if (superiorStage == 0) { superiorStage=1; soundManager.play(SoundEvent::FORTUNE_CONTINUE); displayManager.showSuperiorFortune(superiorStage, superiorPointer, superiorRatings); }
            else if (superiorStage == 1 && !superiorSwapActive && superiorRatings[superiorPointer] == 100) {
                superiorStage=2; superiorStageDeadline=millis()+3000;
                soundManager.play(SoundEvent::FORTUNE_YAY);
                if (!oledSleeping && !notificationPopupActive && !callOverlayVisible()) displayManager.showSuperiorFortune(superiorStage, superiorPointer, superiorRatings);
            }
            break;

        case SETTINGS_MENU_SCREEN:
            if (settingsMenuIndex == 0) {
                currentScreen = DIAL_APPS_SETTINGS_SCREEN;
                dialAppsSettingsIndex = 0;
            } else if (settingsMenuIndex == 1) {
                currentScreen = SLEEP_SETTINGS_SCREEN;
                sleepSettingIndex = 0;
            } else if (settingsMenuIndex == 2) {
                currentScreen = SOUND_SETTINGS_SCREEN;
                soundSettingIndex = 0;
            } else if (settingsMenuIndex == 3) {
                currentScreen = NOTIFICATION_SETTINGS_SCREEN;
                notificationSettingIndex = 0;
            } else if (settingsMenuIndex == 4) {
                currentScreen = NAVIGATION_SETTINGS_SCREEN;
                navigationSettingIndex = 0;
            } else if (settingsMenuIndex == 5) {
                currentScreen = HAPTIC_SETTINGS_SCREEN;
                hapticSettingIndex = 0;
            } else {
                currentScreen = BOOT_LOGO_SETTINGS_SCREEN;
                bootLogoSettingIndex = 0;
            }
            break;

        case BOOT_LOGO_SETTINGS_SCREEN:
            if (bootLogoSettingIndex == 0) {
                bootSuperiorLogoEnabled = !bootSuperiorLogoEnabled;
            } else if (bootLogoSettingIndex == 1) {
                superiorBootLogoAnimation = CyberNodeSettings::nextBootLogoAnimation(superiorBootLogoAnimation);
            } else if (bootLogoSettingIndex == 2) {
                bootLogoEnabled = !bootLogoEnabled;
            } else {
                cyberNodeBootAnimation = CyberNodeSettings::nextBootLogoAnimation(cyberNodeBootAnimation);
            }
            persistBootLogoSettings();
            break;

        case DIAL_APPS_SETTINGS_SCREEN:
            dialAppEnabled[dialAppsSettingsIndex] = !dialAppEnabled[dialAppsSettingsIndex];
            if (dialCursor >= dialEntryCount()) dialCursor = 0;
            persistLocalAppSettings();
            break;

        case SLEEP_SETTINGS_SCREEN:
            if (sleepSettingIndex == 0) {
                oledSleepEnabled = !oledSleepEnabled;
            } else if (sleepSettingIndex == 1) {
                oledSleepTimeoutMinutes = CyberNodeSettings::nextSleepMinutes(oledSleepTimeoutMinutes);
            } else {
                oledContrast = CyberNodeSettings::nextOledContrast(oledContrast);
                displayManager.setContrast(oledContrast);
            }
            persistSleepSettings();
            break;

        case HAPTIC_SETTINGS_SCREEN:
            if (hapticSettingIndex == 0) hapticManager.setEnabled(!hapticManager.isEnabled());
            else if (hapticSettingIndex == 1) hapticManager.setNavigationEnabled(!hapticManager.navigationEnabled());
            else if (hapticSettingIndex == 2) hapticManager.setCallsEnabled(!hapticManager.callsEnabled());
            else if (hapticSettingIndex == 3) hapticManager.setNotificationsEnabled(!hapticManager.notificationsEnabled());
            else if (hapticSettingIndex == 4) hapticManager.setAlertsEnabled(!hapticManager.alertsEnabled());
            else if (hapticSettingIndex == 5) hapticManager.setStrength((uint8_t)((hapticManager.strength() + 1U) % 3U));
            else hapticManager.trigger(HapticEvent::TEST);
            persistHapticSettings();
            break;

        case NAVIGATION_SETTINGS_SCREEN:
            if (navigationSettingIndex == 0) navigationAutoPopup = !navigationAutoPopup;
            else {
                navigationKeepScreenAwake = !navigationKeepScreenAwake;
                const uint32_t now = millis();
                if (navigationKeepScreenAwake && navigationManager.isGuidanceFresh(now)) {
                    openNavigationScreen(true, true);
                } else {
                    
                    
                    syncNavigationAwakeLease(now);
                    inputManager.markActivity(now);
                }
            }
            persistNavigationSettings();
            break;

        case NAVIGATION_SCREEN:
            if (navigationPage == 0 && navigationManager.snapshot().hasMap) {
                navigationMapRecentered = true;
                showCurrentScreen();
            }
            break;

        case NOTIFICATION_SETTINGS_SCREEN:
            if (notificationSettingIndex == 0) {
                notificationAlertsEnabled = !notificationAlertsEnabled;
                if (!notificationAlertsEnabled) {
                    pendingNotificationPopup = false;
                    notificationBeepQueueCount = 0;
                    notificationPopupActive = false;
                }
            } else if (notificationSettingIndex == 1) {
                notificationPopupInApps = !notificationPopupInApps;
            } else {
                notificationBeepEnabled = !notificationBeepEnabled;
                if (!notificationBeepEnabled) notificationBeepQueueCount = 0;
            }
            persistNotificationSettings();
            break;
        case ALARM_SCREEN:
            alarmEditField=(alarmEditField+1)%6;
            persistLocalAppSettings();
            break;
        case TIMER_SCREEN:
            if (!localTimer.running) {
                if (timerEditField <= 5) timerEditDigit(timerEditField);
                else if (timerEditField == 6) timerCyclePreset();
                else localTimer.tone=(localTimer.tone+1)%4;
                localTimer.completed=false; persistLocalAppSettings();
            }
            break;
        case STOPWATCH_SCREEN:
            
            if (stopwatchRunning) {
                stopwatchAccumulatedMs += millis() - stopwatchStartMs;
                stopwatchRunning = false;
            } else {
                stopwatchStartMs = millis();
                stopwatchRunning = true;
            }
            break;

        case NOTIFICATIONS_SCREEN:
            if (watchData.notificationCount > 0) {
                notificationDetailScrollLine = 0;
                notificationDetailOpenedFromPopup = false;
                currentScreen = NOTIFICATION_DETAIL_SCREEN;
            }
            break;

        case NOTIFICATION_DETAIL_SCREEN:
            if (notificationDetailOpenedFromPopup) {
                notificationDetailOpenedFromPopup = false;
                currentScreen = notificationPopupReturnScreen;
            } else {
                currentScreen = NOTIFICATIONS_SCREEN;
            }
            notificationDetailScrollLine = 0;
            break;

        case GAME_LIST_SCREEN:
            gameManager.startGame((uint8_t)gameMenu.selected(), millis());
            currentScreen = GAME_PLAY_SCREEN;
            break;

        case GAME_PLAY_SCREEN:
            gameManager.handleInput(BUTTON_SELECT, millis());
            gameManager.draw(displayManager);
            break;

        case SOUND_SETTINGS_SCREEN:
            if (soundSettingIndex == 0) soundManager.setEnabled(!soundManager.isEnabled());
            else if (soundSettingIndex == 1) soundManager.setButtonSounds(!soundManager.buttonSoundsEnabled());
            else if (soundSettingIndex == 2) soundManager.setGameSounds(!soundManager.gameSoundsEnabled());
            else soundManager.setVolume(soundManager.volume() < 80 ? 100 : (soundManager.volume() < 150 ? 200 : 40));
            persistSoundSettings();
            break;

        case FACE_PICKER_SCREEN:
            closeFacePicker(true);
            currentScreen = HOME_SCREEN;
            break;   

        case HOME_SCREEN:
            
            NodeBot::controller().greet(millis());
            break;
    }

    
    if (currentScreen != GAME_PLAY_SCREEN) {
        if (currentScreen == MENU_LIST_SCREEN && menuParentScreen == APP_DIAL_SCREEN)
            renderWithTransition(TRANSITION_FROM_LEFT);
        else if (currentScreen == NAVIGATION_SCREEN) showCurrentScreen();
        else if (currentScreen == WEATHER_SCREEN || currentScreen == STEPS_SCREEN || currentScreen == SYSTEM_SCREEN ||
                 currentScreen == GAME_LIST_SCREEN || currentScreen == NOTIFICATIONS_SCREEN || currentScreen == STOPWATCH_SCREEN ||
                 currentScreen == SUPERIOR_SCREEN || currentScreen == ALARM_SCREEN || currentScreen == TIMER_SCREEN ||
                 currentScreen == NAVIGATION_SETTINGS_SCREEN)
            renderWithTransition(TRANSITION_RADIAL_REVEAL);
        else
            renderWithTransition(TRANSITION_FROM_CENTER);
    }
}





void IRAM_ATTR inputButtonEdgeIsr() {
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    TaskHandle_t target = inputTaskHandle;
    if (target != nullptr) vTaskNotifyGiveFromISR(target, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

void inputTask(void* parameter) {
    (void)parameter;
    bool firstScan = true;

    for (;;) {
        
        
        
        
        
        
        if (!firstScan && !inputManager.needsPolling()) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        firstScan = false;
        ButtonEvent event = inputManager.update(oledSleeping);

        if (event != BUTTON_NONE) {
            const char* eventName = "UNKNOWN";
            switch (event) {
                case BUTTON_NEXT: eventName = "NEXT"; break;
                case BUTTON_SELECT: eventName = "SELECT"; break;
                case BUTTON_BACK: eventName = "BACK"; break;
                case BUTTON_LONG_SELECT: eventName = "LONG_SELECT"; break;
                case BUTTON_WAKE: eventName = "WAKE"; break;
                case BUTTON_NONE: eventName = "NONE"; break;
            }
            if (buttonEventQueue != nullptr) {
                
                
                if (xQueueSend(buttonEventQueue, &event, pdMS_TO_TICKS(20)) != pdPASS) {
                    Serial.printf("[INPUT] WARNING: %s queue send failed\n", eventName);
                } else {
                    notifyUiTask();
                    CN_DEBUG_PRINTF("[INPUT] %s detected\n", eventName);
                }
            }
        }

        if (inputManager.needsPolling()) vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void uiTask(void* parameter);
void processUiIteration();






int findTopLevelJsonKey(const String& json, const char* key) {
    int objectDepth = 0;
    int arrayDepth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < (int)json.length(); ++i) {
        char c = json[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') {
            int start = i + 1;
            int end = start;
            bool keyEscaped = false;
            while (end < (int)json.length()) {
                char k = json[end];
                if (!keyEscaped && k == '"') break;
                if (!keyEscaped && k == '\\') keyEscaped = true;
                else keyEscaped = false;
                end++;
            }
            if (end >= (int)json.length()) return -1;
            const size_t keyLength = strlen(key);
            if (objectDepth == 1 && arrayDepth == 0 &&
                end - start == (int)keyLength &&
                memcmp(json.c_str() + start, key, keyLength) == 0) {
                int after = end + 1;
                while (after < (int)json.length() && (json[after] == ' ' || json[after] == '\t' || json[after] == '\r' || json[after] == '\n')) after++;
                if (after < (int)json.length() && json[after] == ':') return start - 1;
            }
            i = end;
        } else if (c == '{') objectDepth++;
        else if (c == '}') objectDepth--;
        else if (c == '[') arrayDepth++;
        else if (c == ']') arrayDepth--;
    }
    return -1;
}

bool jsonGetString(const String& json, const char* key, char* output, size_t outputSize) {
    if (outputSize == 0) return false;
    int keyPos = findTopLevelJsonKey(json, key);
    if (keyPos < 0) return false;
    int colon = json.indexOf(':', keyPos + (int)strlen(key) + 2);
    if (colon < 0) return false;
    int pos = colon + 1;
    while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
    if (pos >= (int)json.length() || json[pos] != '\"') return false;
    pos++;
    size_t out = 0;
    while (pos < (int)json.length()) {
        char c = json[pos++];
        if (c == '\"') { output[out] = '\0'; return true; }
        if (c == '\\' && pos < (int)json.length()) {
            char escaped = json[pos++];
            if (escaped == 'n') c = ' ';
            else if (escaped == 'r' || escaped == 't') c = ' ';
            else c = escaped;
        }
        if (out + 1 >= outputSize) { output[0] = '\0'; return false; }
        output[out++] = c;
    }
    output[0] = '\0';
    return false;
}





bool jsonGetStringBounded(const String& json, const char* key, char* output, size_t outputSize) {
    if (outputSize == 0) return false;
    int keyPos = findTopLevelJsonKey(json, key);
    if (keyPos < 0) return false;
    int colon = json.indexOf(':', keyPos + (int)strlen(key) + 2);
    if (colon < 0) return false;
    int pos = colon + 1;
    while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
    if (pos >= (int)json.length() || json[pos] != '"') return false;
    pos++;

    size_t out = 0;
    bool truncated = false;
    while (pos < (int)json.length()) {
        char c = json[pos++];
        if (c == '"') {
            
            
            size_t leadIndex = out;
            while (leadIndex > 0 && (((uint8_t)output[leadIndex - 1] & 0xC0U) == 0x80U)) --leadIndex;
            if (leadIndex > 0) {
                --leadIndex;
                const uint8_t lead = (uint8_t)output[leadIndex];
                size_t expected = 1;
                if ((lead & 0xE0U) == 0xC0U) expected = 2;
                else if ((lead & 0xF0U) == 0xE0U) expected = 3;
                else if ((lead & 0xF8U) == 0xF0U) expected = 4;
                if (expected > out - leadIndex) out = leadIndex;
            }
            if (truncated && out + 2 < outputSize) {
                output[out++] = '.';
                output[out++] = '.';
            }
            output[out] = '\0';
            return true;
        }
        if (c == '\\' && pos < (int)json.length()) {
            const char escaped = json[pos++];
            if (escaped == 'n' || escaped == 'r' || escaped == 't') c = ' ';
            else c = escaped;
        }
        if (out + 1 < outputSize) output[out++] = c;
        else truncated = true;
    }
    output[0] = '\0';
    return false;
}

bool jsonGetInt64(const String& json, const char* key, int64_t& output) {
    int keyPos = findTopLevelJsonKey(json, key);
    if (keyPos < 0) return false;
    int colon = json.indexOf(':', keyPos + (int)strlen(key) + 2);
    if (colon < 0) return false;
    int start = colon + 1;
    while (start < (int)json.length() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\r' || json[start] == '\n')) start++;
    int end = start;
    if (end < (int)json.length() && json[end] == '-') end++;
    while (end < (int)json.length() && isDigit(json[end])) end++;
    if (end == start || (end == start + 1 && json[start] == '-')) return false;
    if (end < (int)json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']' &&
        json[end] != ' ' && json[end] != '\t' && json[end] != '\r' && json[end] != '\n') return false;
    String numberText = json.substring(start, end);
    output = strtoll(numberText.c_str(), nullptr, 10);
    return true;
}

bool jsonGetInt(const String& json, const char* key, int& output) {
    int64_t value = output;
    if (!jsonGetInt64(json, key, value)) return false;
    if (value < INT32_MIN || value > INT32_MAX) return false;
    output = (int)value;
    return true;
}

bool jsonGetBool(const String& json, const char* key, bool& output) {
    const int keyPos = findTopLevelJsonKey(json, key);
    if (keyPos < 0) return false;
    const int colon = json.indexOf(':', keyPos + (int)strlen(key) + 2);
    if (colon < 0) return false;
    int pos = colon + 1;
    while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) ++pos;
    const auto isValueDelimiter = [&json](int index) {
        if (index >= (int)json.length()) return true;
        const char c = json[index];
        return c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    if (json.startsWith("true", pos) && isValueDelimiter(pos + 4)) {
        output = true;
        return true;
    }
    if (json.startsWith("false", pos) && isValueDelimiter(pos + 5)) {
        output = false;
        return true;
    }
    return false;
}

int findJsonObjectEnd(const String& json, int start) {
    if (start < 0 || start >= (int)json.length() || json[start] != '{') return -1;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (int i = start; i < (int)json.length(); ++i) {
        char c = json[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') quoted = true;
        else if (c == '{') depth++;
        else if (c == '}' && --depth == 0) return i + 1;
    }
    return -1;
}

int findJsonArrayEnd(const String& json, int start) {
    if (start < 0 || start >= (int)json.length() || json[start] != '[') return -1;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (int i = start; i < (int)json.length(); ++i) {
        char c = json[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') quoted = true;
        else if (c == '[') depth++;
        else if (c == ']' && --depth == 0) return i + 1;
    }
    return -1;
}

void skipJsonSeparators(const String& json, int& pos, int end) {
    while (pos < end && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' ||
                         json[pos] == '\n' || json[pos] == ',')) pos++;
}

bool parseStepHistory(const String& json, WatchData& parsed) {
    int keyPos = findTopLevelJsonKey(json, "stepHistory");
    if (keyPos < 0) return false;
    int colon = json.indexOf(':', keyPos + (int)strlen("stepHistory") + 2);
    if (colon < 0) return false;
    int open = colon + 1;
    while (open < (int)json.length() && (json[open] == ' ' || json[open] == '\t' ||
           json[open] == '\r' || json[open] == '\n')) open++;
    int arrayEnd = findJsonArrayEnd(json, open);
    if (arrayEnd < 0) return false;

    memset(parsed.stepHistory, 0, sizeof(parsed.stepHistory));
    const char* defaultDays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    for (uint8_t i = 0; i < 7; ++i)
        strlcpy(parsed.stepHistoryDays[i], defaultDays[i], sizeof(parsed.stepHistoryDays[i]));

    int pos = open + 1;
    const int contentEnd = arrayEnd - 1;
    uint8_t index = 0;
    while (pos < contentEnd) {
        skipJsonSeparators(json, pos, contentEnd);
        if (pos >= contentEnd) break;
        if (index >= 7) return false;

        if (json[pos] == '{') {
            int objectEnd = findJsonObjectEnd(json, pos);
            if (objectEnd < 0 || objectEnd > contentEnd) return false;
            String object = json.substring(pos, objectEnd);
            int steps = -1;
            if (!jsonGetInt(object, "steps", steps) || steps < 0 || steps > 200000) return false;
            parsed.stepHistory[index] = steps;

            char day[8] = {0};
            if (findTopLevelJsonKey(object, "day") >= 0) {
                if (!jsonGetString(object, "day", day, sizeof(day))) return false;
                strlcpy(parsed.stepHistoryDays[index], day, sizeof(parsed.stepHistoryDays[index]));
            } else if (findTopLevelJsonKey(object, "date") >= 0) {
                if (!jsonGetString(object, "date", day, sizeof(day))) return false;
                strlcpy(parsed.stepHistoryDays[index], day, sizeof(parsed.stepHistoryDays[index]));
            }
            pos = objectEnd;
        } else {
            int numberStart = pos;
            while (pos < contentEnd && isDigit(json[pos])) pos++;
            if (pos == numberStart) return false;
            long value = strtol(json.substring(numberStart, pos).c_str(), nullptr, 10);
            if (value < 0 || value > 200000) return false;
            parsed.stepHistory[index] = (int32_t)value;
        }
        index++;
        while (pos < contentEnd && (json[pos] == ' ' || json[pos] == '\t' ||
               json[pos] == '\r' || json[pos] == '\n')) pos++;
        if (pos < contentEnd && json[pos] != ',') return false;
    }
    return true;
}

static bool sameNotificationIdentity(const WatchNotification& a, const WatchNotification& b) {
    
    
    if (a.id[0] && b.id[0] && strcmp(a.id, b.id) == 0) return true;
    return strcmp(a.app, b.app) == 0 && strcmp(a.title, b.title) == 0 &&
           strcmp(a.content, b.content) == 0 && strcmp(a.timestamp, b.timestamp) == 0;
}

static uint8_t countNewNotifications(const WatchData& previous, const WatchData& current,
                                     int& firstNewIndex) {
    firstNewIndex = -1;
    uint8_t count = 0;
    for (uint8_t i = 0; i < current.notificationCount; ++i) {
        bool existed = false;
        for (uint8_t j = 0; j < previous.notificationCount; ++j) {
            if (sameNotificationIdentity(current.notifications[i], previous.notifications[j])) {
                existed = true;
                break;
            }
        }
        if (!existed) {
            if (firstNewIndex < 0) firstNewIndex = (int)i;
            if (count < 255U) ++count;
        }
    }
    return count;
}

bool parseNotifications(const String& json, WatchData& parsed) {
    int keyPos = findTopLevelJsonKey(json, "notifications");
    if (keyPos < 0) return false;
    int colon = json.indexOf(':', keyPos + (int)strlen("notifications") + 2);
    if (colon < 0) return false;
    int open = colon + 1;
    while (open < (int)json.length() && (json[open] == ' ' || json[open] == '\t' ||
           json[open] == '\r' || json[open] == '\n')) open++;
    int arrayEnd = findJsonArrayEnd(json, open);
    if (arrayEnd < 0) return false;

    parsed.notificationCount = 0;
    memset(parsed.notifications, 0, sizeof(parsed.notifications));
    int pos = open + 1;
    const int contentEnd = arrayEnd - 1;
    while (pos < contentEnd) {
        skipJsonSeparators(json, pos, contentEnd);
        if (pos >= contentEnd) break;
        if (json[pos] != '{') return false;
        int objectEnd = findJsonObjectEnd(json, pos);
        if (objectEnd < 0 || objectEnd > contentEnd) return false;
        String object = json.substring(pos, objectEnd);

        if (parsed.notificationCount < WATCH_NOTIFICATION_MAX) {
            WatchNotification& notification = parsed.notifications[parsed.notificationCount];
            const bool fieldsOk =
                (findTopLevelJsonKey(object, "id") < 0 || jsonGetString(object, "id", notification.id, sizeof(notification.id))) &&
                (findTopLevelJsonKey(object, "app") < 0 || jsonGetStringBounded(object, "app", notification.app, sizeof(notification.app))) &&
                (findTopLevelJsonKey(object, "title") < 0 || jsonGetStringBounded(object, "title", notification.title, sizeof(notification.title))) &&
                (findTopLevelJsonKey(object, "heading") < 0 || notification.title[0] || jsonGetStringBounded(object, "heading", notification.title, sizeof(notification.title))) &&
                (findTopLevelJsonKey(object, "content") < 0 || jsonGetStringBounded(object, "content", notification.content, sizeof(notification.content))) &&
                (findTopLevelJsonKey(object, "body") < 0 || notification.content[0] || jsonGetStringBounded(object, "body", notification.content, sizeof(notification.content))) &&
                (findTopLevelJsonKey(object, "message") < 0 || notification.content[0] || jsonGetStringBounded(object, "message", notification.content, sizeof(notification.content))) &&
                (findTopLevelJsonKey(object, "timestamp") < 0 || jsonGetStringBounded(object, "timestamp", notification.timestamp, sizeof(notification.timestamp)));
            if (!fieldsOk || (!notification.title[0] && !notification.content[0])) return false;
            parsed.notificationCount++;
        }
        
        pos = objectEnd;
        while (pos < contentEnd && (json[pos] == ' ' || json[pos] == '\t' ||
               json[pos] == '\r' || json[pos] == '\n')) pos++;
        if (pos < contentEnd && json[pos] != ',') return false;
    }
    return true;
}

bool parseWatchPayload(const String& json, WatchData& parsed) {
    if (json.length() == 0 || json.length() > 1024) return false;
    bool updated = false;
    const int64_t previousEpoch = parsed.epochSeconds;
    char previousDate[sizeof(parsed.date)];
    char previousTime[sizeof(parsed.time)];
    strlcpy(previousDate, parsed.date, sizeof(previousDate));
    strlcpy(previousTime, parsed.time, sizeof(previousTime));
    bool timeWasProvided = findTopLevelJsonKey(json, "time") >= 0;
    bool dateWasProvided = findTopLevelJsonKey(json, "date") >= 0;
    bool epochWasProvided = findTopLevelJsonKey(json, "epoch") >= 0;
    bool goalWasProvided = false;
    bool percentWasProvided = false;
    bool historyWasProvided = findTopLevelJsonKey(json, "stepHistory") >= 0;
    bool notificationsWereProvided = findTopLevelJsonKey(json, "notifications") >= 0;

    if (timeWasProvided) {
        if (!jsonGetString(json, "time", parsed.time, sizeof(parsed.time))) return false;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "date") >= 0) {
        if (!jsonGetString(json, "date", parsed.date, sizeof(parsed.date))) return false;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "weather") >= 0) {
        if (!jsonGetString(json, "weather", parsed.weather, sizeof(parsed.weather))) return false;
        updated = true;
    }

    int number = 0;
    if (findTopLevelJsonKey(json, "temp") >= 0 || findTopLevelJsonKey(json, "temperature") >= 0) {
        bool gotTemperature = findTopLevelJsonKey(json, "temp") >= 0
            ? jsonGetInt(json, "temp", number)
            : jsonGetInt(json, "temperature", number);
        if (!gotTemperature || number < -50 || number > 80) return false;
        parsed.temperature = (int16_t)number;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "humidity") >= 0) {
        if (!jsonGetInt(json, "humidity", number) || number < 0 || number > 100) return false;
        parsed.humidity = (uint8_t)number;
        updated = true;
    }
    bool stepsWereProvided = findTopLevelJsonKey(json, "steps") >= 0;
    if (stepsWereProvided) {
        if (!jsonGetInt(json, "steps", number) || number < 0 || number > 200000) return false;
        parsed.steps = number;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "aqi") >= 0) {
        if (!jsonGetInt(json, "aqi", number) || number < -1 || number > 1000) return false;
        parsed.aqi = (int16_t)number;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "aqiStatus") >= 0) {
        if (!jsonGetString(json, "aqiStatus", parsed.aqiStatus, sizeof(parsed.aqiStatus))) return false;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "precipitationChance") >= 0) {
        if (!jsonGetInt(json, "precipitationChance", number) || number < 0 || number > 100) return false;
        parsed.precipitationChance = (uint8_t)number;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "precipitation") >= 0) {
        if (!jsonGetString(json, "precipitation", parsed.precipitation, sizeof(parsed.precipitation))) return false;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "dailyGoal") >= 0) {
        if (!jsonGetInt(json, "dailyGoal", number) || number < 100 || number > 100000) return false;
        parsed.dailyGoal = number;
        goalWasProvided = true;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "goalPercent") >= 0) {
        if (!jsonGetInt(json, "goalPercent", number) || number < 0 || number > 100) return false;
        parsed.goalPercent = (uint8_t)number;
        percentWasProvided = true;
        updated = true;
    }

    int64_t incomingEpoch = 0;
    if (epochWasProvided) {
        if (!jsonGetInt64(json, "epoch", incomingEpoch) ||
            incomingEpoch < 0 || incomingEpoch > 4102444800LL) return false;
        updated = true;
    }
    if (findTopLevelJsonKey(json, "timezoneOffsetMinutes") >= 0) {
        if (!jsonGetInt(json, "timezoneOffsetMinutes", number) || number < -720 || number > 840) return false;
        parsed.timezoneOffsetMinutes = (int16_t)number;
        updated = true;
    }

    if (historyWasProvided) {
        if (!parseStepHistory(json, parsed)) return false;
        updated = true;
    }
    if (notificationsWereProvided) {
        if (!parseNotifications(json, parsed)) return false;
        updated = true;
    }

    if (parsed.dailyGoal <= 0) parsed.dailyGoal = 10000;
    if (!percentWasProvided && (goalWasProvided || stepsWereProvided)) {
        int64_t progress = ((int64_t)parsed.steps * 100LL) / parsed.dailyGoal;
        parsed.goalPercent = (uint8_t)(progress > 100 ? 100 : (progress < 0 ? 0 : progress));
    }
    if (!historyWasProvided && stepsWereProvided) {
        parsed.stepHistory[6] = parsed.steps;
    }

    if (!updated) return false;
    if (parsed.steps < 0 || parsed.steps > 200000 || parsed.temperature < -50 || parsed.temperature > 80 ||
        parsed.humidity > 100 || parsed.aqi < -1 || parsed.aqi > 1000 ||
        parsed.precipitationChance > 100 || parsed.dailyGoal < 100 || parsed.dailyGoal > 100000 ||
        parsed.goalPercent > 100 || parsed.notificationCount > WATCH_NOTIFICATION_MAX ||
        parsed.timezoneOffsetMinutes < -720 || parsed.timezoneOffsetMinutes > 840 ||
        parsed.epochSeconds < 0 || parsed.epochSeconds > 4102444800LL) return false;
    if (timeWasProvided) {
        int hours = 0, minutes = 0;
        if (!CyberNodeClock::parseHHMM(parsed.time, hours, minutes)) return false;
    }

    
    
    
    
    
    
    if (epochWasProvided && !timeWasProvided) {
        parsed.epochSeconds = incomingEpoch;
    } else if (timeWasProvided || dateWasProvided) {
        int64_t resolvedEpoch = 0;
        bool epochCorrected = false;
        const bool incomingEpochUsable = epochWasProvided && incomingEpoch > 0;
        const bool hasAbsoluteClock = CyberNodeClock::resolveEpoch(
            parsed.date, dateWasProvided, parsed.time, timeWasProvided,
            incomingEpoch, incomingEpochUsable, parsed.timezoneOffsetMinutes,
            previousEpoch, previousDate, previousTime,
            resolvedEpoch, epochCorrected);
        if (hasAbsoluteClock) {
            parsed.epochSeconds = resolvedEpoch;
            if (epochCorrected) {
                Serial.printf("[TIME] BLE local time/date disagreed with epoch; using %s %s offset=%d min. epoch %lld -> %lld\n",
                              parsed.date, parsed.time, (int)parsed.timezoneOffsetMinutes,
                              (long long)incomingEpoch, (long long)resolvedEpoch);
            } else if (timeWasProvided && !epochWasProvided && resolvedEpoch > 0) {
                Serial.printf("[TIME] Built epoch from local %s %s offset=%d min: %lld\n",
                              parsed.date, parsed.time, (int)parsed.timezoneOffsetMinutes,
                              (long long)resolvedEpoch);
            }
        } else if (!epochWasProvided) {
            
            
            parsed.epochSeconds = 0;
        }
    }
    for (uint8_t i = 0; i < 7; ++i)
        if (parsed.stepHistory[i] < 0 || parsed.stepHistory[i] > 200000) return false;
    return true;
}

enum CallPayloadParseResult : uint8_t { CALL_PAYLOAD_NOT_CALL, CALL_PAYLOAD_INVALID, CALL_PAYLOAD_VALID };

static bool validCallId(const char* id) {
    
    
    if (!id || !id[0] || strlen(id) >= CALL_ID_CAPACITY) return false;
    for (const char* p = id; *p; ++p) {
        const char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

static bool isTerminalCallState(const char* state) {
    return state && (strcmp(state, "REJECTED") == 0 || strcmp(state, "MISSED") == 0 ||
                     strcmp(state, "ENDED") == 0 || strcmp(state, "CANCELLED") == 0);
}

static uint8_t parseCallBlePayload(const String& json, CallBleEvent& event) {
    memset(&event, 0, sizeof(event));
    const int typeKey = findTopLevelJsonKey(json, "type");
    if (typeKey >= 0) {
        char type[16] = {0};
        if (!jsonGetString(json, "type", type, sizeof(type))) return CALL_PAYLOAD_NOT_CALL;
        if (strcmp(type, "call") != 0) return CALL_PAYLOAD_NOT_CALL;

        char eventName[20] = {0};
        if (!jsonGetString(json, "event", eventName, sizeof(eventName))) return CALL_PAYLOAD_INVALID;
        if (!jsonGetString(json, "callId", event.callId, sizeof(event.callId)) || !validCallId(event.callId))
            return CALL_PAYLOAD_INVALID;
        if (findTopLevelJsonKey(json, "name") >= 0 &&
            !jsonGetStringBounded(json, "name", event.name, sizeof(event.name))) return CALL_PAYLOAD_INVALID;
        if (findTopLevelJsonKey(json, "phone") >= 0 &&
            !jsonGetStringBounded(json, "phone", event.phone, sizeof(event.phone))) return CALL_PAYLOAD_INVALID;
        if (findTopLevelJsonKey(json, "reason") >= 0 &&
            !jsonGetStringBounded(json, "reason", event.reason, sizeof(event.reason))) return CALL_PAYLOAD_INVALID;

        if (strcmp(eventName, "incoming") == 0) {
            event.kind = CALL_BLE_INCOMING;
            if (findTopLevelJsonKey(json, "state") >= 0) {
                if (!jsonGetString(json, "state", event.state, sizeof(event.state)) ||
                    strcmp(event.state, "RINGING") != 0) return CALL_PAYLOAD_INVALID;
            } else {
                strlcpy(event.state, "RINGING", sizeof(event.state));
            }
            return CALL_PAYLOAD_VALID;
        }
        if (strcmp(eventName, "state_changed") == 0) {
            event.kind = CALL_BLE_STATE_CHANGED;
            if (!jsonGetString(json, "state", event.state, sizeof(event.state))) return CALL_PAYLOAD_INVALID;
            if (strcmp(event.state, "RINGING") != 0 && strcmp(event.state, "ACTIVE") != 0 &&
                !isTerminalCallState(event.state)) return CALL_PAYLOAD_INVALID;
            return CALL_PAYLOAD_VALID;
        }
        if (strcmp(eventName, "ended") == 0) {
            event.kind = CALL_BLE_ENDED;
            if (findTopLevelJsonKey(json, "state") >= 0) {
                if (!jsonGetString(json, "state", event.state, sizeof(event.state)) ||
                    !isTerminalCallState(event.state)) return CALL_PAYLOAD_INVALID;
            } else {
                strlcpy(event.state, "ENDED", sizeof(event.state));
            }
            return CALL_PAYLOAD_VALID;
        }
        if (strcmp(eventName, "command_result") == 0) {
            event.kind = CALL_BLE_COMMAND_RESULT;
            int requestId = 0;
            if (!jsonGetString(json, "action", event.action, sizeof(event.action)) ||
                (strcmp(event.action, "ANSWER") != 0 && strcmp(event.action, "REJECT") != 0) ||
                !jsonGetInt(json, "requestId", requestId) || requestId <= 0 ||
                !jsonGetBool(json, "success", event.success)) return CALL_PAYLOAD_INVALID;
            event.requestId = (uint32_t)requestId;
            return CALL_PAYLOAD_VALID;
        }
        return CALL_PAYLOAD_INVALID;
    }

    
    
    
    if (findTopLevelJsonKey(json, "call") < 0) return CALL_PAYLOAD_NOT_CALL;
    bool isCall = false;
    if (!jsonGetBool(json, "call", isCall)) return CALL_PAYLOAD_INVALID;
    strlcpy(event.callId, "LEGACY_CALL", sizeof(event.callId));
    if (findTopLevelJsonKey(json, "Name") >= 0) {
        if (!jsonGetStringBounded(json, "Name", event.name, sizeof(event.name))) return CALL_PAYLOAD_INVALID;
    } else if (findTopLevelJsonKey(json, "name") >= 0) {
        if (!jsonGetStringBounded(json, "name", event.name, sizeof(event.name))) return CALL_PAYLOAD_INVALID;
    }
    if (findTopLevelJsonKey(json, "phone") >= 0 &&
        !jsonGetStringBounded(json, "phone", event.phone, sizeof(event.phone))) return CALL_PAYLOAD_INVALID;
    if (isCall) {
        event.kind = CALL_BLE_INCOMING;
        strlcpy(event.state, "RINGING", sizeof(event.state));
    } else {
        event.kind = CALL_BLE_ENDED;
        strlcpy(event.state, "ENDED", sizeof(event.state));
    }
    return CALL_PAYLOAD_VALID;
}





class CyberNodeServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        bleConnected = true;
        blePeerAttMtu = BLE_DEFAULT_ATT_MTU;
        blePeerMtuNegotiated = false;
        bleNavigationPayloadSupported = false;
        pendingBleSoundEvent = 1;
        notifyUiTask();
        const uint16_t connId = server != nullptr ? server->getConnId() : 0;
        const uint16_t peerMtu = server != nullptr ? server->getPeerMTU(connId) : BLE_DEFAULT_ATT_MTU;
        if (peerMtu >= BLE_DEFAULT_ATT_MTU) {
            blePeerAttMtu = peerMtu;
            blePeerMtuNegotiated = peerMtu > BLE_DEFAULT_ATT_MTU;
            bleNavigationPayloadSupported = peerMtu >= 243;
        }
        Serial.printf("[BLE] Smartphone connected (connId=%u, localMTU=%u, peerMTU=%u)\n",
                      connId, BLEDevice::getMTU(), (unsigned)blePeerAttMtu);
        Serial.println("[BLE] Waiting for negotiated ATT MTU event; default peer MTU is 23");
    }

    void onDisconnect(BLEServer* server) override {
        (void)server;
        bleConnected = false;
        blePeerAttMtu = BLE_DEFAULT_ATT_MTU;
        blePeerMtuNegotiated = false;
        bleNavigationPayloadSupported = false;
        pendingBleSoundEvent = 2;
        notifyUiTask();
        Serial.println("[BLE] Smartphone disconnected; peer MTU reset to 23");
        BLEDevice::startAdvertising();
    }

#if defined(CONFIG_BLUEDROID_ENABLED)
    void onMtuChanged(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
        if (param == nullptr) return;
        const uint16_t eventMtu = param->mtu.mtu;
        const uint16_t peerMtu = (server != nullptr)
            ? server->getPeerMTU(param->mtu.conn_id)
            : eventMtu;
        const uint16_t negotiated = (peerMtu >= BLE_DEFAULT_ATT_MTU) ? peerMtu : eventMtu;
        if (negotiated >= BLE_DEFAULT_ATT_MTU) {
            blePeerAttMtu = negotiated;
            blePeerMtuNegotiated = true;
            bleNavigationPayloadSupported = negotiated >= 243;
        }
        notifyUiTask();
        Serial.printf("[BLE] MTU event: connId=%u eventMTU=%u peerMTU=%u maxNotifyPayload=%u\n",
                      param->mtu.conn_id, eventMtu, (unsigned)blePeerAttMtu,
                      (unsigned)(blePeerAttMtu > 3 ? blePeerAttMtu - 3 : 0));
    }
#elif defined(CONFIG_NIMBLE_ENABLED)
    void onMtuChanged(BLEServer* server, ble_gap_conn_desc* desc, uint16_t mtu) override {
        (void)server;
        (void)desc;
        if (mtu >= BLE_DEFAULT_ATT_MTU) {
            blePeerAttMtu = mtu;
            blePeerMtuNegotiated = true;
            bleNavigationPayloadSupported = mtu >= 243;
        }
        notifyUiTask();
        Serial.printf("[BLE] MTU event: negotiatedMTU=%u maxNotifyPayload=%u\n",
                      (unsigned)blePeerAttMtu,
                      (unsigned)(blePeerAttMtu > 3 ? blePeerAttMtu - 3 : 0));
    }
#endif
};




class CyberNodeTxCallbacks : public BLECharacteristicCallbacks {
    void onStatus(BLECharacteristic* characteristic, BLECharacteristicCallbacks::Status status, uint32_t code) override {
        const size_t length = characteristic != nullptr ? characteristic->getLength() : 0;
        if (status == BLECharacteristicCallbacks::SUCCESS_NOTIFY) {
            bleLastTxNotifyResult = 1;
        } else {
            bleLastTxNotifyResult = 2;
        }
        switch (status) {
            case BLECharacteristicCallbacks::SUCCESS_NOTIFY:
                Serial.printf("[BLE TX] Notification accepted by GATT stack (%u bytes).\n", (unsigned)length);
                break;
            case BLECharacteristicCallbacks::ERROR_NOTIFY_DISABLED:
                Serial.println("[BLE TX] Notification not sent: client notifications are disabled (CCCD).");
                break;
            case BLECharacteristicCallbacks::ERROR_NO_CLIENT:
                Serial.println("[BLE TX] Notification not sent: no connected client.");
                break;
            case BLECharacteristicCallbacks::ERROR_GATT:
                Serial.printf("[BLE TX] GATT submission failed (status=%lu).\n", (unsigned long)code);
                break;
            default:
                Serial.printf("[BLE TX] Notification status=%u code=%lu length=%u\n",
                              (unsigned)status, (unsigned long)code, (unsigned)length);
                break;
        }
    }
};


class CyberNodeRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        
        
        
        uint8_t* rawBytes = characteristic->getData();
        const size_t rawLength = characteristic->getLength();
        CN_DEBUG_PRINTF("[BLE] RX payload length: %u bytes\n", (unsigned)rawLength);
        if (rawBytes == nullptr || rawLength == 0) return;
        
        
        
        if (rawLength >= 3 && rawBytes[0] == 'N' && rawBytes[1] == 'V' &&
            rawBytes[2] == NAV_STREAM_VERSION) {
            if (navStreamRxQueue == nullptr || rawLength > NAV_STREAM_MAX_PACKET) {
                Serial.println("[NAV_STREAM] rejected: stream queue unavailable or packet exceeds fixed bound");
                return;
            }
            NavStreamRxItem item{};
            item.length = (uint16_t)rawLength;
            memcpy(item.bytes, rawBytes, item.length);
            if (xQueueSend(navStreamRxQueue, &item, 0) != pdPASS) {
                Serial.println("[NAV_STREAM] RX queue full; packet dropped (sender must retry/resync)");
                return;
            }
            notifyUiTask();
            return;
        }
        
        
        if (rawLength > 1024) {
            Serial.println("[BLE] Payload rejected: maximum JSON write is 1024 bytes");
            return;
        }
        String received;
        received.reserve(rawLength);
        for (size_t i = 0; i < rawLength; ++i) received += (char)rawBytes[i];

        
        
        if (findTopLevelJsonKey(received, "type") >= 0) {
            char messageType[16] = {0};
            if (jsonGetString(received, "type", messageType, sizeof(messageType)) &&
                (strcmp(messageType, "NAV_STATE") == 0 || strcmp(messageType, "NAV_MAP") == 0 || strcmp(messageType, "NAV_POS") == 0)) {
                if (received.length() > NAV_MAX_PAYLOAD_BYTES) {
                    Serial.printf("[NAV] Rejected: payload %u exceeds %u-byte navigation limit\n",
                                  (unsigned)received.length(), (unsigned)NAV_MAX_PAYLOAD_BYTES);
                    return;
                }
                if (!blePeerMtuNegotiated || !bleNavigationPayloadSupported || blePeerAttMtu < 243) {
                    Serial.printf("[NAV] Rejected: negotiated ATT MTU %u; navigation needs at least 243\n", (unsigned)blePeerAttMtu);
                    return;
                }
                static bool navFirstRxDiagStarted = false;
                static bool navFirstRxDiagCompleted = false;
                if (!navFirstRxDiagStarted) {
                    navFirstRxDiagStarted = true;
                    Serial.printf("[NAV_DIAG] first BLE nav RX len=%u callback_stack_hwm=%u free_heap=%u\n",
                                  (unsigned)received.length(),
                                  (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                                  (unsigned)esp_get_free_heap_size());
                }
                NavigationMessage navMessage;
                if (!parseNavigationMessage(received.c_str(), received.length(), navMessage)) {
                    Serial.println("[NAV] Rejected malformed or out-of-range Navigation Protocol message (supported versions: 1, 2)");
                    return;
                }
                if (!navFirstRxDiagCompleted) {
                    navFirstRxDiagCompleted = true;
                    Serial.printf("[NAV_DIAG] first BLE nav parse OK v=%u kind=%u sid=%lu seq=%lu callback_stack_hwm_after=%u free_heap=%u\n",
                                  (unsigned)navMessage.version, (unsigned)navMessage.kind,
                                  (unsigned long)navMessage.sessionId, (unsigned long)navMessage.sequence,
                                  (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                                  (unsigned)esp_get_free_heap_size());
                }
                portENTER_CRITICAL(&navigationMailboxMux);
                if (navMessage.kind == NAV_MESSAGE_STATE) { navMailboxState = navMessage; navMailboxHasState = true; }
                else if (navMessage.kind == NAV_MESSAGE_MAP) { navMailboxMap = navMessage; navMailboxHasMap = true; }
                else { navMailboxPosition = navMessage; navMailboxHasPosition = true; }
                portEXIT_CRITICAL(&navigationMailboxMux);
                CN_DEBUG_PRINTF("[NAV] Queued v%u type=%s sid=%lu seq=%lu frame=%u len=%u\n",
                                (unsigned)navMessage.version, messageType,
                                (unsigned long)navMessage.sessionId,
                                (unsigned long)navMessage.sequence,
                                (unsigned)navMessage.frame, (unsigned)received.length());
                notifyUiTask();
                return;
            }
        }

        CallBleEvent callEvent;
        const uint8_t callParse = parseCallBlePayload(received, callEvent);
        if (callParse != CALL_PAYLOAD_NOT_CALL) {
            if (callParse == CALL_PAYLOAD_INVALID) {
                Serial.println("[CALL] Invalid call JSON/event; rejected");
                return;
            }
            if (callEventQueue == nullptr || xQueueSend(callEventQueue, &callEvent, 0) != pdPASS) {
                Serial.println("[CALL] Event queue full/unavailable; event dropped");
                return;
            }
            CN_DEBUG_PRINTF("[CALL] Queued event=%u callId=%s state=%s\n",
                            (unsigned)callEvent.kind, callEvent.callId, callEvent.state);
            notifyUiTask();
            return;
        }

        WatchData parsed;
        
        
        
        if (watchDataMutex != nullptr) xSemaphoreTake(watchDataMutex, portMAX_DELAY);
        parsed = bleStagingData;
        if (!parseWatchPayload(received, parsed)) {
            if (watchDataMutex != nullptr) xSemaphoreGive(watchDataMutex);
            Serial.println("[BLE] Invalid JSON or out-of-range field. Check the documented payload keys and value ranges.");
            return;
        }
        bleStagingData = parsed;
        BaseType_t queued = watchDataQueue != nullptr ? xQueueOverwrite(watchDataQueue, &parsed) : 0;
        if (queued == pdPASS && findTopLevelJsonKey(received, "notifications") >= 0) {
            bleNotificationSnapshotPending = true;
        }
        if (watchDataMutex != nullptr) xSemaphoreGive(watchDataMutex);
        if (queued != pdPASS) {
            Serial.println("[BLE] Parsed payload could not be queued");
            return;
        }
        CN_DEBUG_PRINTF("[BLE] Payload parsed/queued: %02d:%02d, %ld steps, %dC, %u%% humidity, AQI %d, %u notifications\n",
                        (parsed.time[0] - '0') * 10 + parsed.time[1] - '0',
                        (parsed.time[3] - '0') * 10 + parsed.time[4] - '0',
                        (long)parsed.steps, parsed.temperature, parsed.humidity,
                        parsed.aqi, parsed.notificationCount);
        notifyUiTask();
    }
};

void initializeBLE() {
    BLEDevice::init(BLE_DEVICE_NAME);
    const esp_err_t mtuConfigResult = BLEDevice::setMTU(BLE_CONFIGURED_LOCAL_MTU);
    Serial.printf("[BLE] Local ATT MTU configured: %u (setMTU result=%d)\n",
                  BLE_CONFIGURED_LOCAL_MTU, (int)mtuConfigResult);
    Serial.println("[BLE] Local MTU is a ceiling; the per-client negotiated MTU is logged on connection/MTU event.");
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new CyberNodeServerCallbacks());

    BLEService* service = bleServer->createService(BLE_SERVICE_UUID);

    BLECharacteristic* rxCharacteristic = service->createCharacteristic(
        BLE_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    rxCharacteristic->setCallbacks(new CyberNodeRxCallbacks());

    bleTxCharacteristic = service->createCharacteristic(
        BLE_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    bleTxCharacteristic->addDescriptor(new BLE2902());
    bleTxCharacteristic->setCallbacks(new CyberNodeTxCallbacks());

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    
    
    advertising->setMinPreferred(0x18);
    advertising->setMaxPreferred(0x30);
    BLEDevice::startAdvertising();

    Serial.println("[BLE] Initialized; advertising as CyberNode S3");
    Serial.println("[BLE] TX uses one notification per JSON message; oversized payloads are rejected, never silently sent as a partial JSON.");
}



void setup() {
    Serial.begin(115200);
    
    
    Serial.printf("[BOOT_DIAG] reset_reason=%d free_heap=%u min_free_heap=%u\n",
                  (int)esp_reset_reason(), (unsigned)esp_get_free_heap_size(),
                  (unsigned)esp_get_minimum_free_heap_size());
    initializeWatchDefaults();
    soundManager.begin(BUZZER_PIN);
    hapticManager.begin(HAPTIC_MOTOR_PIN);
    Serial.printf("[HAPTIC] Motor output ready on GPIO%u (external driver required)\n", (unsigned)HAPTIC_MOTOR_PIN);
    gameManager.setSoundManager(&soundManager);
    restoreWatchSnapshot();
    notificationBaselineReady = snapshotStored;
    restoreSoundSettings();
    restoreHapticSettings();
    restoreLocalAppSettings();
    syncLocalClockFromPhone(watchData.time);
    updateLocalClock();
    bleStagingData = watchData;
    watchDataMutex = xSemaphoreCreateMutex();
    if (watchDataMutex == nullptr) Serial.println("ERROR: watch data mutex creation failed");
    watchDataQueue = xQueueCreate(1, sizeof(WatchData));
    if (watchDataQueue == nullptr) Serial.println("ERROR: watch data queue creation failed");
    callEventQueue = xQueueCreate(8, sizeof(CallBleEvent));
    if (callEventQueue == nullptr) Serial.println("ERROR: call event queue creation failed");
    navStreamRxQueue = xQueueCreate(8, sizeof(NavStreamRxItem));
    if (navStreamRxQueue == nullptr) Serial.println("ERROR: NAV_STREAM RX queue creation failed; V3 stream disabled");
    navStreamSnapshotMutex = xSemaphoreCreateMutex();
    if (navStreamSnapshotMutex == nullptr) Serial.println("ERROR: NAV_STREAM snapshot mutex creation failed; V3 stream disabled");
    initializeBLE();

    displayManager.begin();
    displayManager.setContrast(oledContrast);
    if (navStreamRxQueue != nullptr && navStreamSnapshotMutex != nullptr) {
        
        
        
        BaseType_t streamTaskCreated = xTaskCreate(navStreamTask, "NavStreamTask", 8192, nullptr, 2, &navStreamTaskHandle);
        if (streamTaskCreated != pdPASS) {
            navStreamTaskHandle = nullptr;
            Serial.println("ERROR: NavStreamTask creation failed; V3 stream disabled");
        } else Serial.println("[NAV_STREAM] task started; V1/V2 compatibility retained");
    }

    
    
    buttonEventQueue = xQueueCreate(10, sizeof(ButtonEvent));
    if (buttonEventQueue == nullptr) {
        Serial.println("ERROR: button event queue creation failed; using loop polling fallback");
    }

    Serial.println("CYBERNODE S3");
    Serial.println("System Initialized");

    currentScreen = HOME_SCREEN;
    selectedMenuItem = 0;
    appLauncherContextValid = false;
    menuParentScreen = APP_DIAL_SCREEN;
    menuParentDialCursor = 0;

    
    
    
    
    if (bootSuperiorLogoEnabled) {
        Serial.println("[BOOT] Showing Superior identity");
        displayManager.showSuperiorBootLogo(superiorBootLogoAnimation);
    }

    if (bootLogoEnabled) {
        Serial.println("[BOOT] Showing CyberNode S3 identity");
        CyberNodeBoot::Animation::run(displayManager, soundManager, hapticManager, BUZZER_PIN, cyberNodeBootAnimation);
    }

    currentScreen = HOME_SCREEN;  
    showCurrentScreen();

    
    inputManager.begin();
    attachInterrupt(digitalPinToInterrupt(4), inputButtonEdgeIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(5), inputButtonEdgeIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(6), inputButtonEdgeIsr, CHANGE);

    if (buttonEventQueue != nullptr) {
        BaseType_t inputCreated = xTaskCreate(
            inputTask,
            "InputTask",
            2048,
            nullptr,
            2,
            &inputTaskHandle
        );
        if (inputCreated != pdPASS) {
            Serial.println("ERROR: InputTask creation failed; using loop polling fallback");
            inputTaskHandle = nullptr;
        } else {
            Serial.println("InputTask started; NEXT=GPIO4, SELECT=GPIO5, BACK=GPIO6");
        }
    }

    
    
    BaseType_t uiCreated = xTaskCreate(
        uiTask,
        "UITask",
        8192,
        nullptr,
        1,
        &uiTaskHandle
    );

    if (uiCreated != pdPASS) {
        Serial.println("ERROR: UITask creation failed; using Arduino loop fallback");
        uiTaskHandle = nullptr;
    } else {
        Serial.println("UITask started");
    }
}



static bool callOverlayVisible() {
    return callUiState != CALL_UI_IDLE && !callOverlayDismissed;
}

static bool callBlocksOledSleep() {
    return callUiState == CALL_UI_RINGING ||
           callUiState == CALL_UI_ANSWER_REQUESTED ||
           callUiState == CALL_UI_REJECT_REQUESTED ||
           callUiState == CALL_UI_TERMINAL;
}

static void setCallFeedback(const char* message) {
    strlcpy(callFeedback, message ? message : "", sizeof(callFeedback));
    callFeedbackUntilMs = callFeedback[0] ? millis() + CALL_FEEDBACK_DISPLAY_MS : 0;
}

static void setCallTerminal(const char* state) {
    soundManager.stopCallRingtone();
    hapticManager.stopCallAlert();
    callUiState = CALL_UI_TERMINAL;
    if (strcmp(state, "REJECTED") == 0) strlcpy(callTerminalLabel, "REJECTED", sizeof(callTerminalLabel));
    else if (strcmp(state, "MISSED") == 0) strlcpy(callTerminalLabel, "MISSED", sizeof(callTerminalLabel));
    else strlcpy(callTerminalLabel, "ENDED", sizeof(callTerminalLabel));
    callTerminalUntilMs = millis() + CALL_TERMINAL_DISPLAY_MS;
    callRequestDeadlineMs = 0;
    pendingCallRequestId = 0;
    callOverlayDismissed = false;
    setCallFeedback("");
    Serial.printf("[CALL] Terminal state: %s (%s)\n", callTerminalLabel, currentCallId);
}

static void beginIncomingCall(const CallBleEvent& event) {
    const bool sameCall = currentCallId[0] && strcmp(currentCallId, event.callId) == 0;
    if (!sameCall && callUiState != CALL_UI_IDLE && callUiState != CALL_UI_TERMINAL) {
        Serial.printf("[CALL] Ignored overlapping/stale incoming callId=%s; active=%s\n",
                      event.callId, currentCallId);
        return;
    }
    if (sameCall && callUiState != CALL_UI_RINGING) {
        
        
        Serial.printf("[CALL] Ignored duplicate incoming for non-ringing callId=%s\n", event.callId);
        return;
    }

    if (!sameCall) {
        strlcpy(currentCallId, event.callId, sizeof(currentCallId));
        strlcpy(currentCallerName, "UNKNOWN CALLER", sizeof(currentCallerName));
        currentCallerPhone[0] = '\0';
        callSelection = 1; 
        callRequestDeadlineMs = 0;
        pendingCallRequestId = 0;
        callOverlayDismissed = false;
        setCallFeedback("");
    }
    if (event.name[0]) strlcpy(currentCallerName, event.name, sizeof(currentCallerName));
    if (event.phone[0]) strlcpy(currentCallerPhone, event.phone, sizeof(currentCallerPhone));
    callUiState = CALL_UI_RINGING;
    callLinkLost = false;
    notificationPopupActive = false; 
    transitionActive = false;
    pendingTransitionEvent = BUTTON_NONE;
    displayManager.setPresentationDeferred(false);
    inputManager.markActivity(millis());
    if (oledSleeping) {
        oledSleeping = false;
        displayManager.setDisplaySleep(false);
        Serial.println("[OLED] Woken automatically for incoming call");
    }
    lastCallRenderMs = 0;
    Serial.printf("[CALL] Incoming: %s / %s (%s)\n",
                  currentCallerName, currentCallerPhone, currentCallId);
}

static void applyCallBleEvent(const CallBleEvent& event) {
    const bool matchingCall = currentCallId[0] && strcmp(currentCallId, event.callId) == 0;

    if (event.kind == CALL_BLE_INCOMING) {
        beginIncomingCall(event);
        return;
    }
    if (!matchingCall) {
        Serial.printf("[CALL] Ignored event for stale/unknown callId=%s\n", event.callId);
        return;
    }

    if (event.kind == CALL_BLE_STATE_CHANGED) {
        
        
        if (callUiState == CALL_UI_TERMINAL && !isTerminalCallState(event.state)) {
            Serial.printf("[CALL] Ignored state regression after terminal event for %s\n", event.callId);
            return;
        }
        if (strcmp(event.state, "RINGING") == 0) {
            if (callUiState != CALL_UI_RINGING && callUiState != CALL_UI_ANSWER_REQUESTED &&
                callUiState != CALL_UI_REJECT_REQUESTED) {
                Serial.printf("[CALL] Ignored stale RINGING state for %s\n", event.callId);
                return;
            }
            if (event.name[0]) strlcpy(currentCallerName, event.name, sizeof(currentCallerName));
            if (event.phone[0]) strlcpy(currentCallerPhone, event.phone, sizeof(currentCallerPhone));
            callUiState = CALL_UI_RINGING;
            callLinkLost = false; 
            callRequestDeadlineMs = 0;
            pendingCallRequestId = 0;
            callOverlayDismissed = false;
            setCallFeedback("");
            Serial.printf("[CALL] Confirmed still ringing: %s\n", event.callId);
        } else if (strcmp(event.state, "ACTIVE") == 0) {
            if (callUiState == CALL_UI_IDLE) {
                Serial.printf("[CALL] Ignored ACTIVE without an active call context: %s\n", event.callId);
                return;
            }
            soundManager.stopCallRingtone();
            const bool wasAlreadyActive = callUiState == CALL_UI_ACTIVE;
            callUiState = CALL_UI_ACTIVE;
            if (!wasAlreadyActive) callActiveStartedMs = millis();
            callRequestDeadlineMs = 0;
            pendingCallRequestId = 0;
            callOverlayDismissed = false;
            setCallFeedback("");
            if (event.name[0]) strlcpy(currentCallerName, event.name, sizeof(currentCallerName));
            if (event.phone[0]) strlcpy(currentCallerPhone, event.phone, sizeof(currentCallerPhone));
            Serial.printf("[CALL] Active: %s\n", event.callId);
        } else if (isTerminalCallState(event.state)) {
            setCallTerminal(event.state);
        }
        return;
    }

    if (event.kind == CALL_BLE_ENDED) {
        setCallTerminal(event.state);
        return;
    }

    if (event.kind == CALL_BLE_COMMAND_RESULT) {
        const bool expectedAction =
            (callUiState == CALL_UI_ANSWER_REQUESTED && strcmp(event.action, "ANSWER") == 0) ||
            (callUiState == CALL_UI_REJECT_REQUESTED && strcmp(event.action, "REJECT") == 0);
        if (!expectedAction || event.requestId != pendingCallRequestId) {
            Serial.printf("[CALL] Ignored stale command result requestId=%lu\n", (unsigned long)event.requestId);
            return;
        }
        if (event.success) {
            setCallFeedback("REQUEST SENT");
            
            Serial.printf("[CALL] %s request acknowledged by companion\n", event.action);
        } else {
            callUiState = CALL_UI_RINGING;
            callRequestDeadlineMs = 0;
            pendingCallRequestId = 0;
            setCallFeedback(strcmp(event.action, "ANSWER") == 0 ? "ANSWER FAILED" : "REJECT FAILED");
            Serial.printf("[CALL] %s request failed: %s\n", event.action,
                          event.reason[0] ? event.reason : "no reason provided");
        }
    }
}

static void drainCallEvents() {
    if (callEventQueue == nullptr) return;
    static CallBleEvent event;
    while (xQueueReceive(callEventQueue, &event, 0) == pdPASS) applyCallBleEvent(event);
}

static void sendCallControl(bool answer) {
    if (callUiState != CALL_UI_RINGING) return;
    if (!bleConnected || callLinkLost || bleTxCharacteristic == nullptr || bleServer == nullptr) {
        if (!bleConnected) callLinkLost = true;
        setCallFeedback(bleConnected ? "SYNCING CALL..." : "PHONE LINK LOST");
        soundManager.stopCallRingtone();
        Serial.println("[CALL] Control not sent: BLE is disconnected or TX is unavailable");
        return;
    }

    
    
    uint16_t peerMtu = blePeerAttMtu;
    if (peerMtu < BLE_DEFAULT_ATT_MTU) peerMtu = BLE_DEFAULT_ATT_MTU;
    const size_t maxNotifyPayload = (size_t)peerMtu - 3U;

    uint32_t nextRequestId = callRequestId + 1U;
    if (nextRequestId == 0U) nextRequestId = 1U;
    const char* action = answer ? "ANSWER" : "REJECT";
    char payload[160];
    const int payloadLength = snprintf(payload, sizeof(payload),
        "{\"type\":\"call_control\",\"action\":\"%s\",\"callId\":\"%s\",\"requestId\":%lu}",
        action, currentCallId, (unsigned long)nextRequestId);
    if (payloadLength <= 0 || payloadLength >= (int)sizeof(payload)) {
        setCallFeedback("CONTROL ERROR");
        Serial.println("[CALL] TX aborted: call-control JSON formatting failed");
        return;
    }

    Serial.printf("[BLE TX] action=%s jsonLength=%u peerMTU=%u maxNotifyPayload=%u mtuEventSeen=%s\n",
                  action, (unsigned)payloadLength, peerMtu, (unsigned)maxNotifyPayload,
                  blePeerMtuNegotiated ? "yes" : "no");

    
    
    
    if ((size_t)payloadLength > maxNotifyPayload) {
        setCallFeedback("LOW BLE MTU");
        Serial.printf("[BLE TX] REFUSED: %u-byte JSON exceeds %u-byte single-notify limit. Request a larger MTU from the central, reconnect, and check the MTU event above.\n",
                      (unsigned)payloadLength, (unsigned)maxNotifyPayload);
        return; 
    }

    bleTxCharacteristic->setValue((uint8_t*)payload, (size_t)payloadLength);
    const size_t storedLength = bleTxCharacteristic->getLength();
    if (storedLength != (size_t)payloadLength) {
        setCallFeedback("TX BUFFER ERROR");
        Serial.printf("[BLE TX] REFUSED: setValue stored %u of %u bytes.\n",
                      (unsigned)storedLength, (unsigned)payloadLength);
        return;
    }

    
    
    
    bleLastTxNotifyResult = 0;
    bleTxCharacteristic->notify();
#if defined(CONFIG_BLUEDROID_ENABLED)
    
    
    
    if (bleLastTxNotifyResult != 1) {
        setCallFeedback("TX NOT SENT");
        Serial.println("[CALL] TX was not accepted by the GATT stack; remaining in RINGING");
        return;
    }
#endif
    callRequestId = nextRequestId;
    pendingCallRequestId = nextRequestId;
    callUiState = answer ? CALL_UI_ANSWER_REQUESTED : CALL_UI_REJECT_REQUESTED;
    callRequestDeadlineMs = millis() + CALL_CONTROL_TIMEOUT_MS;
    soundManager.stopCallRingtone();
    setCallFeedback("SENDING TO PHONE");
    Serial.printf("[CALL] TX submitted: %s requestId=%lu callId=%s bytes=%u\n",
                  action, (unsigned long)pendingCallRequestId, currentCallId,
                  (unsigned)payloadLength);
}

static void updateCallSubsystem(bool alertIsActive) {
    drainCallEvents();
    const uint32_t now = millis();

    
    
    if (callUiState == CALL_UI_RINGING && !bleConnected) {
        if (!callLinkLost) Serial.println("[CALL] BLE link lost while ringing; call remains untouched");
        callLinkLost = true;
        soundManager.stopCallRingtone();
    }

    if ((callUiState == CALL_UI_ANSWER_REQUESTED || callUiState == CALL_UI_REJECT_REQUESTED) &&
        callRequestDeadlineMs && (int32_t)(now - callRequestDeadlineMs) >= 0) {
        callUiState = CALL_UI_RINGING;
        callRequestDeadlineMs = 0;
        pendingCallRequestId = 0;
        setCallFeedback("NO PHONE RESPONSE");
        Serial.println("[CALL] Control request timed out; returning to RINGING");
    }

    if (callFeedbackUntilMs && (int32_t)(now - callFeedbackUntilMs) >= 0) {
        callFeedback[0] = '\0';
        callFeedbackUntilMs = 0;
    }
    if (callUiState == CALL_UI_TERMINAL && callTerminalUntilMs &&
        (int32_t)(now - callTerminalUntilMs) >= 0) {
        NodeBot::controller().observeCallFinished(callTerminalLabel, millis());
        callUiState = CALL_UI_IDLE;
        callTerminalUntilMs = 0;
        currentCallId[0] = currentCallerName[0] = currentCallerPhone[0] = '\0';
        callTerminalLabel[0] = callFeedback[0] = '\0';
        callLinkLost = false;
        callOverlayDismissed = false;
        if (!alertIsActive && !oledSleeping) showCurrentScreen();
    }

    
    
    if (alertIsActive || callUiState != CALL_UI_RINGING || callLinkLost || !bleConnected) {
        soundManager.stopCallRingtone();
        hapticManager.stopCallAlert();
    } else {
        hapticManager.startCallAlert();
        if (soundManager.isEnabled() && !soundManager.isCallRingtoneRequested()) {
            soundManager.startCallRingtone();
        }
    }
}

static void renderCallOverlay(bool alertIsActive) {
    if (alertIsActive || oledSleeping || !callOverlayVisible()) return;
    const uint32_t now = millis();
    const uint32_t refresh = (callUiState == CALL_UI_ACTIVE) ? 1000UL :
        ((callUiState == CALL_UI_RINGING || callUiState == CALL_UI_ANSWER_REQUESTED ||
          callUiState == CALL_UI_REJECT_REQUESTED) ? 110UL : 250UL);
    if (lastCallRenderMs && (uint32_t)(now - lastCallRenderMs) < refresh) return;
    lastCallRenderMs = now;

    const char* status = "RINGING";
    bool isRinging = true;
    if (callUiState == CALL_UI_ANSWER_REQUESTED) { status = "ANSWER_REQUESTED"; isRinging = false; }
    else if (callUiState == CALL_UI_REJECT_REQUESTED) { status = "REJECT_REQUESTED"; isRinging = false; }
    else if (callUiState == CALL_UI_ACTIVE) { status = "ACTIVE"; isRinging = false; }
    else if (callUiState == CALL_UI_TERMINAL) {
        status = callTerminalLabel[0] ? callTerminalLabel : "ENDED";
        isRinging = false;
    }
    const uint32_t duration = callUiState == CALL_UI_ACTIVE
        ? (uint32_t)(now - callActiveStartedMs) / 1000UL : 0;
    const char* visibleFeedback = callFeedback;
    if (callUiState == CALL_UI_RINGING && callLinkLost)
        visibleFeedback = bleConnected ? "SYNCING CALL..." : "PHONE LINK LOST";
    displayManager.showCallScreen(currentCallerName, currentCallerPhone, status, visibleFeedback,
                                  isRinging, callSelection == 0, bleConnected, duration, now);
}



void processUiIteration() {
    const uint8_t connectionSound = pendingBleSoundEvent;
    pendingBleSoundEvent = 0;
    if (connectionSound == 1) soundManager.play(SoundEvent::BLE_CONNECTED);
    else if (connectionSound == 2) soundManager.play(SoundEvent::BLE_DISCONNECTED);

    
    
    NodeBot::controller().beginUiIteration();
    NodeBot::controller().observeBle(bleConnected, millis());

    soundManager.update(millis());
    hapticManager.update(millis());
    navigationManager.setBleConnected(bleConnected);
    consumeNavigationMailbox();
    
    

    const bool alertWasActive = alarmAlertActive || timerAlertActive;
    const bool alertWasAlarm = alarmAlertActive;
    const bool alertWasTimer = timerAlertActive;
    const bool alertTriggered = updateLocalAlarmTimer();
    const bool alertIsActive = alarmAlertActive || timerAlertActive;
    const bool alertDismissed = alertWasActive && !alertIsActive;
    if (alertDismissed) {
        NodeBot::controller().observeAlertDismissed(alertWasTimer && !alertWasAlarm, millis());
    }
    static uint32_t lastAlertRenderMs = 0;

    
    
    
    if (alertTriggered) {
        const uint32_t alertNow = millis();
        hapticManager.trigger(HapticEvent::ALERT);
        soundManager.stopCallRingtone();
        notificationPopupActive = false;
        inputManager.markActivity(alertNow);
        transitionActive = false;
        pendingTransitionEvent = BUTTON_NONE;
        displayManager.setPresentationDeferred(false);
        if (oledSleeping) {
            oledSleeping = false;
            displayManager.setDisplaySleep(false);
            Serial.println("[OLED] Woken automatically by alarm/timer");
        }
        displayManager.showLocalAlert(alarmAlertActive, timerAlertActive);
        lastAlertRenderMs = alertNow;
    }

    
    if (alertDismissed && !oledSleeping && !transitionActive) {
        showCurrentScreen();
    }

    updateCallSubsystem(alertIsActive);

    
    
    
    const uint32_t idleCheckMs = millis();
    const uint32_t sleepTimeoutMs = configuredOledSleepTimeoutMs();
    if (!alertIsActive && !notificationPopupActive && !pendingNotificationPopup && !callBlocksOledSleep() &&
        !navigationStayAwakeOverride && sleepTimeoutMs > 0 && !oledSleeping &&
        (uint32_t)(idleCheckMs - inputManager.lastActivityMs()) >= sleepTimeoutMs) {
        
        
        if (buttonEventQueue != nullptr) {
            ButtonEvent staleEvent;
            while (xQueueReceive(buttonEventQueue, &staleEvent, 0) == pdPASS) {}
        }
        oledSleeping = true;
        displayManager.setDisplaySleep(true);
        Serial.printf("[OLED] Sleeping after %u minute(s) of button inactivity\n",
                      (unsigned)oledSleepTimeoutMinutes);
    }

    
    
    const uint32_t nowForClock = millis();
    if (localClockInitialized && nowForClock - lastClockMaintenanceMs >= 60000UL) {
        lastClockMaintenanceMs = nowForClock;
        const bool clockChanged = updateLocalClock();
        
        
        if (watchDataMutex != nullptr) xSemaphoreTake(watchDataMutex, portMAX_DELAY);
        bleStagingData = watchData;
        if (watchDataMutex != nullptr) xSemaphoreGive(watchDataMutex);
        if (!oledSleeping && !notificationPopupActive && !callOverlayVisible() && clockChanged && !transitionActive && currentScreen == HOME_SCREEN) showHome();
    }

    
    
    
    
    static WatchData incomingData;
    static WatchData previousData;
    bool dataUpdated = false;
    const bool phoneFreshBeforeUpdates = phoneDataFresh;
    if (watchDataQueue != nullptr) {
        while (xQueueReceive(watchDataQueue, &incomingData, 0) == pdPASS) {
            previousData = watchData;
            bool notificationSnapshotWasProvided = false;
            if (watchDataMutex != nullptr) xSemaphoreTake(watchDataMutex, portMAX_DELAY);
            notificationSnapshotWasProvided = bleNotificationSnapshotPending;
            bleNotificationSnapshotPending = false;
            
            
            
            incomingData = bleStagingData;
            watchData = incomingData;
            syncLocalClockFromPhone(watchData.time);
            updateLocalClock();
            bleStagingData = watchData; 
            if (watchDataMutex != nullptr) xSemaphoreGive(watchDataMutex);
            if (watchData.notificationCount == 0) notificationSelected = 0;
            else if (notificationSelected >= watchData.notificationCount) notificationSelected = watchData.notificationCount - 1;

            
            
            NodeBot::controller().observeWatchData(watchData, phoneFreshBeforeUpdates, millis());

            if (notificationSnapshotWasProvided) {
                if (!notificationBaselineReady) {
                    
                    
                    notificationBaselineReady = true;
                    Serial.println("[NOTIFY] Initial notification snapshot established");
                } else {
                    int newestNewNotification = -1;
                    const uint8_t newNotificationCount = countNewNotifications(
                        previousData, watchData, newestNewNotification);
                    
                    NodeBot::controller().observeNotifications(newNotificationCount, notificationAlertsEnabled, millis());
                    if (newNotificationCount > 0 && notificationAlertsEnabled) {
                        
                        hapticManager.trigger(HapticEvent::NOTIFICATION);
                        const bool alreadyInNotificationBrowser =
                            currentScreen == NOTIFICATIONS_SCREEN || currentScreen == NOTIFICATION_DETAIL_SCREEN;
                        const bool shouldPopup = currentScreen == HOME_SCREEN ||
                            (notificationPopupInApps && isNotificationAppScreen(currentScreen) &&
                             !alreadyInNotificationBrowser);

                        if (notificationBeepEnabled) {
                            const uint16_t queued = (uint16_t)notificationBeepQueueCount + newNotificationCount;
                            notificationBeepQueueCount = (uint8_t)(queued > NOTIFICATION_BEEP_QUEUE_MAX
                                ? NOTIFICATION_BEEP_QUEUE_MAX : queued);
                        }

                        if (shouldPopup) {
                            
                            
                            pendingNotificationIndex = (uint8_t)newestNewNotification;
                            if (notificationPopupActive) {
                                notificationPopupSelected = pendingNotificationIndex;
                                notificationPopupUntilMs = millis() + NOTIFICATION_POPUP_DURATION_MS;
                                displayManager.showNotificationPopup(notificationPopupSelected, watchData);
                            } else {
                                pendingNotificationPopup = true;
                            }
                        }
                        Serial.printf("[NOTIFY] New notifications=%u, popup=%s, beep queue=%u\n",
                                      (unsigned)newNotificationCount, shouldPopup ? "YES" : "NO",
                                      (unsigned)notificationBeepQueueCount);
                    }
                }
            }

            
            
            if (!firstPhoneSyncPersistedThisBoot || snapshotWritePending ||
                shouldPersistWatchSnapshot(previousData, watchData)) {
                snapshotWritePending = true;
                if (persistWatchSnapshot()) {
                    snapshotWritePending = false;
                    firstPhoneSyncPersistedThisBoot = true;
                }
            }
            lastPhoneDataUpdateMs = millis();
            phoneDataFresh = true;
            staleStateReported = false;
            dataUpdated = true;
        }
    }

    
    
    
    if (!alertIsActive && !callOverlayVisible() && pendingNotificationPopup && !transitionActive && notificationAlertsEnabled) {
        if (currentScreen == HOME_SCREEN ||
            (notificationPopupInApps && isNotificationAppScreen(currentScreen))) {
            if (oledSleeping) {
                oledSleeping = false;
                inputManager.markActivity(millis());
                displayManager.setDisplaySleep(false);
                Serial.println("[OLED] Woken automatically for notification popup");
            }
            notificationPopupSelected = pendingNotificationIndex < watchData.notificationCount ? pendingNotificationIndex : 0;
            notificationPopupReturnScreen = currentScreen;
            notificationDetailOpenedFromPopup = false;
            notificationPopupActive = watchData.notificationCount > 0;
            inputManager.markActivity(millis());
            notificationPopupUntilMs = millis() + NOTIFICATION_POPUP_DURATION_MS;
            pendingNotificationPopup = false;
            if (notificationPopupActive) displayManager.showNotificationPopup(notificationPopupSelected, watchData);
        } else {
            
            
            pendingNotificationPopup = false;
        }
    }

    
    
    if (notificationPopupActive && !alertIsActive && !callOverlayVisible() &&
        (int32_t)(millis() - notificationPopupUntilMs) >= 0) {
        notificationPopupActive = false;
        showCurrentScreen();
    }

    
    
    
    if (localClockInitialized && (snapshotStored || snapshotWritePending) &&
        nowForClock - lastClockPersistMs >= CLOCK_FLASH_CHECKPOINT_MS &&
        nowForClock - lastClockPersistAttemptMs >= CLOCK_FLASH_RETRY_MS) {
        lastClockPersistAttemptMs = nowForClock;
        updateLocalClock();
        if (watchDataMutex != nullptr) xSemaphoreTake(watchDataMutex, portMAX_DELAY);
        bleStagingData = watchData;
        if (watchDataMutex != nullptr) xSemaphoreGive(watchDataMutex);
        snapshotWritePending = true;
        if (persistWatchSnapshot()) {
            snapshotWritePending = false;
            firstPhoneSyncPersistedThisBoot = true;
        } else {
            Serial.println("[NVS] Clock checkpoint pending; will retry");
        }
    }

    
    
    if (lastPhoneDataUpdateMs == 0 ||
        (millis() - lastPhoneDataUpdateMs) > PHONE_DATA_STALE_TIMEOUT_MS) {
        phoneDataFresh = false;
    }
    if (!phoneDataFresh && !staleStateReported) {
        Serial.println("[BLE] Phone data is stale/unavailable");
        staleStateReported = true;
    }

    
    
    NodeBot::controller().observePhoneFreshness(phoneDataFresh, millis());

    if (dataUpdated && !oledSleeping && !transitionActive && !alertIsActive && !notificationPopupActive && !callOverlayVisible()) {
        if (currentScreen == HOME_SCREEN) showHome();
        else if (currentScreen == APP_DIAL_SCREEN) {
            const int previewId = dialAppAtEntry(dialCursor);
            if (phoneFreshBeforeUpdates != phoneDataFresh ||
                dialPreviewDataChanged(previousData, watchData, previewId)) {
                showCurrentScreen();
            }
        }
        else if (currentScreen == WEATHER_SCREEN) {
            displayManager.showWeatherWindow(weatherWindowIndex, watchData);
        } else if (currentScreen == STEPS_SCREEN) {
            displayManager.showStepsWindow(stepsWindowIndex, watchData, snapshotStored || phoneDataFresh);
        } else if (currentScreen == NOTIFICATIONS_SCREEN) { displayManager.showNotifications(notificationSelected, watchData); }
        else if (currentScreen == NOTIFICATION_DETAIL_SCREEN) { displayManager.showNotificationDetail(notificationSelected, watchData, notificationDetailScrollLine); }
        else if (currentScreen == STOPWATCH_SCREEN) { displayManager.showStopwatch(stopwatchAccumulatedMs + (stopwatchRunning ? millis()-stopwatchStartMs : 0), stopwatchRunning, stopwatchLapCount, stopwatchLaps); }
        else if (currentScreen == SYSTEM_SCREEN && systemWindowIndex == 2) {
            displayManager.showSystemWindow(systemWindowIndex, bleConnected, phoneDataFresh);
        }
    }

    
    
    static uint32_t lastStopwatchRefreshMs = 0;
    if (!oledSleeping && !transitionActive && !alertIsActive && !notificationPopupActive && !callOverlayVisible() && currentScreen == STOPWATCH_SCREEN && stopwatchRunning &&
        millis() - lastStopwatchRefreshMs >= 50) {
        lastStopwatchRefreshMs = millis();
        displayManager.showStopwatch(stopwatchAccumulatedMs + (millis()-stopwatchStartMs), true, stopwatchLapCount, stopwatchLaps);
    }

    static uint32_t lastTimerScreenRefreshMs = 0;
    if (!oledSleeping && !transitionActive && !alertIsActive && !notificationPopupActive &&
        !callOverlayVisible() && currentScreen == TIMER_SCREEN && localTimer.running &&
        (uint32_t)(millis() - lastTimerScreenRefreshMs) >= 1000UL) {
        lastTimerScreenRefreshMs = millis();
        showCurrentScreen(); 
    }

    
    static uint32_t lastDialPreviewRefreshMs = 0;
    if (!oledSleeping && !transitionActive && !alertIsActive && !notificationPopupActive && !callOverlayVisible() && currentScreen == APP_DIAL_SCREEN &&
        millis() - lastDialPreviewRefreshMs >= 250) {
        const int previewId = dialAppAtEntry(dialCursor);
        uint32_t refreshInterval = 0;
        if (previewId == DIAL_PREVIEW_STOPWATCH) refreshInterval = 250;
        else if (previewId == DIAL_PREVIEW_TIMER) refreshInterval = 1000;
        else if (previewId == DIAL_PREVIEW_SYSTEM) refreshInterval = 500;
        if (refreshInterval && millis() - lastDialPreviewRefreshMs >= refreshInterval) {
            lastDialPreviewRefreshMs = millis();
            showCurrentScreen();
        }
    }
    if (!oledSleeping && alertIsActive && !alertTriggered &&
        (uint32_t)(millis() - lastAlertRenderMs) >= 120UL) {
        
        
        lastAlertRenderMs = millis();
        displayManager.showLocalAlert(alarmAlertActive, timerAlertActive);
    }

    
    static uint32_t lastStatusRefreshMs = 0;
    if (!oledSleeping && !transitionActive && !alertIsActive && !notificationPopupActive && !callOverlayVisible() && currentScreen == SYSTEM_SCREEN && systemWindowIndex == 2 &&
        millis() - lastStatusRefreshMs >= 500) {
        lastStatusRefreshMs = millis();
        displayManager.showSystemWindow(systemWindowIndex, bleConnected, phoneDataFresh);
    }

    if (!alertIsActive && !notificationPopupActive && !callOverlayVisible() && !transitionActive && currentScreen == FACE_PICKER_SCREEN && facePickerNameUntilMs != 0 &&
        (int32_t)(millis() - facePickerNameUntilMs) >= 0) {
        facePickerNameUntilMs = 0;
        displayManager.setFacePickerOverlay(true, false);
        if (!oledSleeping) showCurrentScreen();
    }

    
    
    ButtonEvent event = BUTTON_NONE;
    if (buttonEventQueue != nullptr && inputTaskHandle != nullptr) {
        xQueueReceive(buttonEventQueue, &event, 0);
        if (uxQueueMessagesWaiting(buttonEventQueue) > 0) notifyUiTask();
    } else {
        event = inputManager.update(oledSleeping);
    }

    
    
    
    
    if (oledSleeping) {
        if (event == BUTTON_WAKE) {
            oledSleeping = false;
            transitionActive = false;
            pendingTransitionEvent = BUTTON_NONE;
            displayManager.setDisplaySleep(false);
            displayManager.setPresentationDeferred(false);
            lastHomeRefresh = millis();
            showCurrentScreen(); 
            if (pendingNavigationPopup && navigationAutoPopup && currentScreen == HOME_SCREEN && navigationManager.hasActiveSession() &&
                navigationManager.isGuidanceFresh(millis())) {
                pendingNavigationPopup = false;
                openNavigationScreen(true, false);
            }
            Serial.println("[OLED] Woken by button; first press consumed");
        }
        event = BUTTON_NONE;
    } else if (event == BUTTON_WAKE) {
        
        event = BUTTON_NONE;
    }

    
    
    if (!alertIsActive && callOverlayVisible() && event != BUTTON_NONE && event != BUTTON_WAKE) {
        inputManager.markActivity(millis());
        if (callUiState == CALL_UI_RINGING) {
            if (event == BUTTON_NEXT) {
                callSelection = callSelection == 0 ? 1 : 0;
                setCallFeedback("");
            } else if (event == BUTTON_SELECT) {
                sendCallControl(callSelection == 0);
            } else if (event == BUTTON_BACK || event == BUTTON_LONG_SELECT) {
                sendCallControl(false);
            }
        } else if (callUiState == CALL_UI_ACTIVE && event == BUTTON_BACK) {
            
            callOverlayDismissed = true;
            showCurrentScreen();
        } else if (callUiState == CALL_UI_TERMINAL) {
            NodeBot::controller().observeCallFinished(callTerminalLabel, millis());
            callUiState = CALL_UI_IDLE;
            callTerminalUntilMs = 0;
            currentCallId[0] = currentCallerName[0] = currentCallerPhone[0] = '\0';
            callTerminalLabel[0] = callFeedback[0] = '\0';
            callOverlayDismissed = false;
            showCurrentScreen();
        }
        event = BUTTON_NONE;
    }

    
    
    if (notificationPopupActive && event != BUTTON_NONE && event != BUTTON_WAKE) {
        const uint8_t selectedNotification = notificationPopupSelected;
        notificationPopupActive = false;
        if (event == BUTTON_SELECT && watchData.notificationCount > 0) {
            notificationSelected = selectedNotification < watchData.notificationCount ? selectedNotification : 0;
            notificationDetailScrollLine = 0;
            notificationDetailOpenedFromPopup = true;
            currentScreen = NOTIFICATION_DETAIL_SCREEN;
            soundManager.play(SoundEvent::SELECT);
        }
        showCurrentScreen();
        event = BUTTON_NONE;
    }

    
    
    
    if (alertIsActive && event != BUTTON_SELECT) event = BUTTON_NONE;

    if (transitionActive && event != BUTTON_NONE && event != BUTTON_LONG_SELECT) {
        
        
        if (event == BUTTON_SELECT || pendingTransitionEvent != BUTTON_SELECT) {
            pendingTransitionEvent = event;
        }
        event = BUTTON_NONE;
    }

    switch (event) {

        case BUTTON_NEXT:
            Serial.println("[UI] NEXT event received");
            goNextScreen();
            break;

        case BUTTON_BACK:
            Serial.println("[UI] BACK event received");
            goPreviousScreen();
            break;

        case BUTTON_SELECT:
            Serial.println("[UI] SELECT event received");
            handleSelect();
            break;

        case BUTTON_LONG_SELECT:
            soundManager.play(SoundEvent::LONG_PRESS);
            pendingTransitionEvent = BUTTON_NONE;
            if (transitionActive) {
                
                
                displayManager.drawTransitionMask(128, activeTransitionDirection);
                transitionActive = false;
            }
            if (currentScreen == HOME_SCREEN) { openFacePicker(); break; }
            if (currentScreen == FACE_PICKER_SCREEN) {
                closeFacePicker(false);             
                currentScreen = HOME_SCREEN;
                renderWithTransition(TRANSITION_RADIAL_REVEAL);
                break;
            }
            if (currentScreen == SUPERIOR_SCREEN && !superiorCeoMode && superiorStage == 0) {
                superiorCeoMode=true; superiorStage=0; superiorSwapActive=false; superiorSwapDeadline=0;
                superiorStageDeadline=millis()+2800;
                soundManager.play(SoundEvent::CEO_ACTIVATE);
                displayManager.showSuperiorCEO(superiorStage); Serial.println("SUPERIOR CEO MODE"); break;
            }
            if (currentScreen == TIMER_SCREEN) {
                if (localTimer.running) { localTimer.durationMs=localTimer.deadlineMs-millis(); localTimer.running=false; }
                else if (localTimer.completed) { localTimer.durationMs=300000UL; localTimer.completed=false; }
                else if (localTimer.durationMs < 1000UL) soundManager.play(SoundEvent::INVALID); 
                else { localTimer.deadlineMs=millis()+localTimer.durationMs; localTimer.running=true; }
                showCurrentScreen(); break;
            }
            if (currentScreen == GAME_PLAY_SCREEN) {
                gameManager.exitCurrentGame();
                currentScreen = GAME_LIST_SCREEN;
                            renderWithTransition(TRANSITION_FROM_CENTER);
                Serial.println("LONG SELECT: GAME CENTER");
                break;
            }
            if (currentScreen == SUPERIOR_SCREEN) { superiorStageDeadline=0; superiorSwapActive=false; superiorSwapDeadline=0; superiorCeoMode=false; }
            currentScreen = HOME_SCREEN;
            appLauncherContextValid = false;
            renderWithTransition(TRANSITION_RADIAL_REVEAL);
            Serial.println("LONG SELECT: HOME");
            break;

        case BUTTON_NONE:
        case BUTTON_WAKE:
            break;
    }

    
    if (!transitionActive && !callOverlayVisible() && currentScreen == SUPERIOR_SCREEN && superiorSwapActive) {
        const uint32_t now = millis();
        if ((uint32_t)(now - superiorLastGlitchFrame) >= 45) {
            superiorLastGlitchFrame = now;
            superiorGlitchFrame++;
            if (!oledSleeping && !notificationPopupActive && !callOverlayVisible()) displayManager.showSuperiorFortune(superiorStage, superiorPointer, superiorRatings, true, superiorGlitchFrame);
        }
        if ((int32_t)(now - superiorSwapDeadline) >= 0) {
            const int tmp = superiorRatings[superiorSwapFrom];
            superiorRatings[superiorSwapFrom] = superiorRatings[superiorSwapTo];
            superiorRatings[superiorSwapTo] = tmp;
            superiorSwapActive = false;
            superiorSwapDeadline = 0;
            soundManager.play(SoundEvent::FORTUNE_SWAP);
            if (!oledSleeping && !notificationPopupActive && !callOverlayVisible()) displayManager.showSuperiorFortune(superiorStage, superiorPointer, superiorRatings);
        }
    }

    
    if (!transitionActive && !callOverlayVisible() && currentScreen == SUPERIOR_SCREEN && !superiorSwapActive &&
        superiorStageDeadline != 0 && (int32_t)(millis() - superiorStageDeadline) >= 0) {
        superiorStageDeadline = 0;
        if (superiorCeoMode) {
            if (superiorStage < 6) {
                superiorStage++;
                if (superiorStage == 4) soundManager.play(SoundEvent::CEO_STATUS);
                else if (superiorStage == 5) soundManager.play(SoundEvent::SUPERIOR_LOGO);
                else if (superiorStage == 6) soundManager.play(SoundEvent::HYPERWEAR_REVEAL);
                else soundManager.play(SoundEvent::CEO_STEP);
                superiorStageDeadline = millis() + (superiorStage == 5 ? 3000 : (superiorStage == 6 ? 3200 : 2800));
                if (!oledSleeping && !notificationPopupActive && !callOverlayVisible()) displayManager.showSuperiorCEO(superiorStage);
            } else {
                superiorCeoMode=false; superiorStageDeadline=0;
                returnToAppLauncher();
            }
        } else {
            if (superiorStage < 5) {
                superiorStage++;
                if (superiorStage == 3) soundManager.play(SoundEvent::FORTUNE_REVEAL);
                else if (superiorStage == 4) soundManager.play(SoundEvent::SUPERIOR_LOGO);
                else if (superiorStage == 5) soundManager.play(SoundEvent::FORTUNE_PUNCHLINE);
                superiorStageDeadline = millis() + (superiorStage == 3 ? 3000 : (superiorStage == 4 ? 3000 : 5200));
                if (!oledSleeping && !notificationPopupActive && !callOverlayVisible()) displayManager.showSuperiorFortune(superiorStage, superiorPointer, superiorRatings);
            } else {
                superiorStageDeadline=0;
                returnToAppLauncher();
            }
        }
    }

    
    static uint32_t lastGameTickMs = 0;
    if (!transitionActive && currentScreen == GAME_PLAY_SCREEN &&
        millis() - lastGameTickMs >= 30) {
        uint32_t now = millis();
        lastGameTickMs = now;
        gameManager.update(now);
        if (!oledSleeping && !notificationPopupActive && !callOverlayVisible()) gameManager.draw(displayManager);
    }

    
    if (!oledSleeping && !callOverlayVisible() && transitionActive && millis() - lastTransitionFrame >= TRANSITION_FRAME_INTERVAL) {
        lastTransitionFrame = millis();
        transitionStep++;
        const float progress = (float)transitionStep / (float)TRANSITION_TOTAL_STEPS;
        const float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
        int revealWidth = (int)(128.0f * eased + 0.5f);
        if (transitionStep >= TRANSITION_TOTAL_STEPS) revealWidth = 128;
        displayManager.drawTransitionMask(revealWidth, activeTransitionDirection);
        if (transitionStep >= TRANSITION_TOTAL_STEPS) {
            transitionActive = false;
            const ButtonEvent queuedEvent = pendingTransitionEvent;
            pendingTransitionEvent = BUTTON_NONE;
            if (queuedEvent == BUTTON_NEXT) goNextScreen();
            else if (queuedEvent == BUTTON_BACK) goPreviousScreen();
            else if (queuedEvent == BUTTON_SELECT) handleSelect();
        }
    }

    
    
    
    
    if (!oledSleeping && !transitionActive && !alertIsActive && !notificationPopupActive && !callOverlayVisible() && (currentScreen == HOME_SCREEN || currentScreen == FACE_PICKER_SCREEN)) {
        const uint32_t faceInterval = displayManager.homeFaceRefreshIntervalMs();
        if ((uint32_t)(millis() - lastHomeRefresh) >= faceInterval) {
            lastHomeRefresh = millis();
            updateLocalClock();
            showHome();
        }
    }

    
    

    
    
    renderCallOverlay(alertIsActive);

    
    
    
    
    if (notificationBeepQueueCount > 0 &&
        (!notificationAlertsEnabled || !notificationBeepEnabled || !soundManager.isEnabled())) {
        notificationBeepQueueCount = 0;
    }
    if (!alertIsActive && notificationBeepQueueCount > 0 && !soundManager.isNotificationActive() &&
        !soundManager.isCallRingtoneRequested() &&
        (int32_t)(millis() - nextNotificationBeepAllowedMs) >= 0) {
        soundManager.play(SoundEvent::NOTIFICATION);
        --notificationBeepQueueCount;
        nextNotificationBeepAllowedMs = millis() + 180UL;
    }
}

static void considerUiWait(uint32_t candidateMs, uint32_t& waitMs) {
    if (candidateMs == 0) candidateMs = 1;
    if (candidateMs < waitMs) waitMs = candidateMs;
}

static uint32_t untilDeadlineMs(uint32_t now, uint32_t deadline) {
    const int32_t remaining = (int32_t)(deadline - now);
    return remaining <= 0 ? 1UL : (uint32_t)remaining;
}



static uint32_t computeUiWaitMs() {
    const uint32_t now = millis();
    uint32_t waitMs = 1000UL; 

    
    
    const NavigationSnapshot& navigationState = navigationManager.snapshot();
    if (navigationState.active && !navigationState.stale) {
        const uint32_t guidanceAge = (uint32_t)(now - navigationState.lastStateUpdateMs);
        considerUiWait(guidanceAge >= NAV_GUIDANCE_STALE_MS ? 1UL : NAV_GUIDANCE_STALE_MS - guidanceAge, waitMs);
    }

    if (inputTaskHandle == nullptr) considerUiWait(10UL, waitMs); 

    if (!oledSleeping) {
        const bool alertActiveNow = alarmAlertActive || timerAlertActive;
        if (transitionActive) considerUiWait(TRANSITION_FRAME_INTERVAL, waitMs);
        if (alertActiveNow) considerUiWait(120UL, waitMs);
        if (callOverlayVisible()) {
            considerUiWait(callUiState == CALL_UI_ACTIVE ? 1000UL :
                           ((callUiState == CALL_UI_RINGING || callUiState == CALL_UI_ANSWER_REQUESTED ||
                             callUiState == CALL_UI_REJECT_REQUESTED) ? 110UL : 250UL), waitMs);
        } else if (!alertActiveNow && !notificationPopupActive && !transitionActive) {
            if (currentScreen == HOME_SCREEN || currentScreen == FACE_PICKER_SCREEN)
                considerUiWait(displayManager.homeFaceRefreshIntervalMs(), waitMs);
            if (currentScreen == NAVIGATION_SCREEN && navigationPage == 0) {
                NavStreamSnapshot streamSnapshot;
                if (copyNavStreamSnapshot(streamSnapshot) && streamSnapshot.initialized)
                    considerUiWait(50UL, waitMs); 
            }
            if (currentScreen == GAME_PLAY_SCREEN) considerUiWait(30UL, waitMs);
            if (currentScreen == STOPWATCH_SCREEN && stopwatchRunning) considerUiWait(50UL, waitMs);
            if (currentScreen == TIMER_SCREEN && localTimer.running) considerUiWait(1000UL, waitMs);
            if (currentScreen == SYSTEM_SCREEN && systemWindowIndex == 2) considerUiWait(500UL, waitMs);
            if (currentScreen == APP_DIAL_SCREEN) {
                const int previewId = dialAppAtEntry(dialCursor);
                if (previewId == DIAL_PREVIEW_STOPWATCH) considerUiWait(250UL, waitMs);
                else if (previewId == DIAL_PREVIEW_TIMER) considerUiWait(1000UL, waitMs);
                else if (previewId == DIAL_PREVIEW_SYSTEM) considerUiWait(500UL, waitMs);
            }
            if (currentScreen == SUPERIOR_SCREEN && superiorSwapActive) considerUiWait(45UL, waitMs);
        }
        if (notificationPopupActive) considerUiWait(untilDeadlineMs(now, notificationPopupUntilMs), waitMs);
        if (facePickerNameUntilMs != 0) considerUiWait(untilDeadlineMs(now, facePickerNameUntilMs), waitMs);
        if (superiorStageDeadline != 0) considerUiWait(untilDeadlineMs(now, superiorStageDeadline), waitMs);
        if (superiorSwapActive && superiorSwapDeadline != 0)
            considerUiWait(untilDeadlineMs(now, superiorSwapDeadline), waitMs);
        if (oledSleepEnabled && !alertActiveNow && !notificationPopupActive && !pendingNotificationPopup &&
            !callBlocksOledSleep() && !navigationStayAwakeOverride) {
            const uint32_t timeoutMs = configuredOledSleepTimeoutMs();
            if (timeoutMs > 0) {
                const uint32_t elapsed = (uint32_t)(now - inputManager.lastActivityMs());
                considerUiWait(elapsed >= timeoutMs ? 1UL : timeoutMs - elapsed, waitMs);
            }
        }
    }

    if (callRequestDeadlineMs != 0)
        considerUiWait(untilDeadlineMs(now, callRequestDeadlineMs), waitMs);
    if (callFeedbackUntilMs != 0)
        considerUiWait(untilDeadlineMs(now, callFeedbackUntilMs), waitMs);
    if (callTerminalUntilMs != 0)
        considerUiWait(untilDeadlineMs(now, callTerminalUntilMs), waitMs);
    if (localTimer.running)
        considerUiWait(untilDeadlineMs(now, localTimer.deadlineMs), waitMs);
    if (notificationBeepQueueCount > 0 && !(alarmAlertActive || timerAlertActive) && !soundManager.isCallRingtoneRequested() &&
        !soundManager.isNotificationActive())
        considerUiWait(untilDeadlineMs(now, nextNotificationBeepAllowedMs), waitMs);

    const uint32_t soundWait = soundManager.nextUpdateDelayMs(now);
    if (soundWait != UINT32_MAX) considerUiWait(soundWait, waitMs);
    const uint32_t hapticWait = hapticManager.nextUpdateDelayMs(now);
    if (hapticWait != UINT32_MAX) considerUiWait(hapticWait, waitMs);
    return waitMs < 1UL ? 1UL : waitMs;
}

static void navStreamTask(void* parameter) {
    (void)parameter;
    Serial.println("[NAV_STREAM] protocol task running (single-writer route buffer)");
    NavStreamRxItem item;
    for (;;) {
        if (navStreamRxQueue == nullptr || xQueueReceive(navStreamRxQueue, &item, portMAX_DELAY) != pdPASS) continue;
        const NavStreamStatus status = navStreamManager.processPacket(item.bytes, item.length, millis());
        if (status == NAV_STREAM_OK) {
            NavStreamSnapshot snapshot;
            navStreamManager.snapshot(snapshot);
            if (navStreamSnapshotMutex != nullptr) xSemaphoreTake(navStreamSnapshotMutex, portMAX_DELAY);
            navStreamPublishedSnapshot = snapshot;
            if (navStreamSnapshotMutex != nullptr) xSemaphoreGive(navStreamSnapshotMutex);
            notifyUiTask();
        } else {
            
            
            static uint32_t lastRejectLogMs = 0;
            const uint32_t now = millis();
            if ((uint32_t)(now - lastRejectLogMs) >= 1000UL) {
                lastRejectLogMs = now;
                Serial.printf("[NAV_STREAM] packet rejected status=%u type=%u len=%u stack_hwm=%u\n", (unsigned)status, (unsigned)item.bytes[3], (unsigned)item.length, (unsigned)uxTaskGetStackHighWaterMark(nullptr));
            }
        }
    }
}

static bool copyNavStreamSnapshot(NavStreamSnapshot& out) {
    if (navStreamSnapshotMutex != nullptr) xSemaphoreTake(navStreamSnapshotMutex, portMAX_DELAY);
    out = navStreamPublishedSnapshot;
    if (navStreamSnapshotMutex != nullptr) xSemaphoreGive(navStreamSnapshotMutex);
    return out.initialized;
}

void uiTask(void* parameter) {
    (void)parameter;
    Serial.println("UITask running");
    Serial.printf("[UI] BLE Connected: %s\n", bleConnected ? "YES" : "NO");

    for (;;) {
        
        
        processUiIteration();
#if CYBERNODE_DEBUG
        static uint32_t lastStackReportMs = 0;
        if ((uint32_t)(millis() - lastStackReportMs) >= 5000UL) {
            lastStackReportMs = millis();
            UBaseType_t highWaterWords = uxTaskGetStackHighWaterMark(nullptr);
            Serial.printf("[DEBUG] UITask stack high-water remaining: %u words\n",
                          (unsigned)highWaterWords);
        }
#endif
        const uint32_t waitMs = computeUiWaitMs();
        TickType_t waitTicks = pdMS_TO_TICKS(waitMs);
        if (waitTicks == 0) waitTicks = 1;
        ulTaskNotifyTake(pdTRUE, waitTicks);
    }
}



void loop() {

    if (uiTaskHandle == nullptr) {
        processUiIteration();
        vTaskDelay(pdMS_TO_TICKS(10)); 
    } else {
        
        
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
