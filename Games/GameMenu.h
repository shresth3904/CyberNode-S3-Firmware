#ifndef CYBERNODE_GAME_MENU_H
#define CYBERNODE_GAME_MENU_H

#include <Arduino.h>
#include "../DisplayManager.h"

class GameMenu {
public:
    static const int COUNT = 6;
    int selected() const;
    const char* nameAt(int index) const;
    void reset();
    void next();
    void previous();
    void draw(DisplayManager& display) const;
private:
    int selectedIndex = 0;
    static const char* const names[COUNT];
};

#endif
