#include "MiniRacingGame.h"
#include <stdlib.h>
#include <math.h>

void MiniRacingGame::init(uint32_t now) { restart(now); }

void MiniRacingGame::restart(uint32_t now) {
    for (uint8_t i = 0; i < 3; ++i) obstacles_[i] = {-1, -12.0f, false};
    lane_ = 1; lives_ = 3; shields_ = 2; passed_ = 0; score_ = 0;
    state_ = 0; lastSpawnMs_ = now; shieldUntilMs_ = 0; hitEffectUntilMs_ = 0; shieldHitEffectUntilMs_ = 0; speed_ = 23.0f;
    if (sound_) sound_->play(SoundEvent::RACING_ENGINE);
}

void MiniRacingGame::update(uint32_t deltaTimeMs, uint32_t now) {
    if (state_ != 0) return;
    float dt = deltaTimeMs / 1000.0f;
    if (dt > 0.09f) dt = 0.09f;

    uint32_t spawnInterval = (uint32_t)constrain(980 - (int)passed_ * 10, 560, 980);
    if (now - lastSpawnMs_ >= spawnInterval) {
        for (uint8_t i = 0; i < 3; ++i) {
            if (!obstacles_[i].active) {
                int8_t candidateLane = (int8_t)random(0, 3);
                bool corridorClear = true;
                
                
                for (uint8_t j = 0; j < 3; ++j) {
                    if (obstacles_[j].active && obstacles_[j].y < 34.0f) {
                        if (fabsf(obstacles_[j].y - 13.0f) < 22.0f) corridorClear = false;
                    }
                }
                if (!corridorClear) break;
                obstacles_[i].lane = candidateLane;
                obstacles_[i].y = 13.0f;
                obstacles_[i].active = true;
                lastSpawnMs_ = now;
                break;
            }
        }
    }

    bool shieldActive = (int32_t)(shieldUntilMs_ - now) > 0;
    for (uint8_t i = 0; i < 3; ++i) {
        if (!obstacles_[i].active) continue;
        obstacles_[i].y += speed_ * dt;
        if (obstacles_[i].y > 60.0f) {
            obstacles_[i].active = false;
            ++passed_;
            score_ += 10;
            if (passed_ % 5 == 0 && sound_) sound_->play(SoundEvent::RACING_LAP);
            if (passed_ % 7 == 0 && speed_ < 36.0f) speed_ += 2.0f;
            continue;
        }
        if (obstacles_[i].lane == lane_ && obstacles_[i].y >= 43.0f && obstacles_[i].y <= 56.0f) {
            obstacles_[i].active = false;
            if (shieldActive) {
                shieldUntilMs_ = 0;
                shieldActive = false;
                if (sound_) sound_->play(SoundEvent::RACING_SHIELD);
                shieldHitEffectUntilMs_ = now + 420;
            } else {
                if (lives_ > 0) --lives_;
                hitEffectUntilMs_ = now + 520;
                if (sound_) sound_->play(SoundEvent::RACING_COLLISION);
                if (lives_ == 0) state_ = 1;
            }
        }
    }
}

void MiniRacingGame::draw(DisplayManager& display) {
    int lanes[3] = {lane_, lives_, shields_};
    int obstacleLanes[3]; int obstacleY[3]; bool active[3];
    for (uint8_t i = 0; i < 3; ++i) {
        obstacleLanes[i] = obstacles_[i].lane;
        obstacleY[i] = (int)obstacles_[i].y;
        active[i] = obstacles_[i].active;
    }
    display.showMiniRacing(lanes, obstacleLanes, obstacleY, active, score_,
                           (int32_t)(shieldUntilMs_ - millis()) > 0, state_,
                           (int32_t)(hitEffectUntilMs_ - millis()) > 0,
                           (int32_t)(shieldHitEffectUntilMs_ - millis()) > 0);
}

void MiniRacingGame::handleInput(ButtonEvent input, uint32_t now) {
    if (input == BUTTON_NEXT && state_ == 0) { if (lane_ > 0) --lane_; }
    else if (input == BUTTON_BACK && state_ == 0) { if (lane_ < 2) ++lane_; }
    else if (input == BUTTON_SELECT && state_ == 0 && shields_ > 0 && (int32_t)(shieldUntilMs_ - now) <= 0) {
        --shields_; shieldUntilMs_ = now + 950;
        if (sound_) sound_->play(SoundEvent::RACING_SHIELD);
    } else if (input == BUTTON_SELECT && state_ != 0) restart(now);
}

bool MiniRacingGame::isGameOver() const { return state_ != 0; }
void MiniRacingGame::exit() {}
