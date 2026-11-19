#include "BrickBreakerGame.h"
#include <math.h>

void BrickBreakerGame::init(uint32_t now) { restart(now); }

void BrickBreakerGame::resetBricks() {
    bricksRemaining_ = 0;
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < 10; ++col) {
            bool present = (level_ == 1) || ((row + col) % 4 != 0);
            bricks_[row][col] = present;
            if (present) ++bricksRemaining_;
        }
    }
}

void BrickBreakerGame::attachBall() {
    launched_ = false;
    ballX_ = paddleX_ + PADDLE_W / 2.0f;
    ballY_ = 52.0f;
    velocityX_ = (level_ == 1 ? 1.15f : 1.35f);
    velocityY_ = -(level_ == 1 ? 1.45f : 1.65f);
}

void BrickBreakerGame::restart(uint32_t) {
    paddleX_ = 52; lives_ = 3; level_ = 1; state_ = 0; score_ = 0; lastSoundMs_ = 0;
    resetBricks(); attachBall();
}

void BrickBreakerGame::update(uint32_t deltaTimeMs, uint32_t) {
    if (state_ != 0 || !launched_) return;
    float steps = deltaTimeMs / 30.0f;
    if (steps > 2.5f) steps = 2.5f;
    ballX_ += velocityX_ * steps;
    ballY_ += velocityY_ * steps;

    if (ballX_ <= 2.0f) { ballX_ = 2.0f; velocityX_ = fabsf(velocityX_); }
    if (ballX_ >= 125.0f) { ballX_ = 125.0f; velocityX_ = -fabsf(velocityX_); }
    if (ballY_ <= 11.0f) { ballY_ = 11.0f; velocityY_ = fabsf(velocityY_); }

    
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < 10; ++col) {
            if (!bricks_[row][col]) continue;
            int bx = 4 + col * 12, by = 14 + row * 7;
            if (ballX_ >= bx - 1 && ballX_ <= bx + 11 && ballY_ >= by - 2 && ballY_ <= by + 5) {
                bricks_[row][col] = false;
                if (bricksRemaining_ > 0) --bricksRemaining_;
                score_ += 10;
                velocityY_ = -velocityY_;
                if (sound_ && millis() - lastSoundMs_ > 65) { sound_->play(SoundEvent::BRICK_BREAK); lastSoundMs_ = millis(); }
                row = 3; break;
            }
        }
    }

    
    if (velocityY_ > 0 && ballY_ >= 53.0f && ballY_ <= 58.0f &&
        ballX_ >= paddleX_ - 1 && ballX_ <= paddleX_ + PADDLE_W + 1) {
        ballY_ = 52.5f;
        velocityY_ = -fabsf(velocityY_);
        float offset = (ballX_ - (paddleX_ + PADDLE_W / 2.0f)) / (PADDLE_W / 2.0f);
        velocityX_ = constrain(offset * 1.8f, -1.8f, 1.8f);
        if (fabsf(velocityX_) < 0.45f) velocityX_ = (velocityX_ < 0 ? -0.45f : 0.45f);
        if (sound_ && millis() - lastSoundMs_ > 80) { sound_->play(SoundEvent::BRICK_BOUNCE); lastSoundMs_ = millis(); }
    }

    if (ballY_ > 65.0f) {
        if (lives_ > 0) --lives_;
        if (lives_ == 0) { state_ = 1; if (sound_) sound_->play(SoundEvent::BRICK_GAME_OVER); }
        else attachBall();
    }

    if (bricksRemaining_ == 0 && state_ == 0) {
        if (level_ == 1) { level_ = 2; resetBricks(); attachBall(); if (sound_) sound_->play(SoundEvent::BRICK_LEVEL_UP); }
        else { state_ = 2; if (sound_) sound_->play(SoundEvent::BRICK_VICTORY); }
    }
}

void BrickBreakerGame::draw(DisplayManager& display) {
    display.showBrickBreaker(bricks_, paddleX_, constrain((int)ballX_, 0, 127), constrain((int)ballY_, 0, 63), launched_, score_, lives_, level_, state_);
}

void BrickBreakerGame::handleInput(ButtonEvent input, uint32_t) {
    if (input == BUTTON_NEXT && state_ == 0) paddleX_ = (int16_t)constrain(paddleX_ - 14, 1, 103);
    else if (input == BUTTON_BACK && state_ == 0) paddleX_ = (int16_t)constrain(paddleX_ + 14, 1, 103);
    else if (input == BUTTON_SELECT && state_ == 0 && !launched_) {
        launched_ = true;
        if (sound_) sound_->play(SoundEvent::BRICK_BOUNCE);
    }
    else if (input == BUTTON_SELECT && state_ != 0) restart(millis());
    if (!launched_ && state_ == 0) ballX_ = paddleX_ + PADDLE_W / 2.0f;
}

bool BrickBreakerGame::isGameOver() const { return state_ != 0; }
void BrickBreakerGame::exit() {}
