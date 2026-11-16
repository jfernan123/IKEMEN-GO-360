#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/script/input.h"

namespace m360 {

class AnimationTable;
class Character;

// A computer player that plays the way a person does: walks in, attacks when its moves reach, blocks high and low,
// knocks jumpers out of the air, follows a hit with a bigger one, throws up close and backs off now and then.
// mugen360's own, not from Ikemen (which only has MUGEN's random AiInput, core/ai/ai). It only presses buttons, so the
// character's own states decide what each press does, and a character with AI of its own still switches it on through
// the command cheat (Character::turnAndInput).
// It sees the opponent a few ticks late (its reaction time) and doesn't always take its chances: both depend on the
// AI level (1-8), so level 1 is slow and sloppy and level 8 sharp.
class SmartAi {
public:
	SmartAi();
	void seed(uint32_t s) { random_ = s ? s : 1; }
	// One tick. active is false outside the fight (intros, win poses), when it lets go of everything and forgets
	// what it saw.
	void update(const Character &self, const Character &opponent, int32_t level, bool active);
	InputState state() const { return out_; }
	// Whether the command cheat may pick any command now: for a few ticks after it decides to attack and while the
	// opponent is well out of reach, so the specials it brings out don't come while it should block.
	bool cheatAny() const { return cheatWindow_ > 0 || safeDistance_; }
	// Each round's first seconds, when the cheat may pick the character's AI switches (Character::aiSwitches_):
	// MUGEN clears the switch each round, and this turns a character's own AI on at once without random specials.
	bool roundStart() const { return tick_ < ROUND_START_CHEAT; }

private:
	// What it saw of the opponent on one tick, in game units.
	struct Seen {
		float gap;      // between the two bodies, ahead of self; below 0 when they overlap
		float height;   // the opponent's height above the ground
		float approach; // the opponent's speed toward self
		char stateType, moveType;
	};
	enum class Plan : uint8_t { Idle, Approach, Retreat, JumpIn, Guard };
	// The buttons, in InputState's order.
	enum Button { A, B, C, X, Y, Z, S, BUTTONS };
	// How far a character's attacks reach in front of it, in its own units, by animation table: a typical normal's
	// (the 35th percentile: specials and supers with long boxes would otherwise stretch it), and its longer moves'
	// (the 80th percentile), which is how far its opponent should keep away.
	struct Reach {
		const AnimationTable *anims;
		float typical, longer;
	};

	float random01();
	bool chance(float p) { return random01() < p; }
	const Reach &reachOf(const Character &c);
	// Taps a button: down for two ticks, then up for one, so the next press counts as a new one.
	void press(Button b);
	// Picks the next movement plan when the current one runs out.
	void choosePlan(const Seen &look, float myReach, float theirTypical, float t);
	// Presses an attack button for where the opponent is, and how long until it may start the next.
	void attack(const Seen &look, float myReach, float t);

	static constexpr uint32_t HISTORY = 32;
	// Ticks at the start of each round when the command cheat runs freely (6 seconds).
	static constexpr uint32_t ROUND_START_CHEAT = 360;
	std::array<Seen, HISTORY> seen_;
	uint32_t tick_;
	std::vector<Reach> reaches_;
	InputState out_;
	Plan plan_;
	int32_t planTicks_;
	int32_t cooldown_;    // ticks before it may start another attack
	int32_t crouchTicks_; // ticks it keeps holding down for a low attack
	int32_t forwardTicks_; // ticks it holds forward for a throw
	int32_t comboStep_;   // 1 light, 2 medium, 3 strong: the last button of the current string
	int32_t cheatWindow_; // ticks the command cheat may still fire
	int32_t ticksSinceThreat_; // since it last saw the opponent attack within reach
	bool guardLow_;
	bool safeDistance_;   // the opponent's well out of its reach
	bool rolledAntiAir_;  // decided already about this jump of the opponent's
	bool rolledGuard_;    // decided already about this attack of the opponent's
	bool rolledWalkIn_;   // thought again already as the opponent walks in
	std::array<int32_t, BUTTONS> held_;
	// Members are set in the constructor, not with defaults here: runs of constant defaults can become rldic, which
	// Xenia skips.
	uint32_t random_;
};

} // namespace m360
