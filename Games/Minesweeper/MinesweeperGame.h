#ifndef CYBERNODE_MINESWEEPER_GAME_H
#define CYBERNODE_MINESWEEPER_GAME_H

#include "../GameBase.h"
#include "../../SoundManager.h"

class MinesweeperGame : public GameBase {
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
    static const uint8_t CELL_COUNT = 16;
    static const uint8_t MINE_COUNT = 3;
    bool mines_[CELL_COUNT]{};
    bool revealed_[CELL_COUNT]{};
    uint8_t adjacent_[CELL_COUNT]{};
    uint8_t cursor_ = 0;
    uint8_t revealedSafe_ = 0;
    uint8_t state_ = 0; 
    bool minesPlaced_ = false;
    SoundManager* sound_ = nullptr;

    void placeMines(uint8_t firstCell);
    uint8_t countAdjacent(uint8_t cell) const;
    void revealCurrent();
};

#endif
