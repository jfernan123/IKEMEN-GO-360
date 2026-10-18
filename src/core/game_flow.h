#pragma once

#include <cstdint>
#include <vector>

namespace m360 {

// A single-player Arcade route. Ikemen builds this from select.def order values; until those are parsed, every real
// roster entry appears once in a shuffled route. Keeping it separate makes match progression deterministic and
// independent of the menu and renderer.
class ArcadeLadder {
public:
	void reset(int32_t fighters, int32_t player, uint32_t seed);
	// Advances after a win. False means the last match was won, or the route was empty.
	bool advance();
	bool active() const { return !opponents_.empty() && index_ < (int32_t)opponents_.size(); }
	int32_t opponent() const { return active() ? opponents_[(size_t)index_] : -1; }
	int32_t matchNo() const { return active() ? index_ + 1 : 0; }
	int32_t matchCount() const { return (int32_t)opponents_.size(); }
	const std::vector<int32_t> &opponents() const { return opponents_; }

private:
	std::vector<int32_t> opponents_;
	int32_t index_ = 0;
};

// Which member is active in a Turns team match. A lost fight removes that side's active member; the series ends
// when one side has no member left.
class TurnsSeries {
public:
	void reset(int32_t team1, int32_t team2);
	// Records a fight winner (0/1, -1 draw). True when another active pair remains.
	bool result(int winner);
	int32_t active(int side) const { return active_[side]; }
	int32_t size(int side) const { return size_[side]; }
	bool over() const { return active_[0] >= size_[0] || active_[1] >= size_[1]; }
	int winner() const
	{
		const bool out1 = active_[0] >= size_[0], out2 = active_[1] >= size_[1];
		return out1 && out2 ? -1 : out1 ? 1 : out2 ? 0 : -1;
	}

private:
	int32_t size_[2] = { 1, 1 };
	int32_t active_[2] = { 0, 0 };
};

// Ikemen's default Turns recovery (data/system.zss): a base percentage plus a bonus scaled by remaining time.
int32_t turnsRecovery(int32_t lifeMax, int32_t timeRemaining, int32_t roundTime, float basePercent = 0.0f,
	float bonusPercent = 27.5f);

} // namespace m360
