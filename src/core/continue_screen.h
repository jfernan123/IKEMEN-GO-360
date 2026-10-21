#pragma once

#include <cstdint>
#include <string>

#include "core/bgdef.h"
#include "core/layout.h"
#include "core/motif.h"
#include "platform/platform.h"

namespace m360 {

// Ikemen's continue screen, from a screenpack's [Continue Screen] and ContinueBG (motif.go's MotifContinue: init,
// step, draw), drawn over the lost fight's last frame. MUGEN's legacy mode asks "Continue?" with Yes / No (move keys
// switch, done or skip chooses); Ikemen's counts down with an animation (done continues, skip jumps to the next
// number, the end means no). Credits are unlimited here, so the credits text isn't drawn. Not yet: the characters'
// continue poses (p1.state, p1.yes.state, p1.no.state).
class ContinueScreen {
public:
	void load(Motif &motif);
	bool enabled() const { return enabled_; }
	// [Game Over Screen]: shown after a "no" when gameover.enabled (the countdown's own end otherwise).
	bool gameOverScreen() const { return gameOverEnabled_; }
	void start();
	void update(const platform::Pad &pad);
	void draw();
	bool done() const { return done_; }
	// Whether the player chose to continue.
	bool continued() const { return continued_; }

private:
	struct Count {
		int32_t skipTime = -1;
		int32_t snd[2] = { -1, 0 };
	};
	void choose(bool yes);
	void skipCounter();

	Motif *motif_ = nullptr;
	BgDef bg_;
	uint32_t clearColour_ = 0xFF000000;
	bool enabled_ = true, legacy_ = true, gameOverEnabled_ = true;
	float pos_[2] = { 160, 40 };
	TextLayout continue_, yes_, yesActive_, no_, noActive_;
	uint32_t overlayColour_ = 0;
	AnimLayout counterAnim_;
	int32_t counterEndTime_ = 0, counterSkipStart_ = 0;
	Count counts_[10], end_;
	int32_t moveSnd_[2] = { -1, 0 }, doneSnd_[2] = { -1, 0 };
	ScreenFade fadeIn_, fadeOut_;
	int32_t counter_ = 0, endTimer_ = -1;
	bool yesSide_ = true, selected_ = false, showEndAnim_ = false, continued_ = false, done_ = true;
	platform::Pad before_;
};

} // namespace m360
