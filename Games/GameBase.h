#ifndef CYBERNODE_GAME_BASE_H
#define CYBERNODE_GAME_BASE_H

#include <Arduino.h>
#include "../DisplayManager.h"
#include "../InputManager.h"


class GameBase {
public:
    virtual ~GameBase() {}
    virtual void init(uint32_t now) = 0;
    virtual void update(uint32_t deltaTimeMs, uint32_t now) = 0;
    virtual void draw(DisplayManager& display) = 0;
    virtual void handleInput(ButtonEvent input, uint32_t now) = 0;
    virtual bool isGameOver() const = 0;
    virtual void restart(uint32_t now) = 0;
    virtual void exit() = 0;
};

#endif
