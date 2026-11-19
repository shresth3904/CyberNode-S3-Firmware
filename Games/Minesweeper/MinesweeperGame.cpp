#include "MinesweeperGame.h"

void MinesweeperGame::init(uint32_t now) { restart(now); }

void MinesweeperGame::restart(uint32_t) {
    for (uint8_t i = 0; i < CELL_COUNT; ++i) {
        mines_[i] = false;
        revealed_[i] = false;
        adjacent_[i] = 0;
    }
    cursor_ = 5;
    revealedSafe_ = 0;
    state_ = 0;
    minesPlaced_ = false;
}

void MinesweeperGame::placeMines(uint8_t firstCell) {
    uint8_t placed = 0;
    while (placed < MINE_COUNT) {
        uint8_t candidate = (uint8_t)random(0, CELL_COUNT);
        if (candidate == firstCell || mines_[candidate]) continue;
        mines_[candidate] = true;
        ++placed;
    }
    for (uint8_t i = 0; i < CELL_COUNT; ++i) adjacent_[i] = countAdjacent(i);
    minesPlaced_ = true;
}

uint8_t MinesweeperGame::countAdjacent(uint8_t cell) const {
    int row = cell / 4;
    int col = cell % 4;
    uint8_t count = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            int nr = row + dr, nc = col + dc;
            if (nr >= 0 && nr < 4 && nc >= 0 && nc < 4 && mines_[nr * 4 + nc]) ++count;
        }
    }
    return count;
}

void MinesweeperGame::revealCurrent() {
    if (state_ != 0) { restart(millis()); return; }
    if (!minesPlaced_) placeMines(cursor_); 
    if (revealed_[cursor_]) return;
    revealed_[cursor_] = true;
    if (mines_[cursor_]) {
        state_ = 1;
        if (sound_) sound_->play(SoundEvent::MINE_EXPLOSION);
        return;
    }
    ++revealedSafe_;
    if (sound_) sound_->play(SoundEvent::MINE_REVEAL);

    
    if (adjacent_[cursor_] == 0) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (uint8_t cell = 0; cell < CELL_COUNT; ++cell) {
                if (!revealed_[cell] || mines_[cell] || adjacent_[cell] != 0) continue;
                int row = cell / 4, col = cell % 4;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        int nr = row + dr, nc = col + dc;
                        if (nr < 0 || nr >= 4 || nc < 0 || nc >= 4) continue;
                        uint8_t neighbor = (uint8_t)(nr * 4 + nc);
                        if (!revealed_[neighbor] && !mines_[neighbor]) {
                            revealed_[neighbor] = true;
                            ++revealedSafe_;
                            changed = true;
                        }
                    }
                }
            }
        }
    }
    if (revealedSafe_ >= CELL_COUNT - MINE_COUNT) {
        state_ = 2;
        if (sound_) sound_->play(SoundEvent::MINE_VICTORY);
    }
}

void MinesweeperGame::update(uint32_t, uint32_t) {}

void MinesweeperGame::draw(DisplayManager& display) {
    display.showMinesweeper(mines_, revealed_, adjacent_, cursor_, state_, revealedSafe_);
}

void MinesweeperGame::handleInput(ButtonEvent input, uint32_t now) {
    if (input == BUTTON_NEXT) cursor_ = (cursor_ + CELL_COUNT - 1) % CELL_COUNT;
    else if (input == BUTTON_BACK) cursor_ = (cursor_ + 1) % CELL_COUNT;
    else if (input == BUTTON_SELECT) revealCurrent();
    (void)now;
}

bool MinesweeperGame::isGameOver() const { return state_ != 0; }
void MinesweeperGame::exit() {}
