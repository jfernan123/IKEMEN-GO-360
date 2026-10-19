#pragma once

#include <cstdint>
#include <string>

#include "core/bgdef.h"
#include "core/layout.h"
#include "core/motif.h"
#include "core/versus_screen.h"
#include "platform/platform.h"

namespace m360 {

// Ikemen's victory screen, from a screenpack's [Victory Screen] and VictoryBG (motif.go's MotifVictory: init, step,
// draw): after a match, the winner's portrait (p1 slot) and the loser's (p2 slot, dimmed by lose.brightness), the
// winner's name, and its victory quote typed out and wrapped to the quote's window. It fades in, lasts "time" once
// the quote is typed, and a done key ends it (Start first finishes typing). One character a side.
class VictoryScreen {
public:
	void load(Motif &motif);
	// Whether the screenpack shows it after this match: enabled, and cpu.enabled when the computer won,
	// vs.enabled in a versus match.
	bool wanted(bool computerWon, bool versusMode) const;
	// The portrait sprite each slot draws, for the select screen to keep.
	int32_t sprite(int slot, int i) const { return slots_[slot].spr[i]; }
	// fighters[0] the winner, [1] the loser.
	void start(const VersusScreen::Fighter fighters[2], const std::string &quote);
	void update(const platform::Pad &pad1, const platform::Pad &pad2);
	void draw();
	bool done() const { return done_; }

private:
	struct Slot {
		int32_t spr[2] = { 9000, 2 };
		Layout face;
		bool applyPal = false;
		int32_t loseBrightness = 100;
		TextLayout name;
		VersusScreen::Fighter fighter;
	};
	void readSlot(Slot &slot, const std::string &prefix, const IniSection &s);
	void drawSlot(const Slot &slot, bool loser, int32_t layer);
	// The quote broken into lines that fit the winquote window (Ikemen's textwrap "w").
	std::string wrapped(const std::string &text) const;

	Motif *motif_ = nullptr;
	BgDef bg_;
	uint32_t clearColour_ = 0xFF000000;
	Slot slots_[2];
	TextLayout quote_;
	std::string defaultQuote_, text_;
	float textDelay_ = 2;
	int32_t time_ = 300;
	bool enabled_ = false, cpuEnabled_ = true, vsEnabled_ = true;
	ScreenFade fadeIn_, fadeOut_;
	int32_t counter_ = 0, typed_ = 0;
	bool fadingOut_ = false, done_ = true;
	platform::Pad before_[2];
};

} // namespace m360
