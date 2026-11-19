#ifndef CYBERNODE_DINO_GAME_H
#define CYBERNODE_DINO_GAME_H

#include "../GameBase.h"
#include "../../SoundManager.h"

class DinoGame : public GameBase {
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
    static const int DINO_X = 12;
    static const int GROUND_Y = 50;
    static const int DINO_W = 9;
    static const int DINO_H = 12;
    static const int CACTUS_W = 6;
    static const int CACTUS_H = 12;
    static const uint32_t FRAME_INTERVAL_MS = 35;

    float dinoY;
    float velocityY;
    float cactusX;
    int score;
    bool gameOver;
    uint32_t lastFrameMs;
    uint32_t lastScoreMs;
    int lastMilestoneScore = 0;
    SoundManager* sound_ = nullptr;
};

#endif
