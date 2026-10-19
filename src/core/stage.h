#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/bgdef.h"
#include "core/camera.h"
#include "core/ini.h"
#include "core/music.h"
#include "core/sff.h"
#include "core/sprite_renderer.h"

namespace m360 {

// A MUGEN stage: its .def's camera, player and bound settings, and its background elements.
// Translated from Ikemen-GO src/stage.go (loadStage, readBackGround, backGround.drawTransformed), without zoom, BG
// controllers, windows, sine motion, 3D models or video.
class Stage {
public:
	bool load(const std::string &defPath, std::string &error);
	// Logs what the stage's .def sets that isn't read yet.
	static void logUnread(const std::vector<IniSection> &sections, const std::string &defPath);
	// Moves the backgrounds one tick: velocity and animations.
	void update();
	// Draws the elements of one layer (-1 behind everything on a stage made for Ikemen, 0 behind the players, 1 in
	// front).
	void draw(int32_t layer, const View &view, const PalFX *globalPalFX = nullptr, float brightness = 1);
	// A new round: the backgrounds and their controllers start over, unless [StageInfo] resetbg = 0.
	void reset();
	// The view for a camera position: localcoord scaled to the screen's width, the cut split by cuthigh / cutlow.
	View view(float camX, float camY) const;

	std::string name;
	// [Info] ikemenversion is set: layer -1 is drawn (MUGEN doesn't draw it).
	bool ikemen = false;
	bool resetBg = true;
	int32_t localWidth = 320, localHeight = 240;
	// The fight's game space follows the stage's shape (Ikemen's applyFightAspect): 240 high and as wide as the stage's
	// aspect makes it, 320 for 4:3, 426.7 for 16:9. localScale converts the .def's stage units to it (Ikemen's stage
	// localscl): every position and distance below is in stage units, the views and the camera in game units.
	float gameWidth = 320, localScale = 1;
	// Where the ground is, in stage units from the top.
	int32_t zoffset = 200;
	// How far behind its leader a Simul partner starts ([PlayerInfo] partnerspacing).
	int32_t partnerspacing = 25;
	StageCamera camera;
	struct Start {
		int32_t x = 0, y = 0, facing = 1;
	} p[4];
	float leftbound = -1000, rightbound = 1000;
	int32_t screenleft = 15, screenright = 15;
	struct Shadow {
		int32_t intensity = 128;
		float yscale = 0.4f;
		// Fully shown below fadeBegin (closer to the ground), gone above fadeEnd; both 0: no fading.
		int32_t fadeEnd = 0, fadeBegin = 0;
		// color (0xRRGGBB, subtracted after the darkening; ignored in MUGEN 1.1 stages) and offset, in stage units.
		uint32_t colour = 0;
		float offsetX = 0, offsetY = 0;
	} shadow;
	// [Reflection] intensity: the fighters mirrored on the floor, 0 (none) to 255.
	int32_t reflection = 0;
	// [Reflection] color (the reflection mixed toward it by the intensity), yscale and offset.
	uint32_t reflectionColour = 0xFFFFFF;
	float reflectionYScale = 1, reflectionOffsetX = 0, reflectionOffsetY = 0;
	// [StageInfo] autoturn (fighters turn to face their opponent) and xscale / yscale (StageVar only).
	bool autoTurn = true;
	float scaleX = 1, scaleY = 1;
	// [Music] bgmusic and its settings, found as Ikemen's SearchFile finds it.
	MusicTrack music;

	const Sff &sff() const { return sff_; }
	const BgDef &backgrounds() const { return bgs_; }

private:
	Sff sff_;
	AnimationTable anims_;
	BgDef bgs_;
};

} // namespace m360
