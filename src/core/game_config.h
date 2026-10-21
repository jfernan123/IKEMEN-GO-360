#pragma once

#include <string>
#include <vector>

namespace m360 {

// The content mounted into the engine. Keeping these paths in data/mugen360.ini lets a PC or console package swap
// characters and stages without recompiling the executable.
struct GameConfig {
	struct Fighter {
		std::string def;
		int palette = 1;
	};
	std::string player1 = "data/chars/kfm/kfm.def";
	std::string player2 = "data/chars/kfm/kfm.def";
	std::string stage = "data/stages/kfm.def";
	// The screenpack's fight screen (lifebars, round calls); missing or unreadable: the plain built-in one.
	std::string fight = "data/fight.def";
	// The screenpack's system.def: the title screen and main menu. Missing or unreadable: the game starts at the
	// select screen.
	std::string motif = "data/ikemen1/system.def";
	// Palettes 1-12 for each player (the select screen's pick; 1 and 2 by default).
	int p1Palette = 1, p2Palette = 2;
	// The game mode, as Ikemen names it ("versus", "freebattle", "arcade", "training"), and each side's matches won in
	// a row so far: the fight screen's [WinCount].
	std::string mode = "versus";
	int winCount[2] = { 0, 0 };
	// The select screen's roster and stages, found as Ikemen finds select.def's entries ("kfm", "kfm/alt.def",
	// "stages/kfm.def"; under the game, chars/ or stages/, data/). Empty: no select screen, the fight above starts at
	// once.
	std::vector<std::string> characters;
	std::vector<std::string> stages;
	// Team modes. Empty means the single player1/player2 above; Turns uses the indexed active member on each side.
	std::vector<Fighter> team1, team2;

	// A MUGEN game's folder (13a: --game, or [Game] game = in data/mugen360.ini), empty for mugen360's own content,
	// and select.def's arcade.maxmatches as written (not used yet).
	std::string game;
	std::string arcadeMaxMatches;

	// A missing file keeps the defaults. Values present under [Game] replace their corresponding defaults.
	bool load(const std::string &path, std::string &error);
	// Plays a MUGEN game from its folder: its motif, fight.def, roster and stages replace the ones above, and its
	// folder becomes the file root (core/file's setGameRoot). False when it has no system.def, select.def or roster.
	bool loadGame(const std::string &folder, std::string &error);

	// A select.def character or stage entry as Ikemen finds it; empty when it isn't there.
	static std::string findCharacter(const std::string &entry);
	static std::string findStage(const std::string &entry);

private:
	void readSelect(const std::string &text, bool extraStagesSection);
};

} // namespace m360
