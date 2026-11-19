#include "InputManager.h"

InputManager::InputManager() : lastActivityMs_(0) {
  buttons[0] = {4, HIGH, HIGH, 0, 0, false, false};
  buttons[1] = {5, HIGH, HIGH, 0, 0, false, false};
  buttons[2] = {6, HIGH, HIGH, 0, 0, false, false};
}

void InputManager::begin() {
  for (int i = 0; i < 3; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);

    buttons[i].lastReading = HIGH;
    buttons[i].stableState = HIGH;
    buttons[i].lastDebounceTime = millis();
    buttons[i].pressStartTime = 0;
    buttons[i].longPressHandled = false;
    buttons[i].wakePressConsumed = false;
  }
  
  lastActivityMs_ = millis();
}

ButtonEvent InputManager::update(bool wakeOnly) {
  ButtonEvent event = BUTTON_NONE;
  const uint32_t now = millis();

  for (int i = 0; i < 3; i++) {
    const bool reading = digitalRead(buttons[i].pin);

    if (reading != buttons[i].lastReading) {
      buttons[i].lastDebounceTime = now;
    }

    if ((uint32_t)(now - buttons[i].lastDebounceTime) > debounceDelay) {
      if (reading != buttons[i].stableState) {
        buttons[i].stableState = reading;

        
        if (buttons[i].stableState == LOW) {
          buttons[i].pressStartTime = now;
          buttons[i].longPressHandled = false;
          lastActivityMs_ = now;

          if (wakeOnly) {
            
            
            buttons[i].wakePressConsumed = true;
            if (event == BUTTON_NONE) event = BUTTON_WAKE;
          } else if (!buttons[i].wakePressConsumed) {
            
            if (i == 0) event = BUTTON_NEXT;
            if (i == 2) event = BUTTON_BACK;
          }
        }
        
        else {
          if (buttons[i].wakePressConsumed) {
            
            buttons[i].wakePressConsumed = false;
            buttons[i].longPressHandled = false;
          } else if (!wakeOnly && i == 1 && !buttons[i].longPressHandled) {
            
            event = BUTTON_SELECT;
          }
        }
      }

      
      
      if (!wakeOnly && i == 1 &&
          buttons[i].stableState == LOW &&
          !buttons[i].wakePressConsumed &&
          !buttons[i].longPressHandled &&
          (uint32_t)(now - buttons[i].pressStartTime) >= longPressDuration) {
        event = BUTTON_LONG_SELECT;
        buttons[i].longPressHandled = true;
      }
    }

    
    
    if (buttons[i].stableState == LOW) lastActivityMs_ = now;
    buttons[i].lastReading = reading;
  }

  return event;
}

bool InputManager::needsPolling() const {
  for (int i = 0; i < 3; ++i) {
    if (buttons[i].lastReading != buttons[i].stableState || buttons[i].stableState == LOW)
      return true;
  }
  return false;
}

uint32_t InputManager::lastActivityMs() const {
  return lastActivityMs_;
}

void InputManager::markActivity(uint32_t nowMs) {
  lastActivityMs_ = nowMs;
}
