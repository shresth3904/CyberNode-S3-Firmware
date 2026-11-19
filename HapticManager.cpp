#include "HapticManager.h"

void HapticManager::begin(uint8_t pin) {
    pin_ = pin;
    pinMode(pin_, OUTPUT);
    analogWriteFrequency(pin_, 20000); 
    analogWriteResolution(pin_, 8);
    analogWrite(pin_, 0);
}

void HapticManager::applyOutput() {
    const bool on = sequenceActive_ && ((segmentIndex_ & 1U) == 0U);
    
    
    const uint8_t duty = strength_ == 0 ? 155 : (strength_ == 1 ? 205 : 255);
    analogWrite(pin_, on ? duty : 0);
}

void HapticManager::stop() {
    sequenceActive_ = false;
    sequenceIsCall_ = false;
    sequencePriority_ = 0;
    callAlertRequested_ = false;
    segmentCount_ = segmentIndex_ = 0;
    analogWrite(pin_, 0);
}

void HapticManager::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) stop();
}

void HapticManager::setCallsEnabled(bool enabled) {
    callsEnabled_ = enabled;
    if (!enabled_) stopCallAlert();
}

void HapticManager::setStrength(uint8_t strength) {
    strength_ = strength > 2 ? 2 : strength;
}

void HapticManager::startPattern(const uint16_t* segments, uint8_t count, uint32_t now,
                                 bool isCall, uint8_t priority) {
    if (!enabled_ || !segments || count == 0) return;
    if (count > MAX_SEGMENTS) count = MAX_SEGMENTS;
    for (uint8_t i = 0; i < count; ++i) segments_[i] = segments[i];
    segmentCount_ = count;
    segmentIndex_ = 0;
    segmentDeadlineMs_ = now + segments_[0];
    sequenceActive_ = true;
    sequenceIsCall_ = isCall;
    sequencePriority_ = priority;
    applyOutput();
}

void HapticManager::startCallCycle(uint32_t now) {
    if (!enabled_ || !callsEnabled_ || !callAlertRequested_) return;
    
    
    const uint16_t on = strength_ == 0 ? 65 : (strength_ == 1 ? 100 : 135);
    const uint16_t gap = strength_ == 0 ? 150 : (strength_ == 1 ? 130 : 110);
    const uint16_t pause = strength_ == 0 ? 950 : (strength_ == 1 ? 800 : 700);
    const uint16_t pattern[] = {on, gap, on, pause};
    startPattern(pattern, 4, now, true, 4);
}

void HapticManager::startCallAlert() {
    if (!enabled_ || !callsEnabled_) return;
    callAlertRequested_ = true;
    if (!sequenceActive_) startCallCycle(millis());
}

void HapticManager::stopCallAlert() {
    callAlertRequested_ = false;
    if (sequenceIsCall_) {
        sequenceActive_ = false;
        sequenceIsCall_ = false;
        segmentCount_ = segmentIndex_ = 0;
        sequencePriority_ = 0;
        analogWrite(pin_, 0);
    }
}

void HapticManager::trigger(HapticEvent event) {
    if (!enabled_) return;

    if (event == HapticEvent::NAV_APPROACH || event == HapticEvent::NAV_TURN) {
        if (!navigationEnabled_) return;
    } else if (event == HapticEvent::NOTIFICATION) {
        if (!notificationsEnabled_) return;
    } else if (event == HapticEvent::ALERT) {
        if (!alertsEnabled_) return;
    }

    
    
    
    if (callAlertRequested_ && event != HapticEvent::ALERT) return;
    uint8_t priority = 1;
    switch (event) {
        case HapticEvent::NOTIFICATION: priority = 1; break;
        case HapticEvent::NAV_APPROACH: priority = 2; break;
        case HapticEvent::NAV_TURN: priority = 4; break;
        case HapticEvent::BOOT: priority = 3; break;
        case HapticEvent::TEST: priority = 5; break;
        case HapticEvent::ALERT: priority = 6; break;
    }
    if (sequenceActive_ && event != HapticEvent::ALERT && priority <= sequencePriority_) return;

    const uint16_t shortOn = strength_ == 0 ? 28 : (strength_ == 1 ? 45 : 68);
    const uint16_t mediumOn = strength_ == 0 ? 48 : (strength_ == 1 ? 72 : 105);
    const uint16_t longOn = strength_ == 0 ? 75 : (strength_ == 1 ? 115 : 165);
    const uint16_t bootOn = strength_ == 0 ? 24 : (strength_ == 1 ? 34 : 46);
    const uint16_t gap = strength_ == 0 ? 85 : (strength_ == 1 ? 75 : 65);
    const uint32_t now = millis();

    switch (event) {
        case HapticEvent::NAV_APPROACH: {
            const uint16_t pattern[] = {shortOn};
            startPattern(pattern, 1, now, false, priority);
            break;
        }
        case HapticEvent::NAV_TURN: {
            const uint16_t pattern[] = {mediumOn, gap, mediumOn};
            startPattern(pattern, 3, now, false, priority);
            break;
        }
        case HapticEvent::NOTIFICATION: {
            
            const uint16_t pattern[] = {shortOn, gap, shortOn};
            startPattern(pattern, 3, now, false, priority);
            break;
        }
        case HapticEvent::ALERT: {
            
            const uint16_t pattern[] = {longOn, gap, longOn, gap, longOn};
            startPattern(pattern, 5, now, false, priority);
            break;
        }
        case HapticEvent::BOOT: {
            
            const uint16_t pattern[] = {bootOn};
            startPattern(pattern, 1, now, false, priority);
            break;
        }
        case HapticEvent::TEST: {
            const uint16_t pattern[] = {longOn};
            startPattern(pattern, 1, now, false, priority);
            break;
        }
    }
}

void HapticManager::update(uint32_t now) {
    if (!sequenceActive_) {
        if (callAlertRequested_) startCallCycle(now);
        return;
    }

    
    
    uint8_t guard = 0;
    while (sequenceActive_ && (int32_t)(now - segmentDeadlineMs_) >= 0 && guard++ < MAX_SEGMENTS) {
        ++segmentIndex_;
        if (segmentIndex_ >= segmentCount_) {
            const bool wasCall = sequenceIsCall_;
            sequenceActive_ = false;
            sequenceIsCall_ = false;
            sequencePriority_ = 0;
            segmentCount_ = segmentIndex_ = 0;
            analogWrite(pin_, 0);
            if (wasCall && callAlertRequested_) startCallCycle(now);
            else if (callAlertRequested_) startCallCycle(now);
            return;
        }
        segmentDeadlineMs_ += segments_[segmentIndex_];
        applyOutput();
    }
}

uint32_t HapticManager::nextUpdateDelayMs(uint32_t now) const {
    if (!sequenceActive_) return callAlertRequested_ ? 1UL : UINT32_MAX;
    const int32_t remaining = (int32_t)(segmentDeadlineMs_ - now);
    return remaining <= 0 ? 1UL : (uint32_t)remaining;
}
