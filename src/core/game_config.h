#pragma once

#include <string>

namespace m360 {

// The content mounted into the engine. Keeping these paths in data/mugen360.ini lets a PC or console package swap
// characters and stages without recompiling the executable.
struct GameConfig {
	std::string player1 = "data/chars/kfm/kfm.def";
	std::string player2 = "data/chars/kfm/kfm.def";
	std::string stage = "data/stages/kfm.def";

	// A missing file keeps the defaults. Values present under [Game] replace their corresponding defaults.
	bool load(const std::string &path, std::string &error);
};

} // namespace m360
