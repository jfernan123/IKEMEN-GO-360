#pragma once

// --soak: every character of a game against the next, to find what breaks (13d).

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/game/game_config.h"
#include "core/game/settings.h"

namespace m360 {

std::string runSoak(const GameConfig &config, const Settings &settings, uint32_t frames, const std::string &path,
	size_t firstPair);

} // namespace m360
