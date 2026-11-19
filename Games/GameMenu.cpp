#include "GameMenu.h"

const char* const GameMenu::names[GameMenu::COUNT] = {
    "Dino Runner", "Minesweeper", "Mini Racing", "Reaction Timer", "Space Invaders", "Brick Breaker"
};

int GameMenu::selected() const { return selectedIndex; }
const char* GameMenu::nameAt(int index) const {
    if (index < 0 || index >= COUNT) return "Unknown Game";
    return names[index];
}
void GameMenu::reset() { selectedIndex = 0; }
void GameMenu::next() { selectedIndex = (selectedIndex + 1) % COUNT; }
void GameMenu::previous() { selectedIndex = (selectedIndex + COUNT - 1) % COUNT; }
void GameMenu::draw(DisplayManager& display) const { display.showGameList(selectedIndex, names, COUNT); }
