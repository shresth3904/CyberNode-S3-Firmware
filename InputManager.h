#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>

enum ButtonEvent {
  BUTTON_NONE,
  BUTTON_NEXT,
  BUTTON_SELECT,
  BUTTON_BACK,
  BUTTON_LONG_SELECT,
  
  
  BUTTON_WAKE
};

class InputManager {
public:
  InputManager();
  void begin();
  ButtonEvent update(bool wakeOnly = false);
  
  bool needsPolling() const;
  uint32_t lastActivityMs() const;
  
  
  void markActivity(uint32_t nowMs);

private:
  struct Button {
    uint8_t pin;
    bool lastReading;
    bool stableState;
    unsigned long lastDebounceTime;
    unsigned long pressStartTime;
    bool longPressHandled;
    bool wakePressConsumed;
  };

  Button buttons[3];
  volatile uint32_t lastActivityMs_;

  const unsigned long debounceDelay = 40;
  const unsigned long longPressDuration = 800;
};

#endif
