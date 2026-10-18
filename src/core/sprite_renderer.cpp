#include "core/sprite_renderer.h"

#include <algorithm>
#include <cmath>

namespace m360 {

SpriteRenderer::~SpriteRenderer()
{
	clear();
}

void SpriteRenderer::clear()
{
	for (auto &[key, tex] : cache_)
		platform::destroyTexture(tex);
	cache_.clear();
}

platform::Texture *SpriteRenderer::texture(const Sprite &sprite, int palette, const SpriteDraw &how)
{
	const size_t index = (size_t)(&sprite - sff_.sprites().data());
	// Only what changes the pixels goes in the key.
	const Key key{ index, sprite.isTrueColor() ? -1 : palette, how.mask, how.shadow >= 0 ? Trans::None : how.trans,
		how.trans == Trans::None ? (uint8_t)255 : how.srcAlpha, how.trans == Trans::None ? (uint8_t)0 : how.dstAlpha,
		how.shadow, how.addR, how.addG, how.addB, how.mulR, how.mulG, how.mulB };
	const auto it = cache_.find(key);
	if (it != cache_.end())
		return it->second;

	platform::Texture *tex = nullptr;
	if (!sprite.isBlank()) {
		std::vector<Argb> pixels = sprite.toArgb(sff_.palette(palette), how.mask);
		const float s = (how.trans == Trans::None ? 255 : how.srcAlpha) / 255.0f;
		const float keep = (how.trans == Trans::None ? 0 : how.dstAlpha) / 255.0f;
		for (Argb &p : pixels) {
			const float a = (p >> 24) / 255.0f;
			float outA = a;
			float r = std::clamp(((int)(p >> 16 & 0xFF) + how.addR) * how.mulR / 256.0f, 0.0f, 255.0f) * a;
			float g = std::clamp(((int)(p >> 8 & 0xFF) + how.addG) * how.mulG / 256.0f, 0.0f, 255.0f) * a;
			float b = std::clamp(((int)(p & 0xFF) + how.addB) * how.mulB / 256.0f, 0.0f, 255.0f) * a;
			if (how.shadow >= 0) {
				// Black, covering the screen by the shadow's strength where the sprite is.
				r = g = b = 0;
				outA = a * how.shadow / 255.0f;
			} else if (how.trans == Trans::Add) {
				r *= s, g *= s, b *= s;
				outA = a * (1 - keep);
			} else if (how.trans == Trans::Sub || how.trans == Trans::SubAdd) {
				r *= s, g *= s, b *= s;
				outA = 0;
			}
			p = (Argb)(outA * 255 + 0.5f) << 24 | (Argb)(r + 0.5f) << 16 | (Argb)(g + 0.5f) << 8 | (Argb)(b + 0.5f);
		}
		tex = platform::createTexture(sprite.width, sprite.height, pixels.data());
	}
	cache_[key] = tex;
	return tex;
}

void SpriteRenderer::drawSprite(int32_t group, int32_t number, float x, float y, const SpriteDraw &how)
{
	if (group < 0 || number < 0)
		return;
	const Sprite *sprite = sff_.get((uint16_t)group, (uint16_t)number);
	if (!sprite || sprite->isBlank())
		return;
	const int palette = how.palette >= 0 ? how.palette : palette_ >= 0 ? palette_ : sprite->palette;
	platform::Texture *tex = texture(*sprite, palette, how);
	if (!tex)
		return;
	const float w = sprite->width * how.scaleX, h = sprite->height * how.scaleY;
	const float ax = sprite->axisX * how.scaleX, ay = sprite->axisY * how.scaleY;
	// Unflipped, the axis pixel lands on (x, y); flipped, the sprite mirrors about it.
	const float left = how.flipH ? x + ax - w : x - ax;
	const float top = how.flipV ? y + ay - h : y - ay;
	const float u0 = how.flipH ? 1.0f : 0.0f, u1 = 1 - u0;
	const float v0 = how.flipV ? 1.0f : 0.0f, v1 = 1 - v0;
	// Shear shifts each row by its distance below the axis.
	const float topShift = how.shear * (top - y), bottomShift = how.shear * (top + h - y);
	platform::Vertex corners[4] = {
		{ left + topShift, top, u0, v0 },
		{ left + w + topShift, top, u1, v0 },
		{ left + bottomShift, top + h, u0, v1 },
		{ left + w + bottomShift, top + h, u1, v1 },
	};
	if (how.angle != 0) {
		// Counterclockwise on screen, where y grows downward.
		const float r = -how.angle * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
		for (platform::Vertex &v : corners) {
			const float dx = v.x - how.pivotX, dy = v.y - how.pivotY;
			v.x = how.pivotX + dx * c - dy * s;
			v.y = how.pivotY + dx * s + dy * c;
		}
	}
	const bool subtract = how.shadow < 0 && (how.trans == Trans::Sub || how.trans == Trans::SubAdd);
	platform::drawQuad(tex, corners, subtract ? platform::Blend::Subtract : platform::Blend::Over);
}

void SpriteRenderer::drawAnimation(const Animation &anim, float x, float y, float scale, int facing, SpriteDraw base)
{
	const AnimFrame *frame = anim.drawFrame();
	if (!frame)
		return;
	// base's scale multiplies the frame's; a negative y scale (a shadow) turns it upside down about the feet.
	// The frame's offset (already mirrored by its own H/V flags) turns with the character and scales with base.
	const float ox = ((float)frame->xoffset + anim.offsetX()) * facing * scale * base.scaleX;
	const float oy = ((float)frame->yoffset + anim.offsetY()) * scale * base.scaleY;
	const float sy = scale * anim.scaleY() * base.scaleY;
	SpriteDraw how = base;
	how.scaleX = scale * anim.scaleX() * base.scaleX;
	how.scaleY = sy < 0 ? -sy : sy;
	how.flipH = (facing < 0) != (frame->hflip < 0);
	how.flipV = (frame->vflip < 0) != (sy < 0);
	if (base.trans == Trans::None) {
		how.trans = frame->trans;
		how.srcAlpha = frame->srcAlpha;
		how.dstAlpha = frame->dstAlpha;
	}
	// A rotation turns the whole frame about the character's position (mirrored when it faces left).
	if (how.angle != 0) {
		how.angle *= facing;
		how.pivotX = x;
		how.pivotY = y;
	}
	drawSprite(anim.spriteGroup(), anim.spriteNumber(), x + ox, y + oy, how);
}

} // namespace m360
