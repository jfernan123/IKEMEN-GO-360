#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/camera.h"
#include "core/ini.h"
#include "core/sff.h"
#include "core/sprite_renderer.h"

namespace m360 {

// One [BG ...] element of a stage.
struct BgElement {
	enum class Type { Normal, Anim, Parallax, Dummy };
	std::string name;
	Type type = Type::Normal;
	int32_t layer = 0;
	float startX = 0, startY = 0;
	float deltaX = 1, deltaY = 1;
	// Normal and parallax: one sprite. Anim: an action from the stage's own [Begin Action] list.
	int32_t group = -1, number = 0;
	int32_t actionNo = -1;
	Animation anim;
	Trans trans = Trans::None;
	uint8_t srcAlpha = 255, dstAlpha = 0;
	bool mask = false;
	// 0 no tiling, 1 forever, n > 1 that many copies; spacing is from one copy's start to the next.
	int32_t tileX = 0, tileY = 0;
	int32_t spacingX = 0, spacingY = 0;
	float velocityX = 0, velocityY = 0;
	// Parallax: how fast the top and bottom rows scroll ("xscale"), or the top and bottom widths ("width").
	float rasterTop = 1, rasterBottom = 1;
	int32_t widthTop = 0, widthBottom = 0;
	float yscalestart = 100, yscaledelta = 0;
	bool positionlink = false;
	// Where velocity has moved it so far.
	float offsetX = 0, offsetY = 0;
};

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

	const Sff &sff() const { return sff_; }

private:
	void drawElement(const BgElement &bg, const View &view);

	Sff sff_;
	AnimationTable anims_;
	std::vector<BgElement> bgs_;
	std::unique_ptr<SpriteRenderer> renderer_;
};

} // namespace m360
