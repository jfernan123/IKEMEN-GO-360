#include "core/ai/smart_ai.h"

#include <algorithm>

#include "core/formats/air.h"
#include "core/fight/character.h"

namespace m360 {

SmartAi::SmartAi()
{
	for (Seen &s : seen_) {
		s.gap = 0;
		s.height = 0;
		s.approach = 0;
		s.stateType = 'S';
		s.moveType = 'I';
	}
	tick_ = 0;
	plan_ = Plan::Idle;
	planTicks_ = 0;
	cooldown_ = 0;
	crouchTicks_ = 0;
	forwardTicks_ = 0;
	comboStep_ = 0;
	cheatWindow_ = 0;
	ticksSinceThreat_ = 1000;
	guardLow_ = false;
	safeDistance_ = false;
	rolledAntiAir_ = false;
	rolledGuard_ = false;
	rolledWalkIn_ = false;
	held_.fill(0);
	random_ = 0x9E3779B9u;
}

float SmartAi::random01()
{
	// xorshift32: the same fight plays out the same way, on the 360 as on PC.
	random_ ^= random_ << 13;
	random_ ^= random_ >> 17;
	random_ ^= random_ << 5;
	return (float)(random_ >> 8) / 16777216.0f;
}

// How far a character's attacks reach: of the animations with attack boxes, each one's furthest box edge in front of
// the character, the 35th and 80th percentiles. Found once for each animation table.
const SmartAi::Reach &SmartAi::reachOf(const Character &c)
{
	const AnimationTable *anims = &c.anims();
	for (const Reach &r : reaches_)
		if (r.anims == anims)
			return r;
	std::vector<float> fronts;
	for (const auto &[no, anim] : anims->all()) {
		bool any = false;
		float front = 0;
		for (const AnimFrame &frame : anim.frames)
			for (const Clsn &box : frame.clsn1) {
				front = any ? std::max(front, std::max(box[0], box[2])) : std::max(box[0], box[2]);
				any = true;
			}
		if (any && front > 0)
			fronts.push_back(front);
	}
	Reach reach{ anims, 50, 60 };
	if (!fronts.empty()) {
		std::sort(fronts.begin(), fronts.end());
		reach.typical = fronts[fronts.size() * 35 / 100];
		reach.longer = fronts[fronts.size() * 4 / 5];
	}
	reaches_.push_back(reach);
	return reaches_.back();
}

void SmartAi::press(Button b)
{
	if (held_[b] == 0)
		held_[b] = 3;
}

void SmartAi::choosePlan(const Seen &look, float myReach, float theirTypical, float t)
{
	const float r = random01();
	// A block held before anything comes, as people do, since a quick attack lands before anyone could react to it:
	// only within the opponent's everyday reach and when it's pressing (walking in, or attacking lately). Holding back
	// otherwise just walks away, and a big character's reach covers most of the screen.
	const bool pressed = look.approach > 0.3f || ticksSinceThreat_ < 30;
	if (look.gap < theirTypical && pressed && look.stateType != 'A' && r < 0.35f + 0.45f * t) {
		// Low against a crouching opponent, and sometimes low anyway, guessing.
		plan_ = Plan::Guard;
		guardLow_ = look.stateType == 'C' || chance(0.3f);
		planTicks_ = 8 + (int32_t)(random01() * 12);
		return;
	}
	if (look.gap > myReach * 1.1f && look.gap < theirTypical * 1.1f) {
		// The opponent reaches further: a step in to get in range, or a wait for it to come.
		plan_ = r < 0.5f ? Plan::Approach : Plan::Idle;
		planTicks_ = 8 + (int32_t)(random01() * 10);
		return;
	}
	if (look.gap > myReach * 1.1f) {
		// Out of reach: mostly walk in, sometimes jump in from a middle distance, sometimes wait.
		if (look.gap < myReach * 3 && r < 0.08f + 0.1f * t)
			plan_ = Plan::JumpIn;
		else if (r < 0.75f + 0.15f * t)
			plan_ = Plan::Approach;
		else
			plan_ = Plan::Idle;
	} else {
		// In reach: stand and fight, now and then step back out to make the opponent come in.
		plan_ = r < 0.12f ? Plan::Retreat : Plan::Idle;
	}
	planTicks_ = plan_ == Plan::JumpIn ? 3 : 10 + (int32_t)(random01() * 30);
}

void SmartAi::attack(const Seen &look, float myReach, float t)
{
	// Right up close, sometimes a throw: forward and a medium punch, MUGEN characters' usual throw.
	if (look.gap < myReach * 0.15f && look.stateType != 'A' && chance(0.15f + 0.2f * t)) {
		forwardTicks_ = 3;
		press(Y);
		comboStep_ = 3;
	} else {
		// Lighter buttons close, stronger ones at the edge of reach; a low kick now and then against a standing
		// opponent, which a standing block doesn't stop.
		const bool far = look.gap > myReach * 0.6f;
		const float r = random01();
		if (look.stateType == 'S' && r < 0.2f) {
			crouchTicks_ = 8;
			press(far ? B : A);
			comboStep_ = far ? 2 : 1;
		} else if (r < (far ? 0.35f : 0.6f)) {
			press(r < (far ? 0.2f : 0.4f) ? X : A);
			comboStep_ = 1;
		} else if (r < (far ? 0.7f : 0.85f)) {
			press(r < (far ? 0.52f : 0.72f) ? Y : B);
			comboStep_ = 2;
		} else {
			press(r < 0.92f ? Z : C);
			comboStep_ = 3;
		}
	}
	// Recovery before the next try: a sharper player attacks again sooner.
	cooldown_ = 24 - (int32_t)(14 * t) + (int32_t)(random01() * 10);
	cheatWindow_ = 8;
	plan_ = Plan::Idle;
	planTicks_ = cooldown_;
}

void SmartAi::update(const Character &self, const Character &opponent, int32_t level, bool active)
{
	out_ = InputState{};
	if (!active) {
		tick_ = 0;
		plan_ = Plan::Idle;
		planTicks_ = cooldown_ = crouchTicks_ = forwardTicks_ = comboStep_ = cheatWindow_ = 0;
		ticksSinceThreat_ = 1000;
		rolledAntiAir_ = rolledGuard_ = rolledWalkIn_ = safeDistance_ = false;
		held_.fill(0);
		return;
	}
	// 0 at level 1, 1 at level 8.
	const float t = (float)(std::clamp(level, 1, 8) - 1) / 7.0f;
	const uint32_t reaction = (uint32_t)(24 - 18 * t);

	// What it sees now, and what it acts on: what it saw reaction ticks ago.
	float left, right, top, theirLeft, theirRight, theirTop;
	self.body(left, right, top);
	opponent.body(theirLeft, theirRight, theirTop);
	Seen now;
	now.gap = self.facing > 0 ? theirLeft - right : left - theirRight;
	now.height = -opponent.worldY();
	now.approach = -opponent.vx * (float)opponent.facing * opponent.localScale * (float)self.facing;
	now.stateType = opponent.stateType;
	now.moveType = opponent.moveType;
	tick_++;
	seen_[tick_ % HISTORY] = now;
	const Seen &look = seen_[(tick_ - std::min(reaction, tick_ - 1)) % HISTORY];

	// Reach past the front of the body, the same gap as now.gap (MUGEN's P2BodyDist): the attack boxes are measured
	// from the character's middle, so a wide body takes up some of them.
	const float myFront = self.facing > 0 ? right - self.worldX() : self.worldX() - left;
	const float theirFront = opponent.facing > 0 ? theirRight - opponent.worldX() : opponent.worldX() - theirLeft;
	const float myReach = std::max(10.0f, reachOf(self).typical * self.localScale - myFront);
	const float theirReach = std::max(10.0f, reachOf(opponent).longer * opponent.localScale - theirFront);
	const float theirTypical = std::max(10.0f, reachOf(opponent).typical * opponent.localScale - theirFront);
	if (cheatWindow_ > 0)
		cheatWindow_--;
	safeDistance_ = look.gap > theirReach * 1.5f && look.moveType != 'A';
	const bool canAct = self.ctrl;
	const bool inAir = self.stateType == 'A';
	if (cooldown_ > 0)
		cooldown_--;

	bool forward = false, back = false, up = false, down = false;

	// Blocking: when the opponent attacks within its reach, hold back (and down against a crouching attack), if this
	// level notices in time. Getting hit, it holds back too, to block whatever follows.
	const bool threat = look.moveType == 'A' && look.gap < theirReach + 15;
	ticksSinceThreat_ = threat ? 0 : std::min(ticksSinceThreat_ + 1, 1000);
	if (threat && !inAir && self.moveType != 'A') {
		if (!rolledGuard_) {
			rolledGuard_ = true;
			if (plan_ != Plan::Guard && chance(0.3f + 0.65f * t))
				plan_ = Plan::Guard;
		}
		if (plan_ == Plan::Guard) {
			guardLow_ = look.stateType == 'C';
			planTicks_ = std::max(planTicks_, 2);
		}
	} else if (rolledGuard_ && plan_ == Plan::Guard) {
		// The attack it blocked is over: punish it while the opponent recovers.
		plan_ = Plan::Idle;
		planTicks_ = 0;
		cooldown_ = 0;
	}
	if (!threat)
		rolledGuard_ = false;
	if (self.moveType == 'H') {
		plan_ = Plan::Guard;
		planTicks_ = std::max(planTicks_, 10);
	}

	// A hit landed: follow it with a stronger button while the opponent is still reeling (a chain combo).
	if (self.moveType == 'A' && opponent.moveType == 'H' && comboStep_ > 0 && comboStep_ < 3 && cooldown_ <= 18 &&
		chance(0.04f + 0.1f * t)) {
		comboStep_++;
		press(comboStep_ == 2 ? (chance(0.5f) ? Y : B) : (chance(0.5f) ? Z : C));
		cooldown_ = 24;
	}

	// Jumping in: attack on the way down once the opponent's in reach.
	if (inAir && self.moveType != 'A' && look.gap < myReach && self.vy > 0 && chance(0.2f + 0.3f * t)) {
		press(chance(0.5f) ? Y : B);
		comboStep_ = 2;
	}

	// Anti-air: the opponent jumps in close; decide once per jump whether to meet it with a crouching strong punch.
	if (look.stateType != 'A')
		rolledAntiAir_ = false;
	if (canAct && !rolledAntiAir_ && look.stateType == 'A' && look.height > 15 && look.gap < myReach + 40 &&
		look.approach >= 0) {
		rolledAntiAir_ = true;
		if (chance(0.1f + 0.6f * t)) {
			crouchTicks_ = 6;
			press(Z);
			comboStep_ = 3;
			cooldown_ = 30;
		}
	}

	// Attacking, when its moves reach and it isn't blocking. A sloppy level lets more chances go by.
	if (canAct && plan_ != Plan::Guard && cooldown_ <= 0 && look.gap <= myReach && look.stateType != 'A' &&
		chance(0.06f + 0.14f * t))
		attack(look, myReach, t);

	// Moving, when it has control and nothing more urgent.
	// Walking in from afar, it stops to think again on reaching the opponent's range instead of strolling into its
	// attacks; and it thinks again once as the opponent walks into range.
	if (plan_ == Plan::Approach && planTicks_ > 18 && look.gap < std::max(myReach, theirTypical) * 1.1f)
		planTicks_ = 0;
	const bool walkingIn = look.approach > 0.5f && look.gap < theirTypical * 1.5f;
	if (walkingIn && !rolledWalkIn_ && plan_ != Plan::Guard)
		planTicks_ = 0;
	rolledWalkIn_ = walkingIn;
	if (canAct && --planTicks_ <= 0)
		choosePlan(look, myReach, theirTypical, t);
	switch (plan_) {
	// Walking in to half its reach: close enough for its own attacks, and for the distance checks of a character's own
	// AI, which mostly attack from close in.
	case Plan::Approach: forward = look.gap > myReach * 0.5f; break;
	case Plan::Retreat: back = true; break;
	case Plan::JumpIn:
		up = forward = true;
		break;
	case Plan::Guard:
		back = true;
		down = guardLow_;
		break;
	case Plan::Idle: break;
	}
	if (crouchTicks_ > 0) {
		crouchTicks_--;
		down = true;
		forward = back = false;
	}
	if (forwardTicks_ > 0) {
		forwardTicks_--;
		forward = true;
		back = false;
	}

	out_.up = up;
	out_.down = down;
	const bool faceRight = self.facing > 0;
	out_.right = faceRight ? forward : back;
	out_.left = faceRight ? back : forward;
	// Buttons stay down while their count is above 1; at 1 they're let go for a tick.
	bool *buttons[BUTTONS] = { &out_.a, &out_.b, &out_.c, &out_.x, &out_.y, &out_.z, &out_.s };
	for (int i = 0; i < BUTTONS; i++) {
		*buttons[i] = held_[i] > 1;
		if (held_[i] > 0)
			held_[i]--;
	}
}

} // namespace m360
