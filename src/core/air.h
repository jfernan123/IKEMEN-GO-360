#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace m360 {

// How a frame blends onto what's behind it (AIR's seventh field).
enum class Trans : uint8_t { None, Add, Sub, SubAdd };

// A collision box: left, top, right, bottom, relative to the character's position, y up being negative.
using Clsn = std::array<float, 4>;

struct AnimFrame {
	int32_t time = -1;
	int32_t group = -1;
	int32_t number = 0;
	int32_t xoffset = 0;
	int32_t yoffset = 0;
	Trans trans = Trans::None;
	uint8_t srcAlpha = 255;
	uint8_t dstAlpha = 0;
	// -1 when the frame is flipped (H / V); the offset is flipped with it.
	int8_t hflip = 1;
	int8_t vflip = 1;
	float xscale = 1;
	float yscale = 1;
	float angle = 0;
	// Attack (Clsn1) and hurt (Clsn2) boxes.
	std::vector<Clsn> clsn1;
	std::vector<Clsn> clsn2;
};

// One action of an AIR file, with its playback state. Copy an AnimationTable entry to play it: each copy keeps its own
// time. Translated from Ikemen-GO src/anim.go (ReadAnimFrame, readAnimation, Action, UpdateSprite, animSeek, AnimTime,
// AnimElemTime, AnimElemNo, SetAnimElem, UpdateInterpolation).
class Animation {
public:
	std::vector<AnimFrame> frames;
	int32_t loopstart = 0;
	// The frames, after which an "Interpolate Offset/Scale/Angle/Blend" line came.
	std::vector<int32_t> interpolateOffset, interpolateScale, interpolateAngle, interpolateBlend;
	int32_t totaltime = 0;
	int32_t looptime = 0;
	int32_t prelooptime = 0;
	int32_t copyAction = -1;

	// Playback.
	void reset();
	// Advances one tick (1/60 s).
	void action();
	// The frame shown now, or null for an empty action.
	const AnimFrame *drawFrame() const { return frames.empty() ? nullptr : &frames[drawidx_]; }
	// The element playing now, whose collision boxes count for hits (Ikemen's CurrentFrame).
	const AnimFrame *curFrame() const
	{
		return (size_t)curelem_ < frames.size() ? &frames[curelem_] : nullptr;
	}
	// Its sprite, after a reset or an action(); -1,-1 when it shows none.
	int32_t spriteGroup() const { return spriteGroup_; }
	int32_t spriteNumber() const { return spriteNumber_; }
	// Interpolated additions to the drawn frame's offset and scale.
	float offsetX() const { return interpOffsetX_; }
	float offsetY() const { return interpOffsetY_; }
	float scaleX() const { return scaleX_; }
	float scaleY() const { return scaleY_; }

	// MUGEN's AnimTime, AnimElemTime(elem) and AnimElemNo(time) triggers.
	int32_t animTime() const { return curtime_ - totaltime; }
	int32_t animElemTime(int32_t elem) const;
	int32_t animElemNo(int32_t time) const;
	// Jumps to element elem (1-based), elemtime ticks in; the ChangeAnim controller's elem parameter.
	void setAnimElem(int32_t elem, int32_t elemtime = 0);
	int32_t curElem() const { return curelem_; }
	int32_t curElemTime() const { return curelemtime_; }
	int32_t loopCount() const { return loopcount_; }
	// Whether the action has played through once (MUGEN's "animtime = 0" moment has passed).
	bool loopEnd() const { return loopend_; }

private:
	void updateSprite();
	void updateInterpolation();
	void animSeek(int32_t elem);
	const AnimFrame &cur() const { return frames[curelem_]; }

	int32_t curtime_ = 0;
	int32_t curelem_ = 0;
	int32_t curelemtime_ = 0;
	int32_t drawidx_ = 0;
	int32_t loopcount_ = 0;
	bool newframe_ = true;
	bool loopend_ = false;
	int32_t spriteGroup_ = -1;
	int32_t spriteNumber_ = -1;
	float interpOffsetX_ = 0, interpOffsetY_ = 0;
	float scaleX_ = 1, scaleY_ = 1;
};

// Every action of an AIR file by number.
class AnimationTable {
public:
	// Parses AIR text; problems are logged and skipped, as MUGEN doesn't stop on them.
	void parse(const std::string &filename, const std::string &text);
	bool load(const std::string &path, std::string &error);

	// A fresh copy of action no, reset, or null.
	const Animation *find(int32_t no) const;
	bool get(int32_t no, Animation &out) const;
	const std::map<int32_t, Animation> &all() const { return anims_; }

private:
	void resolveCopyAction();
	std::map<int32_t, Animation> anims_;
};

// Parses one AIR element line ("200,1, 0,0, 4, H, A"); false when the line isn't an element. error is set for an
// element with bad optional fields, which is still returned.
bool readAnimFrame(const std::string &line, AnimFrame &out, std::string &error);

} // namespace m360
