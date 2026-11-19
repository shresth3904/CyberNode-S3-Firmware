#ifndef CYBERNODE_SPACE_INVADERS_GAME_H
#define CYBERNODE_SPACE_INVADERS_GAME_H

#include "../GameBase.h"
#include "../../SoundManager.h"

class SpaceInvadersGame : public GameBase {
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
    bool invaders_[8]{};
    int16_t baseX_ = 8;
    int16_t baseY_ = 15;
    int8_t direction_ = 1;
    int16_t playerX_ = 60;
    int16_t playerBulletX_ = 0, playerBulletY_ = 0;
    int16_t enemyBulletX_ = 0, enemyBulletY_ = 0;
    bool playerBulletActive_ = false, enemyBulletActive_ = false;
    uint8_t lives_ = 3, wave_ = 1, state_ = 0; 
    uint16_t score_ = 0;
    uint32_t lastFormationMoveMs_ = 0, lastEnemyShotMs_ = 0, lastPlayerShotMs_ = 0;
    uint32_t invulnerableUntilMs_ = 0;
    SoundManager* sound_ = nullptr;

    void spawnWave(uint32_t now);
    uint8_t remainingInvaders() const;
};

#endif
