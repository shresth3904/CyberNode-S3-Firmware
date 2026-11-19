#ifndef CYBERNODE_GAME_MANAGER_H
#define CYBERNODE_GAME_MANAGER_H

#include "Dino/DinoGame.h"
#include "Minesweeper/MinesweeperGame.h"
#include "MiniRacing/MiniRacingGame.h"
#include "ReactionTimer/ReactionTimerGame.h"
#include "SpaceInvaders/SpaceInvadersGame.h"
#include "BrickBreaker/BrickBreakerGame.h"
#include "../SoundManager.h"

class GameManager {
public:
    static const uint8_t GAME_COUNT = 6;
    void setSoundManager(SoundManager* sound);
    void startGame(uint8_t gameIndex, uint32_t now);
    void update(uint32_t now);
    void draw(DisplayManager& display);
    void handleInput(ButtonEvent input, uint32_t now);
    void exitCurrentGame();
    uint8_t currentGameIndex() const { return currentIndex_; }
    bool hasActiveGame() const { return activeGame != nullptr; }

private:
    GameBase* activeGame = nullptr;
    uint8_t currentIndex_ = 0;
    uint32_t lastUpdateMs = 0;
    SoundManager* sound_ = nullptr;
    DinoGame dinoGame;
    MinesweeperGame minesweeperGame;
    MiniRacingGame miniRacingGame;
    ReactionTimerGame reactionTimerGame;
    SpaceInvadersGame spaceInvadersGame;
    BrickBreakerGame brickBreakerGame;
};

#endif
