#ifndef CYBERNODE_HAPTIC_MANAGER_H
#define CYBERNODE_HAPTIC_MANAGER_H

#include <Arduino.h>



enum class HapticEvent : uint8_t {
    NAV_APPROACH,
    NAV_TURN,
    NOTIFICATION,
    ALERT,
    BOOT,
    TEST
};

class HapticManager {
public:
    void begin(uint8_t pin);
    void update(uint32_t now);
    uint32_t nextUpdateDelayMs(uint32_t now) const;

    void trigger(HapticEvent event);
    void startCallAlert();
    void stopCallAlert();

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    void setNavigationEnabled(bool enabled) { navigationEnabled_ = enabled; }
    bool navigationEnabled() const { return navigationEnabled_; }
    void setCallsEnabled(bool enabled);
    bool callsEnabled() const { return callsEnabled_; }
    void setNotificationsEnabled(bool enabled) { notificationsEnabled_ = enabled; }
    bool notificationsEnabled() const { return notificationsEnabled_; }
    void setAlertsEnabled(bool enabled) { alertsEnabled_ = enabled; }
    bool alertsEnabled() const { return alertsEnabled_; }
    void setStrength(uint8_t strength);
    uint8_t strength() const { return strength_; } 

    void stop();

private:
    static constexpr uint8_t MAX_SEGMENTS = 7;
    uint8_t pin_ = 10;
    bool enabled_ = true;
    bool navigationEnabled_ = true;
    bool callsEnabled_ = true;
    bool notificationsEnabled_ = true;
    bool alertsEnabled_ = true;
    uint8_t strength_ = 1;

    bool sequenceActive_ = false;
    bool sequenceIsCall_ = false;
    uint8_t sequencePriority_ = 0;
    bool callAlertRequested_ = false;
    uint16_t segments_[MAX_SEGMENTS]{}; 
    uint8_t segmentCount_ = 0;
    uint8_t segmentIndex_ = 0;
    uint32_t segmentDeadlineMs_ = 0;

    void startPattern(const uint16_t* segments, uint8_t count, uint32_t now,
                      bool isCall = false, uint8_t priority = 0);
    void startCallCycle(uint32_t now);
    void applyOutput();
};

#endif 
