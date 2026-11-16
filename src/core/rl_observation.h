#pragma once

#include <cstdint>
#include <vector>

#include "platform/platform.h"

namespace m360 {

class Character;

// What a learned policy sees and how it presses buttons: shared by the PC's RL training bridge and the neural CPU
// (core/neural_policy), so the game shows a policy exactly what training did. Not from Ikemen: mugen360's own.

// A decision picks one of 9 directions and one of RL_BUTTONS button choices, held for RL_FRAMES frames. Choices 0-7
// are none, x, y, z, a, b, c, start (the original eight, so older policies keep their meaning); 8 on are every press
// of two or more of x y z a b c together (57), for hypers, throws and anything else a command asks to press at once.
constexpr int RL_DIRECTIONS = 9;
constexpr int RL_SINGLE_BUTTONS = 8;
constexpr int RL_BUTTONS = 65;
constexpr int RL_FRAMES = 3;
// Each side's "little guys" (helpers and projectiles): up to RL_OBJECTS, RL_OBJECT_VALUES numbers each.
constexpr int RL_OBJECTS = 4;
constexpr int RL_OBJECT_VALUES = 12;

// The buttons of a button choice as bits: x 1, y 2, z 4, a 8, b 16, c 32, start 64.
int rlButtonMask(int button);

// A decision as a controller. Directions are relative to the fighter: neutral, F, B, D, DF, DB, U, UF, UB; buttons
// as in RL_BUTTONS.
platform::Pad rlPad(int direction, int button, int facing);

// One fighter's view of the fight: its numbers, its opponent's, then the distance between them, whether the round is
// on, the time left, and the decision it made last. roundState and timeLeft come from the Match.
// Positions are in world space, so with mirror the view is reflected left to right (x, speeds, facing, boxes): player
// 2's view then looks like player 1's, and one policy plays both sides the same way.
std::vector<float> rlObservation(const Character &self, const Character &other, int32_t roundState, int32_t timeLeft,
	int previousDirection, int previousButton, bool mirror = false);

} // namespace m360
