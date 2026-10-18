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

} // namespace m360
