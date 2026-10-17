#pragma once

#include <cstdint>

#include "core/character.h"
#include "core/match.h"

namespace m360 {

// The fight screen, drawn plainly until a screenpack's lifebars (fight.def) come in: lifebars with a red trail for
// recent damage, power gauges with their level, the clock, round wins, and the round's announcements ("Round 1",
// "Fight", "KO", "Time over", who won).
class FightHud {
public:
	// Each game tick: the red trail catches up with the life bar once a combo is over.
	void update(const Character &p1, const Character &p2);
	void draw(const Match &match, const Character &p1, const Character &p2) const;
	void reset();

private:
	float redLife_[2] = { -1, -1 };
	int32_t redWait_[2] = { 0, 0 };
};

} // namespace m360
