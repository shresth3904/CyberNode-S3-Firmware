#ifndef CYBERNODE_BRICK_BREAKER_GAME_H
#define CYBERNODE_BRICK_BREAKER_GAME_H

#include "../GameBase.h"
#include "../../SoundManager.h"

class BrickBreakerGame : public GameBase {
public:
    void init(uint32_t now) override;
    void update(uint32_t deltaTimeMs, uint32_t now) override;
    void draw(DisplayManager& display) override;
    void handleInput(ButtonEvent input, uint32_t now) override;
    bool isGameOver() const override;
    void restart(uint32_t now) override;
    void exit() override;
    void setSoundManager(SoundManager* sound) { sound_ = sound; }

private:
    bool bricks_[3][10]{};
    int16_t paddleX_ = 52;
    static const int16_t PADDLE_W = 24;
    float ballX_ = 64.0f, ballY_ = 52.0f, velocityX_ = 1.15f, velocityY_ = -1.45f;
    bool launched_ = false;
    uint8_t lives_ = 3, level_ = 1, state_ = 0; 
    uint8_t bricksRemaining_ = 30;
    uint16_t score_ = 0;
    uint32_t lastSoundMs_ = 0;
    SoundManager* sound_ = nullptr;

    void resetBricks();
    void attachBall();
};

#endif
