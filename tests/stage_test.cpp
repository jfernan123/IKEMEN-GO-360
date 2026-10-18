// Milestone 9: stages and rendering. 9a: the Ikemen release's 2D stages load, and a stage's localcoord sets the
// fight's game space as Ikemen's applyFightAspect / updateLocalScale / setGameAspect do. 9b: Ikemen's camera (tension,
// easing, zoom between zoomout and zoomin, the players' screen limits).

#include <cmath>
#include <cstdio>
#include <string>

#include "core/camera.h"
#include "core/character.h"
#include "core/layout.h"
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

	// The camera on a stage without zoom (KFM's): players inside the tension margins don't move it.
	StageCamera still;
	still.boundleft = -150;
	still.boundright = 150;
	still.tension = 60;
	CameraSpace space;
	space.localScale = 1;
	space.gameWidth = 320;
	space.gameHeight = 240;
	space.screenLeft = space.screenRight = 15;
	space.zoffset = 200;
	space.localHeight = 240;
	space.heightScale = 3;
	Camera camera;
	camera.reset(still, space);
	const auto step = [&](float p1, float p2) {
		camera.resetTracking();
		camera.track(p1, p1, 0, 0);
		camera.track(p2, p2, 0, 0);
		camera.action();
	};
	step(-70, 70);
	expect(camera.x == 0 && camera.scale == 1, "players inside the margins: the camera stays");
	expect(near(camera.xmin(), -145) && near(camera.xmax(), 145), "the players' limits: the screen less screenleft / right");
	// A player past the right margin pulls the camera, eased: a step part of the way, then all of it.
	step(0, 150);
	const float first = camera.x;
	for (int i = 0; i < 200; i++)
		step(0, 150);
	expect(first > 0 && first < 50 && near(camera.x, 50), "the camera eases until the player is 60 from the edge");
	expect(camera.x * 4 == std::floor(camera.x * 4), "without zoom, x is kept to quarter units");

	// Training Stage Z's zoom: players far apart zoom out to 0.75, close together back in to 1.
	Stage zoomStage;
	zoomStage.load("data/stages/stageZ.def", error);
	expect(zoomStage.camera.zoomout == 0.75f && zoomStage.camera.ytensionenable, "stageZ zooms out to 0.75, vertical tension");
	camera.reset(zoomStage.camera, space);
	for (int i = 0; i < 300; i++)
		step(-183, 183);
	expect(near(camera.scale, 0.75f) && camera.zoomEnabled(), "far apart: zoomed out to zoomout");
	expect(near(camera.xmin(), -160 / 0.75f + 15) && near(camera.xmax(), 160 / 0.75f - 15), "zoomed out: the limits widen");
	for (int i = 0; i < 300; i++)
		step(-40, 40);
	expect(near(camera.scale, 1), "close together: zoomed back in to zoomin");

	// 9c background data used by real stages and screenpacks.
	Stage controlled;
	expect(controlled.load("tests/fixtures/stage9c.def", error), "9c synthetic stage loads");
	const BgDef &backgrounds = controlled.backgrounds();
	expect(backgrounds.size() == 2 && backgrounds.controllerCount() == 4,
		"window, width floor and four BG controllers load");
	const BgElement &moving = backgrounds.element(0);
	expect(moving.hasWindow && moving.window[0] == 10 && moving.window[2] == 109 &&
		near(moving.windowDeltaX, .5f) && near(moving.windowDeltaY, .25f), "window and windowdelta parse");
	ClipRect clip;
	expect(backgrounds.clipRect(0, controlled.view(0, 0), clip) &&
		clip.x == 190 && clip.y == 60 && clip.width == 300 && clip.height == 150,
		"stage window converts to the expected screen rectangle");
	const BgElement &floor = backgrounds.element(1);
	expect(floor.widthTop == 100 && floor.widthBottom == 200, "parallax width parses");
	const BgTrapezoid edges = BgDef::parallaxEdges(640, 640, 100, 50, 1, 2, 3);
	expect(near(edges.topLeft, 490) && near(edges.topRight, 790) &&
		near(edges.bottomLeft, 340) && near(edges.bottomRight, 940),
		"parallax top and bottom edges converge about screen centre");

	// At t0 it moves by 1. At t1 Enable=0 freezes it through t2. At t3 Enable=1 and VelSet=3 move it again.
	controlled.update();
	expect(!backgrounds.element(0).visible && near(backgrounds.element(0).offsetX, 1),
		"BGCtrl Visible runs at time zero");
	controlled.update();
	controlled.update();
	expect(!backgrounds.element(0).enabled && near(backgrounds.element(0).offsetX, 1),
		"BGCtrl Enable freezes movement while disabled");
	controlled.update();
	expect(backgrounds.element(0).enabled && near(backgrounds.element(0).velocityX, 3) &&
		near(backgrounds.element(0).offsetX, 4), "BGCtrl resumes and VelSet applies on its timeline");
	controlled.update();
	controlled.update();
	controlled.update();
	expect(near(backgrounds.element(0).offsetX, 13), "BGCtrl looptime repeats the time-zero controller");
	controlled.reset();
	expect(backgrounds.time() == 0 && backgrounds.element(0).visible && backgrounds.element(0).enabled &&
		near(backgrounds.element(0).offsetX, 0) && near(backgrounds.element(0).velocityX, 1),
		"stage reset restores BG motion and controller state");

	std::printf("9a-9c stages and backgrounds: %d passed, %d failed\n", passed, failed);
	return failed;
}
