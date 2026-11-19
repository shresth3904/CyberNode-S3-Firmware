#ifndef CYBERNODE_MINI_RACING_GAME_H
#define CYBERNODE_MINI_RACING_GAME_H

#include "../GameBase.h"
#include "../../SoundManager.h"

class MiniRacingGame : public GameBase {
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
    struct Obstacle { int8_t lane; float y; bool active; };
    Obstacle obstacles_[3]{};
    uint8_t lane_ = 1;
    uint8_t lives_ = 3;
    uint8_t shields_ = 2;
    uint16_t passed_ = 0;
    uint16_t score_ = 0;
    uint8_t state_ = 0; 
    uint32_t lastSpawnMs_ = 0;
    uint32_t shieldUntilMs_ = 0;
    uint32_t hitEffectUntilMs_ = 0;
    uint32_t shieldHitEffectUntilMs_ = 0;
    float speed_ = 23.0f;
    SoundManager* sound_ = nullptr;
};

#endif
