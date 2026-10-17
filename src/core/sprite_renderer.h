#pragma once

#include <cstdint>
#include <map>
#include <tuple>

#include "core/air.h"
#include "core/sff.h"
#include "platform/platform.h"

namespace m360 {

// How to draw one sprite. Positions and sizes are in screen pixels.
struct SpriteDraw {
	// Screen pixels per sprite pixel.
	float scaleX = 1, scaleY = 1;
	// Mirror about the axis.
	bool flipH = false, flipV = false;
	// MUGEN transparency: None draws normally; Add draws src * srcAlpha + dst * dstAlpha (out of 255); Sub subtracts.
	Trans trans = Trans::None;
	uint8_t srcAlpha = 255, dstAlpha = 0;
	// Whether colour 0 is see-through. MUGEN's backgrounds can turn it off (mask = 0), to draw colour 0 too.
	bool mask = true;
	// A shadow: the sprite's shape darkened by this much (0 none, 255 black) instead of its colours. -1: not a shadow.
	int shadow = -1;
	// Parallax: how far each row shifts right per screen pixel below the axis.
	float shear = 0;
	// The palette list index for an indexed sprite; -1 uses the renderer's, then the sprite's own.
	int palette = -1;
	// Palette effects, applied to straight RGB before premultiplication.
	int16_t addR = 0, addG = 0, addB = 0;
	uint16_t mulR = 256, mulG = 256, mulB = 256;
};

// Draws an SFF's sprites, turning each sprite into a texture the first time it's drawn a given way. MUGEN's
// transparency is baked into the texture as premultiplied colour (see platform::Blend), so the GPU needs no special
// shaders: a pixel of alpha a drawn "src * s + dst * d" becomes colour * a * s with alpha a * (1 - d).
// TODO(memory): one texture per sprite costs at least 64 KB of GPU memory each on the 360 (xsdk allocates in pages);
// fine for one character and a stage, but a full match will want sprites packed into atlas textures.
class SpriteRenderer {
public:
	explicit SpriteRenderer(const Sff &sff) : sff_(sff) {}
	~SpriteRenderer();
	SpriteRenderer(const SpriteRenderer &) = delete;
	SpriteRenderer &operator=(const SpriteRenderer &) = delete;

	const Sff &sff() const { return sff_; }
	// The palette list index indexed sprites use; -1 (the default) uses each sprite's own.
	void setPalette(int palette) { palette_ = palette; }
	int palette() const { return palette_; }

	// Draws sprite group,number with its axis at screen (x, y).
	void drawSprite(int32_t group, int32_t number, float x, float y, const SpriteDraw &how);
	// Draws an animation's current frame for a character at screen (x, y) facing right (1) or left (-1), scale screen
	// pixels per MUGEN pixel: the frame's offset, flips, scale and transparency applied as MUGEN does. base sets
	// anything else (a shadow, a palette); its scaleX / scaleY multiply the frame's, a negative scaleY drawing it
	// upside down about (x, y).
	void drawAnimation(const Animation &anim, float x, float y, float scale, int facing, SpriteDraw base = {});

	// Frees every texture, for after a palette change.
	void clear();

private:
	using Key = std::tuple<size_t, int, bool, Trans, uint8_t, uint8_t, int, int16_t, int16_t, int16_t,
		uint16_t, uint16_t, uint16_t>;
	platform::Texture *texture(const Sprite &sprite, int palette, const SpriteDraw &how);

	const Sff &sff_;
	int palette_ = -1;
	std::map<Key, platform::Texture *> cache_;
};

} // namespace m360
