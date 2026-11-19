#include "SoundManager.h"

void SoundManager::begin(uint8_t pin) { pin_ = pin; pinMode(pin_, OUTPUT); stop(); }
void SoundManager::setVolume(uint8_t level) { volume_ = level == 0 ? 1 : level; }
void SoundManager::stop() {
  noTone(pin_);
  callRingtoneRequested_ = false;
  sequenceIsCallRingtone_ = false;
  sequenceActive_ = false;
  notificationPriorityActive_ = false;
  notificationPriorityUntilMs_ = 0;
  sequenceLength_ = noteIndex_ = 0;
}
void SoundManager::load(const Note* notes, uint8_t count, uint32_t now) {
  if (!enabled_ || count == 0) return;
  if (count > MAX_NOTES) count = MAX_NOTES;
  for (uint8_t i=0;i<count;i++) sequence_[i]=notes[i];
  sequenceLength_=count; noteIndex_=0; sequenceActive_=true; sequenceIsCallRingtone_=false; startCurrent(now);
}
void SoundManager::startCurrent(uint32_t now) {
  if (noteIndex_ >= sequenceLength_) { stop(); return; }
  const Note &n=sequence_[noteIndex_];
  if (n.frequency) tone(pin_, n.frequency, n.duration);
  else noTone(pin_);
  noteDeadline_=now+n.duration+n.gap;
}
void SoundManager::startCallRingtoneCycle(uint32_t now) {
  if (!enabled_ || !callRingtoneRequested_) return;
  
  
  static const Note callRing[] = {
    {440, 300, 150}, {660, 300, 900}
  };
  for (uint8_t i = 0; i < sizeof(callRing) / sizeof(callRing[0]); ++i)
    sequence_[i] = callRing[i];
  sequenceLength_ = sizeof(callRing) / sizeof(callRing[0]);
  noteIndex_ = 0;
  sequenceActive_ = true;
  sequenceIsCallRingtone_ = true;
  startCurrent(now);
}

void SoundManager::startCallRingtone() {
  if (!enabled_) return;
  if (callRingtoneRequested_) return;
  callRingtoneRequested_ = true;
  
  
  if (!sequenceActive_) startCallRingtoneCycle(millis());
}

void SoundManager::stopCallRingtone() {
  callRingtoneRequested_ = false;
  if (sequenceIsCallRingtone_) {
    noTone(pin_);
    sequenceActive_ = false;
    sequenceIsCallRingtone_ = false;
    sequenceLength_ = noteIndex_ = 0;
  }
}

uint32_t SoundManager::nextUpdateDelayMs(uint32_t now) const {
  if (!sequenceActive_) return callRingtoneRequested_ ? 1UL : UINT32_MAX;
  const int32_t remaining = (int32_t)(noteDeadline_ - now);
  return remaining <= 0 ? 1UL : (uint32_t)remaining;
}

