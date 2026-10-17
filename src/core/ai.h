#pragma once

#include <cstdint>

#include "core/input.h"

namespace m360 {

// MUGEN's built-in computer player, for characters without AI of their own: it jams random directions and buttons,
// more often at higher AI levels (1 to 8). Characters with their own AI read the level with the AILevel trigger.
// Translated from Ikemen-GO src/input.go (AiInput).
class AiInput {
public:
	// One tick. active is false during intros and win poses (Ikemen: sys.intro != 0), when it lets go of everything.
	void update(float level, bool active);
	// What it's holding now; left and right are the screen's.
	InputState state() const;
	void seed(uint32_t s) { random_ = s; }

private:
	// A random number from lo to hi, inclusive.
	int32_t rand(int32_t lo, int32_t hi);
	// Counts a held key down; when it's free, presses it for 1..time frames with a 1 in chance chance.
	bool jam(int32_t &t, int32_t chance, int32_t time);

	int32_t dir_ = 0; // 0 U, 1 UR, 2 R, 3 DR, 4 D, 5 DL, 6 L, 7 UL
	int32_t dirt_ = 0, at_ = 0, bt_ = 0, ct_ = 0, xt_ = 0, yt_ = 0, zt_ = 0, st_ = 0;
	uint32_t random_ = 0x2545F491;
};

} // namespace m360
