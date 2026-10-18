// Milestone 9: stages and rendering. 9a: the Ikemen release's 2D stages load, and a stage's localcoord sets the
// fight's game space as Ikemen's applyFightAspect / updateLocalScale / setGameAspect do.

#include <cmath>
#include <cstdio>
#include <string>

#include "core/character.h"
#include "core/stage.h"

using namespace m360;

namespace {
int passed, failed;

void expect(bool value, const char *name)
{
	if (value)
		passed++;
	else {
		failed++;
		std::printf("FAIL  9: %s\n", name);
	}
}

bool near(float a, float b)
{
	return std::fabs(a - b) < 0.01f;
}
} // namespace

int run9Tests()
{
	passed = failed = 0;
	std::string error;

	const char *stages[] = { "data/stages/kfm.def", "data/stages/stage0.def", "data/stages/stage0-720.def",
		"data/stages/stage1.def", "data/stages/stageZ.def", "data/stages/interactivestage.def" };
	for (const char *path : stages) {
		Stage stage;
		expect(stage.load(path, error), path);
	}

	// A 4:3 stage (320x240): game units are its own; pillarboxed at 960x720 on the 1280x720 screen.
	Stage kfm;
	kfm.load("data/stages/kfm.def", error);
	const View narrow = kfm.view(0, 0);
	expect(near(kfm.gameWidth, 320) && near(kfm.localScale, 1), "4:3 stage: game width 320, stage scale 1");
	expect(near(narrow.scale, 3) && near(narrow.width, 960) && near(narrow.originX, 160), "4:3 stage: pillarboxed");

	// A 16:9 stage at 1280x720: the game is 426.7 wide, one stage unit a third of a game unit, the whole screen used.
	Stage wide;
	wide.load("data/stages/stage0-720.def", error);
	const View full = wide.view(0, 0);
	expect(near(wide.gameWidth, 240.0f * 16 / 9) && near(wide.localScale, 1.0f / 3), "16:9 stage: game width 426.7");
	expect(near(full.scale, 3) && near(full.width, 1280) && near(full.originX, 0) && near(full.topCut, 0),
		"16:9 stage: the full screen, nothing cut");

	// A 320-localcoord character in a 16:9 fight is 4/3 as big in game units (Ikemen's setGameAspect).
	Character fighter;
	expect(fighter.load("data/chars/kfm/kfm.def", 1, error), "KFM loads");
	fighter.setGameWidth(kfm.gameWidth);
	expect(near(fighter.localScale, 1), "KFM on a 4:3 stage: scale 1");
	fighter.setGameWidth(wide.gameWidth);
	expect(near(fighter.localScale, 4.0f / 3), "KFM on a 16:9 stage: scale 4/3");

	std::printf("9 stages: %d passed, %d failed\n", passed, failed);
	return failed;
}
