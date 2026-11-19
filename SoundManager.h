#ifndef CYBERNODE_SOUND_MANAGER_H
#define CYBERNODE_SOUND_MANAGER_H
#include <Arduino.h>

enum class SoundEvent : uint8_t {
  NEXT, BACK, SELECT, LONG_PRESS, INVALID,
  STARTUP, BLE_CONNECTED, BLE_DISCONNECTED, NOTIFICATION, ALARM, TIMER_DONE, LOW_BATTERY, MENU_OPEN,
  DINO_JUMP, DINO_SCORE, DINO_GAME_OVER, DINO_RESTART, DINO_PAUSE, DINO_RESUME,
  MINE_REVEAL, MINE_FLAG, MINE_EXPLOSION, MINE_VICTORY,
  RACING_ENGINE, RACING_COLLISION, RACING_LAP, RACING_SHIELD,
  REACTION_READY, REACTION_GO, REACTION_RESULT, REACTION_FALSE_START,
  SPACE_LASER, SPACE_HIT, SPACE_PLAYER_DEATH, SPACE_VICTORY,
  BRICK_BOUNCE, BRICK_BREAK, BRICK_POWERUP, BRICK_GAME_OVER, BRICK_LEVEL_UP, BRICK_VICTORY,
  
  FORTUNE_INTRO, FORTUNE_CONTINUE, FORTUNE_GLITCH, FORTUNE_SWAP, FORTUNE_YAY,
  FORTUNE_REVEAL, FORTUNE_PUNCHLINE, SUPERIOR_LOGO, CEO_ACTIVATE, CEO_STEP, CEO_STATUS, HYPERWEAR_REVEAL, CALL_RING
};

class SoundManager {
public:
  void begin(uint8_t pin);
  void update(uint32_t now);
  
  uint32_t nextUpdateDelayMs(uint32_t now) const;
  void play(SoundEvent event);
  void setEnabled(bool enabled) { enabled_ = enabled; if (!enabled_) stop(); }
  bool isEnabled() const { return enabled_; }
  bool isPlaying() const { return sequenceActive_; }
  bool isNotificationActive() const { return notificationPriorityActive_ && sequenceActive_; }
  void setButtonSounds(bool enabled) { buttonSounds_ = enabled; }
  bool buttonSoundsEnabled() const { return buttonSounds_; }
  void setGameSounds(bool enabled) { gameSounds_ = enabled; }
  bool gameSoundsEnabled() const { return gameSounds_; }
  
  void setVolume(uint8_t level);
  uint8_t volume() const { return volume_; }
  void stop();
  
  void startCallRingtone();
  void stopCallRingtone();
  bool isCallRingtoneRequested() const { return callRingtoneRequested_; }
private:
  struct Note { uint16_t frequency; uint16_t duration; uint16_t gap; };
  static constexpr uint8_t MAX_NOTES = 8;
  uint8_t pin_ = 7;
  uint8_t volume_ = 100;
  bool enabled_ = true, buttonSounds_ = true, gameSounds_ = true;
  bool sequenceActive_ = false;
  bool sequenceIsCallRingtone_ = false;
  bool callRingtoneRequested_ = false;
  bool notificationPriorityActive_ = false;
  uint32_t notificationPriorityUntilMs_ = 0;
  Note sequence_[MAX_NOTES]{};
  uint8_t sequenceLength_ = 0, noteIndex_ = 0;
  uint32_t noteDeadline_ = 0;
  void load(const Note* notes, uint8_t count, uint32_t now);
  void startCurrent(uint32_t now);
  void startCallRingtoneCycle(uint32_t now);
};
#endif
