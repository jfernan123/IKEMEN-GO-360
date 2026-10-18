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
	// "sin.x" / "sin.y = radius, period, phase": a swing of radius over period ticks (Ikemen's bgAction).
	float sinRadius[2] = { 0, 0 }, sinOffset[2] = { 0, 0 };
	int32_t sinPeriod[2] = { 0, 0 }, sinTime[2] = { 0, 0 }, sinStart[2] = { 0, 0 };
	// "scalestart" and "scaledelta" (per unit the camera scrolls it).
	float scaleStartX = 1, scaleStartY = 1, scaleDeltaX = 0, scaleDeltaY = 0;
	// "window" (x1, y1, x2, y2, inclusive, from the top left) or "maskwindow" (exclusive, from the top centre), and
	// "windowdelta": how far the window scrolls with the camera.
	bool hasWindow = false, maskWindow = false;
	int32_t window[4] = { 0, 0, 0, 0 };
	float windowDeltaX = 0, windowDeltaY = 0;
	// "id", for BG controllers, and what they switch: a hidden element still moves, a disabled one stops too.
	int32_t id = 0;
	bool visible = true, enabled = true;
	// What a BGCtrl PosSet / VelSet / SinX started from, for reset.
	float startVelocityX = 0, startVelocityY = 0, startSinRadius[2] = { 0, 0 };
	int32_t startSinPeriod[2] = { 0, 0 };
};

// A background controller ("[BGCtrl ...]" under a "[BGCtrlDef ...]"): over a time range, it acts on the elements
// with its ctrlid (Ikemen's bgCtrl: read, bgCtrlAction, runBgCtrl). PalFX comes with 9d; RemapPal and ShaderSet aren't
// supported.
struct BgCtrl {
	enum class Type { Null, Anim, Visible, Enable, PosSet, PosAdd, SinX, SinY, VelSet, VelAdd };
	Type type = Type::Null;
	// "time = start, end, loop": active from start to end, again every loop ticks (-1 never).
	int32_t start = 0, end = 0, loop = -1;
	bool hasX = false, hasY = false;
	float x = 0, y = 0;
	int32_t value[3] = { 0, 0, 0 };
	std::vector<size_t> targets;
};

struct ClipRect;

// A set of background elements: a stage's, or a screenpack screen's. The elements are the sections named
// "<prefix> ..." (Ikemen's loadBGDef / readBackGround / backGround.drawTransformed), and for a stage its BG
// controllers. Without 3D models, video, zoomdelta and the other Ikemen-only zoom keys.
class BgDef {
public:
	// Reads the sections named "<prefix> something" (prefix lowercase: "bg" for a stage, "titlebg" for a title
	// screen). Sprites come from sff, "type = anim" elements from anims; both must outlive this.
	void load(const std::vector<IniSection> &sections, const std::string &prefix, const Sff &sff,
		const AnimationTable &anims, const std::string &defPath);
	// A stage's elements: their windows follow the camera; and the stage's controllers.
	void setStage(bool stage) { stage_ = stage; }
	// Moves the elements one tick: the controllers, velocity, sine motion and animations (Ikemen's Stage.action
	// and tick).
	void update();
	// Ticks since the round started, which the controllers' times count (Ikemen's stageTime).
	int32_t time() const { return time_; }
	size_t controllerCount() const { return controllers_.size(); }
	// Back to where they started.
	void reset();
	// Draws the elements of one layer (0 behind the players, 1 in front).
	void draw(int32_t layer, const View &view);
	size_t size() const { return bgs_.size(); }
	const BgElement &element(size_t i) const { return bgs_[i]; }

private:
	void drawElement(const BgElement &bg, const View &view);
	bool windowRect(const BgElement &bg, const View &cameraView, const View &view, ClipRect &out) const;
	void loadControllers(const std::vector<IniSection> &sections, const std::string &defPath);
	void runController(BgCtrl &ctrl);

	bool stage_ = false;
	const AnimationTable *anims_ = nullptr;
	std::vector<BgCtrl> controllers_;
	int32_t time_ = 0;

	std::vector<BgElement> bgs_;
	const Sff *sff_ = nullptr;
	std::unique_ptr<SpriteRenderer> renderer_;
};

} // namespace m360
