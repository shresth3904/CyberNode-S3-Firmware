#include "DinoGame.h"

void DinoGame::init(uint32_t now) { restart(now); }

void DinoGame::restart(uint32_t now) {
    dinoY = GROUND_Y - DINO_H;
    velocityY = 0.0f;
    cactusX = 128.0f;
    score = 0;
    lastMilestoneScore = 0;
    gameOver = false;
    lastFrameMs = now;
    lastScoreMs = now;
}

void DinoGame::update(uint32_t deltaTimeMs, uint32_t now) {
    if (gameOver) return;
    float dt = deltaTimeMs / (float)FRAME_INTERVAL_MS;
    if (dt > 2.5f) dt = 2.5f;

    velocityY += 0.48f * dt; 
    dinoY += velocityY * dt;
    const float groundTop = GROUND_Y - DINO_H;
    if (dinoY >= groundTop) { dinoY = groundTop; velocityY = 0.0f; }

    cactusX -= 2.8f * dt; 
    if (cactusX < -CACTUS_W) cactusX = 128.0f + random(12, 42);

    
    int dinoLeft = DINO_X + 1, dinoRight = DINO_X + DINO_W - 1;
    int dinoTop = (int)dinoY + 1, dinoBottom = (int)dinoY + DINO_H - 1;
    int cactusLeft = (int)cactusX, cactusRight = (int)cactusX + CACTUS_W;
    int cactusTop = GROUND_Y - CACTUS_H, cactusBottom = GROUND_Y;
    if (dinoRight > cactusLeft && dinoLeft < cactusRight &&
        dinoBottom > cactusTop && dinoTop < cactusBottom) {
        gameOver = true;
        if (sound_) sound_->play(SoundEvent::DINO_GAME_OVER);
    }

    if (now - lastScoreMs >= 1000) { lastScoreMs = now; score++; if (score > 0 && score % 10 == 0 && score != lastMilestoneScore) { lastMilestoneScore = score; if (sound_) sound_->play(SoundEvent::DINO_SCORE); } }
}

void DinoGame::draw(DisplayManager& display) {
    display.showDinoGame(DINO_X, (int)dinoY, (int)cactusX, score, gameOver);
}

void DinoGame::handleInput(ButtonEvent input, uint32_t now) {
    if (input != BUTTON_SELECT) return;
    if (gameOver) { restart(now); if (sound_) sound_->play(SoundEvent::DINO_RESTART); }
    else if (dinoY >= GROUND_Y - DINO_H - 0.5f) { velocityY = -5.2f; if (sound_) sound_->play(SoundEvent::DINO_JUMP); } 
}

bool DinoGame::isGameOver() const { return gameOver; }
void DinoGame::exit() {  }
