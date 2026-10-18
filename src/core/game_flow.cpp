#include "core/game_flow.h"

#include <algorithm>

namespace m360 {

void ArcadeLadder::reset(int32_t fighters, int32_t player, uint32_t seed)
{
	opponents_.clear();
	index_ = 0;
	for (int32_t i = 0; i < fighters; i++)
		opponents_.push_back(i);
	// Ikemen randomizes a generated order. Xorshift keeps scripted runs identical on PC and big-endian Xbox 360.
	for (int32_t i = fighters - 1; i > 0; i--) {
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		std::swap(opponents_[(size_t)i], opponents_[(size_t)(seed % (uint32_t)(i + 1))]);
	}
	// When another character exists, don't open the route with a mirror match.
	if (fighters > 1 && opponents_[0] == player) {
		const auto other = std::find_if(opponents_.begin() + 1, opponents_.end(),
			[player](int32_t value) { return value != player; });
		if (other != opponents_.end())
			std::swap(opponents_[0], *other);
	}
}

bool ArcadeLadder::advance()
{
	if (!active())
		return false;
	index_++;
	return active();
}

} // namespace m360
