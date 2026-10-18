#pragma once

#include <string>
#include <vector>

namespace m360 {

// The content mounted into the engine. Keeping these paths in data/mugen360.ini lets a PC or console package swap
// characters and stages without recompiling the executable.
struct GameConfig {
	std::string player1 = "data/chars/kfm/kfm.def";
	std::string player2 = "data/chars/kfm/kfm.def";
	std::string stage = "data/stages/kfm.def";
	// Palettes 1-12 for each player (the select screen's pick; 1 and 2 by default).
	int p1Palette = 1, p2Palette = 2;
	// The select screen's roster and stages ([Characters] and [Stages], one per line as in MUGEN's select.def: "kfm",
	// "kfm/kfm.def" or a path under data/). Empty: no select screen, the fight above starts at once.
	std::vector<std::string> characters;
	std::vector<std::string> stages;

	// A missing file keeps the defaults. Values present under [Game] replace their corresponding defaults.
	bool load(const std::string &path, std::string &error);
};

} // namespace m360
