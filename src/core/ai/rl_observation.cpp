#include "core/ai/rl_observation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

#include "core/fight/character.h"
#include "core/fight/match.h"
#include "core/fight/projectile.h"

namespace m360 {

int rlButtonMask(int button)
{
	if (button <= 0 || button >= RL_BUTTONS)
		return 0;
	if (button < RL_SINGLE_BUTTONS)
		return 1 << (button - 1);
	// The presses of two or more of the six attack buttons, in increasing bit order.
	int index = RL_SINGLE_BUTTONS;
	for (int mask = 3; mask < 64; mask++) {
		if ((mask & (mask - 1)) == 0)
			continue;
		if (index++ == button)
			return mask;
	}
	return 0;
}

// Inputs rather than state changes: a learned policy plays through the same CMD parser as a person, so it can play
// any MUGEN character without character code.
platform::Pad rlPad(int direction, int button, int facing)
{
	const int mask = rlButtonMask(button);
	platform::Pad pad;
	pad.connected = true;
	const bool forward = direction == 1 || direction == 4 || direction == 7;
	const bool back = direction == 2 || direction == 5 || direction == 8;
	pad.right = facing >= 0 ? forward : back;
	pad.left = facing >= 0 ? back : forward;
	pad.down = direction == 3 || direction == 4 || direction == 5;
	pad.up = direction == 6 || direction == 7 || direction == 8;
	pad.x = (mask & 1) != 0;
	pad.y = (mask & 2) != 0;
	pad.rb = (mask & 4) != 0;
	pad.a = (mask & 8) != 0;
	pad.b = (mask & 16) != 0;
	pad.rt = (mask & 32) != 0;
	pad.start = (mask & 64) != 0;
	return pad;
}

namespace {

float rlType(char value, char a, char b, char c)
{
	return value == a ? -1.0f : value == b ? 0.0f : value == c ? 1.0f : 0.0f;
}

// Four exact boxes of each kind are enough for normal MUGEN frames. The mask keeps padding distinguishable from a
// real zero-sized box. Coordinates are in world space, so the policy sees actual reach across localcoord scales.
// sign is -1 for a mirrored view: x values flip and the box's left and right swap.
void appendRlBoxes(std::vector<float> &out, const Character &c, int kind, float sign)
{
	const AnimFrame *frame = c.anim.curFrame();
	const std::vector<Clsn> *boxes = frame ? (kind == 1 ? &frame->clsn1 : &frame->clsn2) : nullptr;
	out.push_back(std::min(boxes ? (float)boxes->size() / 4.0f : 0.0f, 1.0f));
	for (size_t i = 0; i < 4; i++) {
		if (!boxes || i >= boxes->size()) {
			out.insert(out.end(), { 0, 0, 0, 0, 0 });
			continue;
		}
		const Clsn &box = (*boxes)[i];
		float left = box[0] * c.localScale, right = box[2] * c.localScale;
		if (c.facing < 0) {
			left = -box[2] * c.localScale;
			right = -box[0] * c.localScale;
		}
		const float x1 = sign * (c.worldX() + std::min(left, right)), x2 = sign * (c.worldX() + std::max(left, right));
		out.push_back(1.0f);
		out.push_back(std::clamp(std::min(x1, x2) / 640.0f, -1.0f, 1.0f));
		out.push_back(std::clamp((c.worldY() + std::min(box[1], box[3]) * c.localScale) / 240.0f, -1.0f, 1.0f));
		out.push_back(std::clamp(std::max(x1, x2) / 640.0f, -1.0f, 1.0f));
		out.push_back(std::clamp((c.worldY() + std::max(box[1], box[3]) * c.localScale) / 240.0f, -1.0f, 1.0f));
	}
}

// A fighter's "little guys": its helpers (and theirs) and every live projectile any of them owns. Many characters
// attack mostly through these, so the policy sees up to RL_OBJECTS of each side's: the attacking ones first, then the
// ones nearest the observing fighter. Each is RL_OBJECT_VALUES numbers: present, projectile or helper, world position,
// velocity, facing, whether it has an attack box now, and the box around all its attack boxes (zeros without one).
// sign is -1 for a mirrored view.
void appendRlObjects(std::vector<float> &out, const Character &owner, const Character &observer, float sign)
{
	struct Object {
		bool projectile, attacking;
		float x, y, vx, vy, facing, box[4];
	};
	std::vector<Object> objects;
	// Around every attack box of one animation frame, in world space.
	auto attackBox = [](const AnimFrame *frame, float x, float y, float scaleX, float scaleY, int facing, float box[4]) {
		if (!frame || frame->clsn1.empty())
			return false;
		box[0] = box[1] = 1e9f;
		box[2] = box[3] = -1e9f;
		for (const Clsn &c : frame->clsn1) {
			const float left = facing < 0 ? -c[2] * scaleX : c[0] * scaleX;
			const float right = facing < 0 ? -c[0] * scaleX : c[2] * scaleX;
			box[0] = std::min(box[0], x + std::min(left, right));
			box[2] = std::max(box[2], x + std::max(left, right));
			box[1] = std::min(box[1], y + std::min(c[1], c[3]) * scaleY);
			box[3] = std::max(box[3], y + std::max(c[1], c[3]) * scaleY);
		}
		return true;
	};
	auto addProjectiles = [&](const Character &c) {
		for (const Projectile &p : c.projectiles()) {
			if (!p.active || p.removing)
				continue;
			Object o{};
			o.projectile = true;
			o.x = p.x * p.localScale;
			o.y = p.y * p.localScale;
			o.vx = p.vx;
			o.vy = p.vy;
			o.facing = (float)p.facing;
			o.attacking = attackBox(p.anim.curFrame(), o.x, o.y, p.localScale * p.scaleX, p.localScale * p.scaleY,
				p.facing, o.box);
			objects.push_back(o);
		}
	};
	std::function<void(const Character &)> addHelpers = [&](const Character &c) {
		for (const auto &h : c.helpers()) {
			if (!h || h->destroyed())
				continue;
			Object o{};
			o.projectile = false;
			o.x = h->worldX();
			o.y = h->worldY();
			o.vx = h->vx;
			o.vy = h->vy;
			o.facing = (float)h->facing;
			o.attacking = attackBox(h->anim.curFrame(), o.x, o.y, h->localScale, h->localScale, h->facing, o.box);
			objects.push_back(o);
			addProjectiles(*h);
			addHelpers(*h);
		}
	};
	addProjectiles(owner);
	addHelpers(owner);
	const float from = observer.worldX();
	std::sort(objects.begin(), objects.end(), [from](const Object &a, const Object &b) {
		if (a.attacking != b.attacking)
			return a.attacking;
		return std::abs(a.x - from) < std::abs(b.x - from);
	});
	const auto clamp = [](float value, float scale) { return std::clamp(value / scale, -1.0f, 1.0f); };
	for (size_t i = 0; i < (size_t)RL_OBJECTS; i++) {
		if (i >= objects.size()) {
			out.insert(out.end(), RL_OBJECT_VALUES, 0.0f);
			continue;
		}
		const Object &o = objects[i];
		out.push_back(1.0f);
		out.push_back(o.projectile ? 1.0f : 0.0f);
		out.push_back(clamp(sign * o.x, 640.0f));
		out.push_back(clamp(o.y, 240.0f));
		out.push_back(clamp(sign * o.vx, 20.0f));
		out.push_back(clamp(o.vy, 20.0f));
		out.push_back(sign * o.facing);
		out.push_back(o.attacking ? 1.0f : 0.0f);
		// Mirrored, the box's left is the old right.
		const float box[4] = { std::min(sign * o.box[0], sign * o.box[2]), o.box[1],
			std::max(sign * o.box[0], sign * o.box[2]), o.box[3] };
		for (int k = 0; k < 4; k++)
			out.push_back(o.attacking ? clamp(box[k], k % 2 == 0 ? 640.0f : 240.0f) : 0.0f);
	}
}

} // namespace

std::vector<float> rlObservation(const Character &self, const Character &other, int32_t roundState, int32_t timeLeft,
	int previousDirection, int previousButton, bool mirror)
{
	const auto clamp = [](float value, float scale) { return std::clamp(value / scale, -1.0f, 1.0f); };
	// Directions are already relative to facing (rlPad), so a mirrored view only flips what's left or right.
	const float sign = mirror ? -1.0f : 1.0f;
	std::vector<float> out;
	out.reserve(121 + 2 * RL_OBJECTS * RL_OBJECT_VALUES);
	for (const Character *c : { &self, &other }) {
		out.push_back((float)c->life / (float)std::max(1, c->lifeMax()));
		out.push_back((float)c->power / (float)std::max(1, c->powerMax()));
		out.push_back(clamp(sign * c->worldX(), 640.0f));
		out.push_back(clamp(c->worldY(), 240.0f));
		out.push_back(clamp(sign * c->vx, 20.0f));
		out.push_back(clamp(c->vy, 20.0f));
		out.push_back(sign * (float)c->facing);
		out.push_back(c->ctrl ? 1.0f : 0.0f);
		out.push_back(c->guarding() ? 1.0f : 0.0f);
		out.push_back(c->inGuardDist() ? 1.0f : 0.0f);
		out.push_back(clamp((float)c->hitPauseTime(), 30.0f));
		out.push_back(clamp((float)c->moveContact(), 30.0f));
		out.push_back(clamp((float)c->stateNo, 6000.0f));
		out.push_back(clamp((float)c->stateTime, 300.0f));
		out.push_back(rlType(c->stateType, 'S', 'C', 'A'));
		out.push_back(rlType(c->moveType, 'I', 'A', 'H'));
		appendRlBoxes(out, *c, 1, sign);
		appendRlBoxes(out, *c, 2, sign);
		// Right after each fighter's own numbers, so the opponent's block still starts at (size - 5) / 2.
		appendRlObjects(out, *c, self, sign);
	}
	out.push_back(clamp(sign * (other.worldX() - self.worldX()), 640.0f));
	out.push_back(roundState == 2 ? 1.0f : 0.0f);
	out.push_back(clamp((float)timeLeft, (float)Match::ROUND_TIME));
	out.push_back((float)previousDirection / (float)(RL_DIRECTIONS - 1));
	// The original eight keep the scale they were trained with (choice / 7); a press of several buttons reads as its
	// first button, and the policy's memory knows the rest.
	int shown = previousButton;
	if (shown >= RL_SINGLE_BUTTONS) {
		const int mask = rlButtonMask(shown);
		shown = 1;
		while (!(mask & (1 << (shown - 1))))
			shown++;
	}
	out.push_back((float)shown / (float)(RL_SINGLE_BUTTONS - 1));
	return out;
}

} // namespace m360
