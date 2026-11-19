#include "SpaceInvadersGame.h"

void SpaceInvadersGame::init(uint32_t now) { restart(now); }

void SpaceInvadersGame::restart(uint32_t now) {
    lives_ = 3; wave_ = 1; state_ = 0; score_ = 0; playerX_ = 60;
    playerBulletActive_ = enemyBulletActive_ = false;
    invulnerableUntilMs_ = 0; lastPlayerShotMs_ = 0;
    spawnWave(now);
}

void SpaceInvadersGame::spawnWave(uint32_t now) {
    for (uint8_t i = 0; i < 8; ++i) invaders_[i] = true;
    baseX_ = 8; baseY_ = 15; direction_ = 1;
    playerBulletActive_ = enemyBulletActive_ = false;
    lastFormationMoveMs_ = now;
    lastEnemyShotMs_ = now;
}

uint8_t SpaceInvadersGame::remainingInvaders() const {
    uint8_t count = 0; for (uint8_t i = 0; i < 8; ++i) if (invaders_[i]) ++count; return count;
}

void SpaceInvadersGame::update(uint32_t, uint32_t now) {
    if (state_ != 0) return;
    uint32_t moveInterval = (uint32_t)constrain(240 - (int)(8 - remainingInvaders()) * 20 - (wave_ - 1) * 35, 75, 240);
    if (now - lastFormationMoveMs_ >= moveInterval) {
        lastFormationMoveMs_ = now;
        baseX_ += direction_ * 2;
        if (baseX_ >= 52) { baseX_ = 52; direction_ = -1; baseY_ += 3; }
        else if (baseX_ <= 4) { baseX_ = 4; direction_ = 1; baseY_ += 3; }
        if (baseY_ >= 39) {
            state_ = 1;
            if (sound_) sound_->play(SoundEvent::SPACE_PLAYER_DEATH);
        }
    }

    if (playerBulletActive_) {
        playerBulletY_ -= 3;
        if (playerBulletY_ < 12) playerBulletActive_ = false;
        else {
            for (uint8_t i = 0; i < 8; ++i) {
                if (!invaders_[i]) continue;
                int col = i % 4, row = i / 4;
                int ax = baseX_ + col * 17, ay = baseY_ + row * 9;
                if (playerBulletX_ >= ax - 1 && playerBulletX_ <= ax + 8 &&
                    playerBulletY_ >= ay - 1 && playerBulletY_ <= ay + 7) {
                    invaders_[i] = false; playerBulletActive_ = false; score_ += 10;
                    if (sound_) sound_->play(SoundEvent::SPACE_HIT);
                    break;
                }
            }
        }
    }

    if (!enemyBulletActive_ && now - lastEnemyShotMs_ >= (wave_ == 1 ? 1250U : 930U) && remainingInvaders() > 0) {
        uint8_t start = (uint8_t)random(0, 8);
        for (uint8_t k = 0; k < 8; ++k) {
            uint8_t i = (start + k) % 8;
            if (invaders_[i]) {
                enemyBulletX_ = baseX_ + (i % 4) * 17 + 4;
                enemyBulletY_ = baseY_ + (i / 4) * 9 + 6;
                enemyBulletActive_ = true;
                lastEnemyShotMs_ = now;
                break;
            }
        }
    }
    if (enemyBulletActive_) {
        enemyBulletY_ += 2;
        if (enemyBulletY_ > 63) enemyBulletActive_ = false;
        else if (enemyBulletY_ >= 51 && enemyBulletX_ >= playerX_ - 1 && enemyBulletX_ <= playerX_ + 10 &&
                 (int32_t)(invulnerableUntilMs_ - now) <= 0) {
            enemyBulletActive_ = false;
            if (lives_ > 0) --lives_;
            invulnerableUntilMs_ = now + 800;
            if (sound_) sound_->play(SoundEvent::SPACE_PLAYER_DEATH);
            if (lives_ == 0) state_ = 1;
        }
    }

    if (remainingInvaders() == 0) {
        if (wave_ == 1) { wave_ = 2; spawnWave(now); }
        else { state_ = 2; if (sound_) sound_->play(SoundEvent::SPACE_VICTORY); }
    }
}

void SpaceInvadersGame::draw(DisplayManager& display) {
    display.showSpaceInvaders(invaders_, baseX_, baseY_, playerX_, playerBulletActive_, playerBulletX_, playerBulletY_,
                              enemyBulletActive_, enemyBulletX_, enemyBulletY_, score_, lives_, wave_, state_);
}

void SpaceInvadersGame::handleInput(ButtonEvent input, uint32_t now) {
    if (input == BUTTON_NEXT && state_ == 0) playerX_ = (int16_t)constrain(playerX_ - 7, 2, 116);
    else if (input == BUTTON_BACK && state_ == 0) playerX_ = (int16_t)constrain(playerX_ + 7, 2, 116);
    else if (input == BUTTON_SELECT && state_ == 0 && !playerBulletActive_ && now - lastPlayerShotMs_ >= 170) {
        playerBulletActive_ = true; playerBulletX_ = playerX_ + 4; playerBulletY_ = 49; lastPlayerShotMs_ = now;
        if (sound_) sound_->play(SoundEvent::SPACE_LASER);
    } else if (input == BUTTON_SELECT && state_ != 0) restart(now);
}

bool SpaceInvadersGame::isGameOver() const { return state_ != 0; }
void SpaceInvadersGame::exit() {}
