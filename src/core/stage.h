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
	// Moves the backgrounds one tick: velocity and animations.
	void update();
	// Draws the elements of one layer (0 behind the players, 1 in front).
	void draw(int32_t layer, const View &view);
	// The view for a camera position: localcoord scaled to the screen's width, the cut split by cuthigh / cutlow.
	View view(float camX, float camY) const;

	std::string name;
	int32_t localWidth = 320, localHeight = 240;
	// Where the ground is, in stage units from the top.
	int32_t zoffset = 200;
	StageCamera camera;
	struct Start {
		int32_t x = 0, y = 0, facing = 1;
	} p[2];
	float leftbound = -1000, rightbound = 1000;
	int32_t screenleft = 15, screenright = 15;
	struct Shadow {
		int32_t intensity = 128;
		float yscale = 0.4f;
		// Fully shown below fadeBegin (closer to the ground), gone above fadeEnd; both 0: no fading.
		int32_t fadeEnd = 0, fadeBegin = 0;
	} shadow;
	// [Reflection] intensity: the fighters mirrored on the floor, 0 (none) to 255.
	int32_t reflection = 0;
	// [Music] bgmusic and its settings, found as Ikemen's SearchFile finds it.
	MusicTrack music;

	const Sff &sff() const { return sff_; }

private:
	Sff sff_;
	AnimationTable anims_;
	BgDef bgs_;
};

} // namespace m360
