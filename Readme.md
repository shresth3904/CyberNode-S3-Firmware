# CyberNode S3

CyberNode S3 is a custom ESP32-S3 smartwatch firmware featuring a compact OLED interface, BLE connectivity to a companion Android app, interactive watch faces, navigation, notifications, call controls, games, and local productivity tools. 

---

## Features

### Watch interface
- Seven watch faces: Node-Bot, Glitch, Data Rain, HUD, Neon City, Terminal, and Classic.
- Animated, cyberpunk-inspired visuals and boot identity animations.
- App dial/launcher with configurable app visibility.
- OLED sleep, timeout, and contrast settings.

### Node-Bot
- Animated Node-Bot watch face.
- Reactive expressions and behavior integrated into watch activity and events.

### Phone connectivity (BLE)
- Bluetooth Low Energy connection to a companion phone app.
- Phone-to-watch data transfer and watch-to-phone notifications over a UART-style BLE service.
- Phone data includes time/date and supported watch information such as weather and step count.
- Phone notifications displayed on the watch, with configurable popups and sound.
- Incoming-call UI with answer/reject controls, call state feedback, and ringtone behavior.

### Navigation
- Navigation display with route/map information, position updates, maneuver guidance, and alerts.
- Navigation stream support and BLE link/connection status handling.
- Navigation settings for automatic popup and keeping the screen awake.

### Apps and utilities
- Weather information screens.
- Step-count screens and history views when phone data is available.
- Local alarm with configurable alert tone.
- Countdown timer.
- Stopwatch with lap tracking.
- System information screens.

### Games
Six built-in games:
- Dino Runner
- Minesweeper
- Mini Racing
- Reaction Timer
- Space Invaders
- Brick Breaker

### Sound and haptics
- Buzzer sound effects and alerts.
- Call ringtone behavior.
- Haptic feedback for supported watch events.
- Separate settings for sound and haptic behavior/intensity.

### Settings and persistence
- Persistent settings stored using ESP32 Preferences/NVS.
- Configurable boot animations and identity screens.
- Notification, sound, haptic, navigation, display, and app settings.

---

## Hardware

The firmware targets an **ESP32-S3 Mini / ESP32-S3 Super Mini** board and a 128×64 SH1106 OLED display.

| Component | Firmware configuration |
|---|---|
| MCU | ESP32-S3 Mini / Super Mini |
| Display | SH1106, 128×64 OLED, I²C |
| OLED SDA | GPIO 8 |
| OLED SCL | GPIO 9 |
| NEXT button | GPIO 4 |
| SELECT button | GPIO 5 |
| BACK button | GPIO 6 |
| Buzzer | GPIO 7 |
| Haptic motor control | GPIO 10|
| Phone connection | Bluetooth Low Energy (BLE) |

Check your specific ESP32-S3 board pinout and wiring before connecting hardware. GPIO availability and board variants can differ.

The firmware's battery percentage field is currently a placeholder unless a compatible battery-monitoring source is integrated.

---

## Getting started

### 1. Install Arduino IDE
Install Arduino IDE and add the Espressif ESP32 board package through Boards Manager.

### 2. Select the board
Choose the ESP32-S3 board entry matching your hardware. If using an ESP32-S3 Super Mini, verify the board profile, USB settings, flash configuration, and available pins against the specific board revision.

### 3. Install the library
Install the **U8g2** library through Arduino Library Manager. The ESP32 BLE, Preferences, FreeRTOS, and other ESP32 framework components are provided by the ESP32 Arduino core.

### 4. Open the firmware
Open `CyberNode_S3.ino` from the CyberNode_S3 project folder in Arduino IDE. Keep the accompanying source and game folders/files together in the project directory so Arduino can resolve the firmware's includes.

### 5. Connect and upload
Connect the ESP32-S3 over USB, select the correct port, choose the board configuration, and upload the sketch. Open Serial Monitor at the baud rate configured in the sketch to view startup and debugging messages.

### 6. Hardware wiring
Wire the display, buttons, buzzer, and haptic-driver control according to the pin table above. Use an appropriate external driver for the vibration motor; do not drive a motor directly from an ESP32 GPIO.

---

## BLE connection with the companion Android app

The watch advertises over Bluetooth Low Energy as:

**CyberNode S3**

The firmware uses a UART-style BLE service with the following UUIDs:

| BLE item | UUID | Direction / purpose |
|---|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | BLE data service |
| RX characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Phone writes data to the watch |
| TX characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Watch sends notifications to phone |

### Connection procedure
1. Power on the watch and wait for BLE advertising to begin.
2. Open the CyberNode companion Android app and start scanning for the watch.
3. Select **CyberNode S3** from the discovered devices.
4. Connect and allow the app to discover the BLE service and characteristics.
5. Enable notifications on the TX characteristic if the app's BLE setup requires it.
6. Keep the connection active while using phone-dependent features such as notifications, calls, time/weather/step updates, and navigation.

For manual BLE inspection, a BLE utility such as nRF Connect can scan for `CyberNode S3`, connect, inspect the service, and subscribe to the TX characteristic. The firmware's RX characteristic accepts writes; TX is notify-only.

> The UUIDs and BLE roles above are defined by the firmware. The companion app must use the compatible data protocol and message formats; connecting at the BLE level alone does not guarantee app features will work.

---

## Controls

The watch uses three buttons:

| Button | GPIO | Typical role |
|---|---:|---|
| NEXT | 4 | Move through items / change selection |
| SELECT | 5 | Select, confirm, or activate |
| BACK | 6 | Return, exit, or cancel |

A long SELECT press is also supported in the input system. Button actions may vary by screen/context.

---

## Project files

The firmware's main sketch is `CyberNode_S3.ino`. Its supporting implementation files are kept alongside the sketch and in the `Games/` subdirectory.

Key source modules include:

- `DisplayManager.*` — OLED initialization, drawing, screens, and display sleep.
- `InputManager.*` — button scanning, debounce, and long-press handling.
- `NavigationManager.*` and `NavigationStream.*` — navigation state and stream handling.
- `WatchFaces.*` — home watch-face rendering.
- `NodeBot.h` — Node-Bot animation and behavior.
- `SoundManager.*` — sound events and buzzer control.
- `HapticManager.*` — vibration control and patterns.
- `SettingsConfig.h` — settings validation/defaults.
- `ClockSyncUtils.h` — clock synchronization helpers.
- `CyberNodeBootAnimation.h` and boot identity assets — startup animations and branding.
- `Games/` — game manager and individual games.

---


## Notes

- CyberNode S3 is a firmware project intended for compatible ESP32-S3 hardware and its configured peripherals.
- Features requiring phone data or a BLE companion connection will not work fully when disconnected.
- Board revisions and wiring variations may require changes to pin configuration.
