#pragma once

// The PC-only bridge to the reinforcement-learning trainer in rl/: --rl-server (one fight, text protocol) and
// --rl-pool (many fights in one process, binary protocol).

#include <string>

#include "core/game/game_config.h"
#include "core/game/settings.h"

namespace m360 {

#ifndef M360_X360
int runRlServer(const GameConfig &config, Settings &settings, const std::string &recordDir, bool show, bool trace);
int runRlPool(const GameConfig &config, Settings &settings, int count, int threads);
#endif

} // namespace m360
