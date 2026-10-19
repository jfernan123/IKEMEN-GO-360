// Milestone 13: compatibility. 13a: a MUGEN game from its own folder (tests/game): Ikemen's SearchFile, mugen.cfg's
// motif, system.def's select.def, and select.def's characters, stage params and [ExtraStages]. 13b: --check's report.

#include <cstdio>
#include <string>

#include "core/compat.h"
#include "core/file.h"
#include "core/game_config.h"

using namespace m360;

namespace {
int passed, failed;

void expect(bool value, const char *name)
{
	if (value)
		passed++;
	else {
		failed++;
		std::printf("FAIL  13: %s\n", name);
	}
}
} // namespace

int run13Tests()
{
	passed = failed = 0;

	// Ikemen's SearchFile: under the default folder of a root, then as given.
	expect(searchFile("kfm/kfm.def", { "", "data/" }, { "chars/" }) == "data/chars/kfm/kfm.def",
		"a character found under data/chars/");
	expect(searchFile("kfm.sff", { "data/stages/kfm.def" }) == "data/stages/kfm.sff", "a file next to a .def root");
	expect(searchFile("  missing.def ; comment", { "" }) == "missing.def", "a missing file comes back cleaned");
	expect(GameConfig::findStage("stages/kfm") == "data/stages/kfm.def", "a stage without .def");
	expect(GameConfig::findCharacter("nobody").empty(), "a character not on disk");

	// The test game: its own select.def, mugen360's characters and stages through the fallback.
	GameConfig config;
	std::string error;
	const bool loaded = config.loadGame("tests/game", error);
	expect(loaded, "the test game loads");
	expect(gameRoot() == "tests/game/", "its folder is the file root");
	expect(config.motif == "data/ikemen1/system.def", "mugen.cfg's motif");
	expect(config.fight == "data/fight.def", "system.def's fight.def, found under data/");
	expect(config.characters.size() == 5 && config.characters[0] == "data/chars/kfm720/kfm720.def" &&
			config.characters[1] == "data/chars/kfm/kfm.def" && config.characters[2] == "chars/oddkfm/oddkfm.def" &&
			config.characters[3] == "dummyslot" && config.characters[4] == "randomselect",
		"select.def's roster, the missing character left out");
	expect(config.stages.size() == 3 && config.stages[0] == "data/stages/stageZ.def" &&
			config.stages[1] == "data/stages/kfm.def" && config.stages[2] == "data/stages/stage1.def",
		"[ExtraStages], then the characters' stages unless includestage = 0");
	expect(config.player1 == "data/chars/kfm720/kfm720.def" && config.player2 == "data/chars/kfm/kfm.def" &&
			config.stage == "data/stages/stageZ.def",
		"the first fight: the first two characters on the first stage");
	expect(config.arcadeMaxMatches == "6,1,1,0,0,0,0,0,0,0", "[Options] arcade.maxmatches");

	// 13b: the compatibility report. Odd KFM loads from KFM's files (found under data/ the way Ikemen finds a
	// character's files) and uses a controller and a trigger mugen360 doesn't know.
	const std::string report = compat::checkGame(config);
	const auto &features = compat::features();
	expect(features.count("controller\tmakerainbow") && features.at("controller\tmakerainbow").items.size() == 1,
		"the report finds Odd KFM's unknown controller");
	expect(features.count("trigger\trainbowcount") != 0, "the report finds Odd KFM's unknown trigger");
	expect(report.find("Characters: 3 of 3 load") != std::string::npos, "all three characters load");
	expect(report.find("char chars/oddkfm/oddkfm.def: loads") != std::string::npos, "Odd KFM loads from KFM's files");
	setGameRoot("");

	GameConfig missing;
	expect(!missing.loadGame("tests/no_such_game", error), "a folder with no game fails");
	setGameRoot("");

	std::printf("13 compatibility: %d passed, %d failed\n", passed, failed);
	return failed;
}