void SoundManager::update(uint32_t now) {
  if (!sequenceActive_) {
    if (callRingtoneRequested_) startCallRingtoneCycle(now);
    return;
  }
  if ((int32_t)(now-noteDeadline_) < 0) return;
  noTone(pin_);
  noteIndex_++;
  if (noteIndex_ >= sequenceLength_) {
    sequenceActive_ = false;
    sequenceIsCallRingtone_ = false;
    sequenceLength_ = noteIndex_ = 0;
    notificationPriorityActive_ = false;
    if (callRingtoneRequested_) startCallRingtoneCycle(now);
    return;
  }
  startCurrent(now);
}
void SoundManager::play(SoundEvent e) {
  if (!enabled_) return;
  bool button = e==SoundEvent::NEXT || e==SoundEvent::BACK || e==SoundEvent::SELECT || e==SoundEvent::LONG_PRESS || e==SoundEvent::INVALID || e==SoundEvent::MENU_OPEN;
  bool game = (e>=SoundEvent::DINO_JUMP && e<=SoundEvent::BRICK_VICTORY);
  if ((button && !buttonSounds_) || (game && !gameSounds_)) return;
  const uint32_t now=millis();
  const bool critical = e==SoundEvent::ALARM || e==SoundEvent::TIMER_DONE || e==SoundEvent::LOW_BATTERY;
  if (e == SoundEvent::CALL_RING) { startCallRingtone(); return; }
  
  
  if (callRingtoneRequested_ && !critical) return;
  
  
  
  if (notificationPriorityActive_ && (int32_t)(now-notificationPriorityUntilMs_) < 0 &&
      !critical && e!=SoundEvent::NOTIFICATION) return;
  if (critical) notificationPriorityActive_ = false;
  if (e==SoundEvent::NOTIFICATION) {
    notificationPriorityActive_ = true;
    notificationPriorityUntilMs_ = now + 170UL;
  }
  
  static const Note next[]={{2100,22,0}};
  static const Note back[]={{1050,35,0}};
  static const Note select[]={{1450,35,18},{2050,42,0}};
  static const Note longPress[]={{700,110,0}};
  static const Note invalid[]={{420,45,25},{320,55,0}};
  static const Note startup[]={{660,70,25},{990,70,25},{1320,95,30},{1760,130,0}};
  static const Note bleOn[]={{880,65,25},{1320,100,0}};
  static const Note bleOff[]={{1320,65,25},{880,90,25},{550,120,0}};
  static const Note notify[]={{1560,45,25},{1960,65,0}};
  static const Note alarm[]={{880,180,70},{1320,180,70},{880,180,70},{1320,180,0}};
  static const Note timer[]={{1040,90,25},{1310,90,25},{1560,160,0}};
  static const Note low[]={{520,120,80},{420,150,0}};
  static const Note menu[]={{1750,18,0}};
  static const Note jump[]={{1250,35,0}};
  static const Note score[]={{1400,45,20},{1800,45,20},{2200,75,0}};
  static const Note gameOver[]={{1000,80,30},{700,100,30},{480,150,35},{300,220,0}};
  static const Note restart[]={{600,45,20},{1200,65,0}};
  static const Note pause[]={{1000,55,25},{700,65,0}};
  static const Note resume[]={{700,55,25},{1000,65,0}};
  static const Note reveal[]={{1300,20,0}}, flag[]={{900,35,0}}, explosion[]={{220,180,0}}, victory[]={{880,70,20},{1180,70,20},{1580,130,0}};
  static const Note engine[]={{180,55,0}}, collision[]={{260,90,0}}, lap[]={{1000,45,20},{1500,80,0}}, shield[]={{720,40,12},{1180,75,0}};
  static const Note ready[]={{500,80,40},{500,80,40},{500,80,0}}, go[]={{1600,150,0}}, result[]={{1200,55,20},{1600,95,0}}, falseStart[]={{350,65,18},{250,95,0}};
  static const Note laser[]={{1900,30,0}}, hit[]={{850,45,0}}, death[]={{1100,70,25},{650,100,25},{300,180,0}}, spaceVictory[]={{1000,60,20},{1300,60,20},{1700,120,0}};
  static const Note bounce[]={{1200,18,0}}, brick[]={{800,25,0}}, power[]={{900,45,20},{1400,75,0}}, brickOver[]={{700,80,25},{400,140,0}};
  static const Note brickLevel[]={{880,55,18},{1320,70,0}}, brickVictory[]={{880,60,20},{1180,60,20},{1580,140,0}};
  static const Note fortuneIntro[]={{660,55,35},{880,75,0}};
  static const Note fortuneContinue[]={{1040,30,12},{1320,48,0}};
  static const Note fortuneGlitch[]={{390,35,12},{280,48,0}};
  static const Note fortuneSwap[]={{980,32,12},{1480,58,0}};
  static const Note fortuneYay[]={{880,55,18},{1180,65,18},{1580,125,0}};
  static const Note fortuneReveal[]={{520,75,22},{740,90,22},{1080,140,0}};
  static const Note fortunePunchline[]={{980,40,18},{1480,45,18},{1980,100,0}};
  static const Note superiorLogo[]={{1040,50,18},{1390,70,18},{1760,110,0}};
  static const Note ceoActivate[]={{360,85,25},{540,85,25},{810,125,0}};
  static const Note ceoStep[]={{1120,24,12},{1460,36,0}};
  static const Note ceoStatus[]={{880,45,18},{1320,65,0}};
  static const Note hyperwear[]={{740,55,18},{1110,75,18},{1480,115,0}};
#define LOAD(arr) load(arr, sizeof(arr)/sizeof(arr[0]), now)
  switch(e) {
    case SoundEvent::NEXT: LOAD(next); break; case SoundEvent::BACK: LOAD(back); break; case SoundEvent::SELECT: LOAD(select); break;
    case SoundEvent::LONG_PRESS: LOAD(longPress); break; case SoundEvent::INVALID: LOAD(invalid); break;
    case SoundEvent::STARTUP: LOAD(startup); break;
    case SoundEvent::BLE_CONNECTED: LOAD(bleOn); break; case SoundEvent::BLE_DISCONNECTED: LOAD(bleOff); break;
    case SoundEvent::NOTIFICATION: LOAD(notify); break; case SoundEvent::ALARM: LOAD(alarm); break; case SoundEvent::TIMER_DONE: LOAD(timer); break; case SoundEvent::LOW_BATTERY: LOAD(low); break; case SoundEvent::MENU_OPEN: LOAD(menu); break;
    case SoundEvent::DINO_JUMP: LOAD(jump); break; case SoundEvent::DINO_SCORE: LOAD(score); break; case SoundEvent::DINO_GAME_OVER: LOAD(gameOver); break; case SoundEvent::DINO_RESTART: LOAD(restart); break; case SoundEvent::DINO_PAUSE: LOAD(pause); break; case SoundEvent::DINO_RESUME: LOAD(resume); break;
    case SoundEvent::MINE_REVEAL: LOAD(reveal); break; case SoundEvent::MINE_FLAG: LOAD(flag); break; case SoundEvent::MINE_EXPLOSION: LOAD(explosion); break; case SoundEvent::MINE_VICTORY: LOAD(victory); break;
    case SoundEvent::RACING_ENGINE: LOAD(engine); break; case SoundEvent::RACING_COLLISION: LOAD(collision); break; case SoundEvent::RACING_LAP: LOAD(lap); break; case SoundEvent::RACING_SHIELD: LOAD(shield); break;
    case SoundEvent::REACTION_READY: LOAD(ready); break; case SoundEvent::REACTION_GO: LOAD(go); break; case SoundEvent::REACTION_RESULT: LOAD(result); break; case SoundEvent::REACTION_FALSE_START: LOAD(falseStart); break;
    case SoundEvent::SPACE_LASER: LOAD(laser); break; case SoundEvent::SPACE_HIT: LOAD(hit); break; case SoundEvent::SPACE_PLAYER_DEATH: LOAD(death); break; case SoundEvent::SPACE_VICTORY: LOAD(spaceVictory); break;
    case SoundEvent::BRICK_BOUNCE: LOAD(bounce); break; case SoundEvent::BRICK_BREAK: LOAD(brick); break; case SoundEvent::BRICK_POWERUP: LOAD(power); break; case SoundEvent::BRICK_GAME_OVER: LOAD(brickOver); break; case SoundEvent::BRICK_LEVEL_UP: LOAD(brickLevel); break; case SoundEvent::BRICK_VICTORY: LOAD(brickVictory); break;
    case SoundEvent::FORTUNE_INTRO: LOAD(fortuneIntro); break;
    case SoundEvent::FORTUNE_CONTINUE: LOAD(fortuneContinue); break;
    case SoundEvent::FORTUNE_GLITCH: LOAD(fortuneGlitch); break;
    case SoundEvent::FORTUNE_SWAP: LOAD(fortuneSwap); break;
    case SoundEvent::FORTUNE_YAY: LOAD(fortuneYay); break;
    case SoundEvent::FORTUNE_REVEAL: LOAD(fortuneReveal); break;
    case SoundEvent::FORTUNE_PUNCHLINE: LOAD(fortunePunchline); break;
    case SoundEvent::SUPERIOR_LOGO: LOAD(superiorLogo); break;
    case SoundEvent::CEO_ACTIVATE: LOAD(ceoActivate); break;
    case SoundEvent::CEO_STEP: LOAD(ceoStep); break;
    case SoundEvent::CEO_STATUS: LOAD(ceoStatus); break;
    case SoundEvent::HYPERWEAR_REVEAL: LOAD(hyperwear); break;
    case SoundEvent::CALL_RING: startCallRingtone(); break;
  }
#undef LOAD
}
