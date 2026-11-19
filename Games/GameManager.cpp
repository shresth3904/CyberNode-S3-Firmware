#include "GameManager.h"

void GameManager::setSoundManager(SoundManager* sound) {
    sound_ = sound;
    dinoGame.setSoundManager(sound);
    minesweeperGame.setSoundManager(sound);
    miniRacingGame.setSoundManager(sound);
    reactionTimerGame.setSoundManager(sound);
    spaceInvadersGame.setSoundManager(sound);
    brickBreakerGame.setSoundManager(sound);
}

void GameManager::startGame(uint8_t gameIndex, uint32_t now) {
    
    exitCurrentGame();
    if (gameIndex >= GAME_COUNT) {
        activeGame = nullptr;
        currentIndex_ = 0;
        lastUpdateMs = now;
        return;
    }
    currentIndex_ = gameIndex;
    switch (gameIndex) {
        case 0: activeGame = &dinoGame; break;
        case 1: activeGame = &minesweeperGame; break;
        case 2: activeGame = &miniRacingGame; break;
        case 3: activeGame = &reactionTimerGame; break;
        case 4: activeGame = &spaceInvadersGame; break;
        case 5: activeGame = &brickBreakerGame; break;
        default: activeGame = nullptr; return;
    }
    activeGame->init(now);
    lastUpdateMs = now;
}

void GameManager::update(uint32_t now) {
    if (!activeGame) return;
    uint32_t delta = now - lastUpdateMs;
    lastUpdateMs = now;
    if (delta > 100) delta = 100;
    activeGame->update(delta, now);
}

void GameManager::draw(DisplayManager& display) {
    if (activeGame) activeGame->draw(display);
}

void GameManager::handleInput(ButtonEvent input, uint32_t now) {
    if (activeGame) activeGame->handleInput(input, now);
}

void GameManager::exitCurrentGame() {
    if (activeGame) activeGame->exit();
    activeGame = nullptr;
}
