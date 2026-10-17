#include "core/ai.h"

namespace m360 {

int32_t AiInput::rand(int32_t lo, int32_t hi)
{
	// xorshift32: the same game plays out the same way, on the 360 as on PC.
	random_ ^= random_ << 13;
	random_ ^= random_ >> 17;
	random_ ^= random_ << 5;
	return lo + (int32_t)(random_ % (uint32_t)(hi - lo + 1));
}

bool AiInput::jam(int32_t &t, int32_t chance, int32_t time)
{
	t--;
	if (t <= 0) {
		if (rand(1, chance) == 1) {
			t = rand(1, time);
			return true;
		}
		t = 0;
	}
	return false;
}

void AiInput::update(float level, bool active)
{
	if (!active) {
		dirt_ = at_ = bt_ = ct_ = xt_ = yt_ = zt_ = st_ = 0;
		return;
	}
	// A direction now and then, held up to a second.
	if (jam(dirt_, 15, 60))
		dir_ = rand(0, 7);
	// Buttons: at level 8 each has a 1 in 525 chance a tick, at level 1 1 in 1076; held up to half a second.
	const int32_t chance = (int32_t)((-11.25f * level + 165) * 7);
	for (int32_t *t : { &at_, &bt_, &ct_, &xt_, &yt_, &zt_ })
		jam(*t, chance, 30);
	// Start (a taunt) far less often.
	jam(st_, 3600, 30);
}

InputState AiInput::state() const
{
	InputState in;
	const bool held = dirt_ != 0;
	in.up = held && (dir_ == 7 || dir_ == 0 || dir_ == 1);
	in.down = held && (dir_ == 3 || dir_ == 4 || dir_ == 5);
	in.left = held && (dir_ == 5 || dir_ == 6 || dir_ == 7);
	in.right = held && (dir_ == 1 || dir_ == 2 || dir_ == 3);
	in.a = at_ != 0;
	in.b = bt_ != 0;
	in.c = ct_ != 0;
	in.x = xt_ != 0;
	in.y = yt_ != 0;
	in.z = zt_ != 0;
	in.s = st_ != 0;
	return in;
}

} // namespace m360
