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

// One background element ("[BG Floor]" in a stage, "[TitleBG Clouds]" in a screenpack).
struct BgElement {
	enum class Type { Normal, Anim, Parallax, Dummy };
	std::string name;
	Type type = Type::Normal;
	int32_t layer = 0;
	float startX = 0, startY = 0;
	float deltaX = 1, deltaY = 1;
	// Normal and parallax: one sprite. Anim: an action from the def's own [Begin Action] list.
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

// A set of background elements: a stage's, or a screenpack screen's. The elements are the sections named
// "<prefix> ..." (Ikemen's loadBGDef / readBackGround / backGround.drawTransformed, without zoom, BG controllers,
// windows, sine motion or 3D models).
class BgDef {
public:
	// Reads the sections named "<prefix> something" (prefix lowercase: "bg" for a stage, "titlebg" for a title
	// screen). Sprites come from sff, "type = anim" elements from anims; both must outlive this.
	void load(const std::vector<IniSection> &sections, const std::string &prefix, const Sff &sff,
		const AnimationTable &anims, const std::string &defPath);
	// Moves the elements one tick: velocity and animations.
	void update();
	// Back to where they started.
	void reset();
	// Draws the elements of one layer (0 behind the players, 1 in front).
	void draw(int32_t layer, const View &view);
	size_t size() const { return bgs_.size(); }

private:
	void drawElement(const BgElement &bg, const View &view);

	std::vector<BgElement> bgs_;
	const Sff *sff_ = nullptr;
	std::unique_ptr<SpriteRenderer> renderer_;
};

} // namespace m360
