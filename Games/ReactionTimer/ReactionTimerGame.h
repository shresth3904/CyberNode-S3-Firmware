#ifndef CYBERNODE_REACTION_TIMER_GAME_H
#define CYBERNODE_REACTION_TIMER_GAME_H

#include "../GameBase.h"
#include "../../SoundManager.h"

class ReactionTimerGame : public GameBase {
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
    enum State : uint8_t { READY, WAITING, GO, RESULT, SUMMARY };
    State state_ = READY;
    uint8_t difficulty_ = 1; 
    uint8_t round_ = 1;
    uint16_t reactionMs_ = 0;
    uint16_t scores_[3] = {0, 0, 0};
    uint16_t bestMs_ = 0;
    uint16_t averageMs_ = 0;
    uint8_t falseStarts_ = 0;
    bool falseStart_ = false;
    uint32_t waitDeadline_ = 0;
    uint32_t goAtMs_ = 0;
    SoundManager* sound_ = nullptr;

    void beginRound(uint32_t now);
    void finishRound();
};

#endif
