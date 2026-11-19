#include "ReactionTimerGame.h"

void ReactionTimerGame::init(uint32_t now) { restart(now); }

void ReactionTimerGame::restart(uint32_t) {
    state_ = READY; difficulty_ = 1; round_ = 1;
    reactionMs_ = 0; bestMs_ = 0; averageMs_ = 0; falseStarts_ = 0; falseStart_ = false;
    scores_[0] = scores_[1] = scores_[2] = 0;
    waitDeadline_ = goAtMs_ = 0;
}

void ReactionTimerGame::beginRound(uint32_t now) {
    uint16_t minDelay = difficulty_ == 0 ? 2600 : (difficulty_ == 1 ? 1700 : 950);
    uint16_t maxDelay = difficulty_ == 0 ? 5000 : (difficulty_ == 1 ? 4100 : 2850);
    waitDeadline_ = now + (uint32_t)random(minDelay, maxDelay + 1);
    state_ = WAITING;
    falseStart_ = false;
    if (sound_) sound_->play(SoundEvent::REACTION_READY);
}

void ReactionTimerGame::finishRound() {
    uint32_t sum = 0; uint8_t valid = 0;
    for (uint8_t i = 0; i < 3; ++i) if (scores_[i] > 0) { sum += scores_[i]; ++valid; }
    averageMs_ = valid ? (uint16_t)(sum / valid) : 0;
    if (round_ >= 3) state_ = SUMMARY;
    else { ++round_; state_ = READY; }
}

void ReactionTimerGame::update(uint32_t, uint32_t now) {
    if (state_ == WAITING && (int32_t)(now - waitDeadline_) >= 0) {
        state_ = GO; goAtMs_ = now;
        if (sound_) sound_->play(SoundEvent::REACTION_GO);
    }
}

void ReactionTimerGame::draw(DisplayManager& display) {
    display.showReactionTimer((uint8_t)state_, difficulty_, round_, reactionMs_, averageMs_, bestMs_, falseStarts_, falseStart_);
}

void ReactionTimerGame::handleInput(ButtonEvent input, uint32_t now) {
    if (state_ == READY && input == BUTTON_NEXT) {
        difficulty_ = (difficulty_ + 2) % 3; 
        return;
    }
    if (state_ == READY && input == BUTTON_BACK) {
        difficulty_ = (difficulty_ + 1) % 3; 
        return;
    }
    if (input != BUTTON_SELECT) return;

    if (state_ == READY) beginRound(now);
    else if (state_ == WAITING) {
        falseStart_ = true; reactionMs_ = 0; scores_[round_ - 1] = 0; ++falseStarts_;
        state_ = RESULT;
        if (sound_) sound_->play(SoundEvent::REACTION_FALSE_START);
    } else if (state_ == GO) {
        uint32_t measured = now - goAtMs_;
        reactionMs_ = (uint16_t)constrain((int)measured, 1, 9999);
        scores_[round_ - 1] = reactionMs_;
        if (bestMs_ == 0 || reactionMs_ < bestMs_) bestMs_ = reactionMs_;
        state_ = RESULT;
        if (sound_) sound_->play(SoundEvent::REACTION_RESULT);
    } else if (state_ == RESULT) {
        finishRound();
    } else if (state_ == SUMMARY) restart(now);
}

bool ReactionTimerGame::isGameOver() const { return state_ == SUMMARY; }
void ReactionTimerGame::exit() {}
