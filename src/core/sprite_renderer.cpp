#include "core/sprite_renderer.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace m360 {

namespace {

// Every renderer's textures together, as the 360 would hold them: padded to 32x32 tiles, at least 64 KB each.
size_t textureCount = 0, textureBytes = 0, textureLogged = 0;
uint64_t useClock = 0;
std::vector<SpriteRenderer *> &renderers()
{
	static std::vector<SpriteRenderer *> all;
	return all;
}

size_t consoleBytes(int width, int height)
{
	const size_t w = ((size_t)width + 31) / 32 * 32, h = ((size_t)height + 31) / 32 * 32;
	const size_t bytes = (w * h * 4 + 65535) / 65536 * 65536;
	return bytes;
}

} // namespace

size_t SpriteRenderer::totalTextureBytes()
{
	return textureBytes;
}

SpriteRenderer::SpriteRenderer(const Sff &sff) : sff_(sff)
{
	renderers().push_back(this);
}

SpriteRenderer::~SpriteRenderer()
{
	clear();
	std::vector<SpriteRenderer *> &all = renderers();
	all.erase(std::remove(all.begin(), all.end(), this), all.end());
}

void SpriteRenderer::clear()
{
	for (auto &[key, cached] : cache_) {
		if (cached.texture) {
			textureCount--;
			textureBytes -= cached.bytes;
		}
		platform::destroyTexture(cached.texture);
	}
	cache_.clear();
}

void SpriteRenderer::trim()
{
	struct Entry {
		uint64_t lastUse;
		SpriteRenderer *renderer;
		Key key;
	};
	std::vector<Entry> entries;
	for (SpriteRenderer *r : renderers()) {
		for (const auto &[key, cached] : r->cache_) {
			if (cached.texture)
				entries.push_back(Entry{ cached.lastUse, r, key });
		}
	}
	std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.lastUse < b.lastUse; });
	// Down to three quarters, so this runs now and then rather than every draw.
	size_t freed = 0, count = 0;
	for (const Entry &e : entries) {
		if (textureBytes <= TEXTURE_BUDGET / 4 * 3)
			break;
		const auto it = e.renderer->cache_.find(e.key);
		textureCount--;
		textureBytes -= it->second.bytes;
		freed += it->second.bytes;
		count++;
		platform::destroyTexture(it->second.texture);
		e.renderer->cache_.erase(it);
	}
	logf("textures: freed %zu (%zu MB), %zu left (%zu MB)", count, freed >> 20, textureCount, textureBytes >> 20);
}

platform::Texture *SpriteRenderer::texture(const Sprite &sprite, int palette, const SpriteDraw &how)
{
	const size_t index = (size_t)(&sprite - sff_.sprites().data());
	// Only what changes the pixels goes in the key.
	const Key key{ index, sprite.isTrueColor() ? -1 : palette, how.mask, how.shadow >= 0 ? Trans::None : how.trans,
		how.trans == Trans::None ? (uint8_t)255 : how.srcAlpha, how.trans == Trans::None ? (uint8_t)0 : how.dstAlpha,
		how.shadow, how.addR, how.addG, how.addB, how.mulR, how.mulG, how.mulB };
	const auto it = cache_.find(key);
	if (it != cache_.end()) {
		it->second.lastUse = ++useClock;
		return it->second.texture;
	}

	platform::Texture *tex = nullptr;
	if (!sprite.isBlank() && sff_.decode(sprite)) {
		std::vector<Argb> pixels = sprite.toArgb(sff_.palette(palette), how.mask);
		// The texture keeps the picture; the sprite goes back to its packed bytes.
		sff_.release(sprite);
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
	Cached cached;
	cached.texture = tex;
	cached.lastUse = ++useClock;
	if (tex) {
		cached.bytes = consoleBytes(sprite.width, sprite.height);
		textureCount++;
		textureBytes += cached.bytes;
		// Every 32 MB more, a line in the log.
		if (textureBytes >= textureLogged + 32 * 1024 * 1024) {
			textureLogged = textureBytes / (32 * 1024 * 1024) * (32 * 1024 * 1024);
			logf("textures: %zu, %zu MB on a 360", textureCount, textureBytes >> 20);
		}
	}
	cache_[key] = cached;
	if (textureBytes > TEXTURE_BUDGET) {
		trim();
		textureLogged = 0;
		// The one just made may have been among the oldest only if everything else was drawn since; keep it.
		const auto made = cache_.find(key);
		return made == cache_.end() ? nullptr : made->second.texture;
	}
	return tex;
}

void SpriteRenderer::drawSprite(int32_t group, int32_t number, float x, float y, const SpriteDraw &how)
{
	if (group < 0 || number < 0)
		return;
	const Sprite *sprite = sff_.get((uint16_t)group, (uint16_t)number);
	if (!sprite || sprite->isBlank())
		return;
	// The renderer's palette (a character's chosen one) replaces the sprite's own, except in an SFF v1, where only the
	// sprites sharing palette 0 take it.
	const bool chosen = palette_ >= 0 && (!sff_.isV1() || sprite->palette == 0);
	const int palette = how.palette >= 0 ? how.palette : chosen ? palette_ : sprite->palette;
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
	const platform::Blend blend =
		how.shadow < 0 && (how.trans == Trans::Sub || how.trans == Trans::SubAdd) ? platform::Blend::Subtract
																				   : platform::Blend::Over;
	if (how.xangle == 0 && how.yangle == 0) {
		platform::drawQuad(tex, corners, blend);
		return;
	}
	// Turned in 3D: the platform maps a texture straight across each triangle, so a strongly turned sprite is drawn
	// as a grid of small pieces, each turned on its own, to keep its picture from bending.
	const int columns = how.yangle != 0 ? 8 : 1, rows = how.xangle != 0 ? 8 : 1;
	const auto corner = [&](float fu, float fv, platform::Vertex &out) {
		// Bilinear across the (sheared, rotated) flat quad, then turned.
		const float topX = corners[0].x + (corners[1].x - corners[0].x) * fu;
		const float topY = corners[0].y + (corners[1].y - corners[0].y) * fu;
		const float bottomX = corners[2].x + (corners[3].x - corners[2].x) * fu;
		const float bottomY = corners[2].y + (corners[3].y - corners[2].y) * fu;
		turnPoint(how, x, y, topX + (bottomX - topX) * fv, topY + (bottomY - topY) * fv, out.x, out.y);
		out.u = u0 + (u1 - u0) * fu;
		out.v = v0 + (v1 - v0) * fv;
	};
	for (int row = 0; row < rows; row++) {
		for (int column = 0; column < columns; column++) {
			platform::Vertex piece[4];
			const float fu0 = (float)column / columns, fu1 = (float)(column + 1) / columns;
			const float fv0 = (float)row / rows, fv1 = (float)(row + 1) / rows;
			corner(fu0, fv0, piece[0]);
			corner(fu1, fv0, piece[1]);
			corner(fu0, fv1, piece[2]);
			corner(fu1, fv1, piece[3]);
			platform::drawQuad(tex, piece, blend);
		}
	}
}

void SpriteRenderer::turnPoint(const SpriteDraw &how, float x, float y, float px, float py, float &outX, float &outY)
{
	// Relative to the axis, y down and z toward the viewer: X by xangle (Ikemen's -xangle with y up), then Y by
	// yangle.
	float dx = px - x, dy = py - y, dz = 0;
	if (how.xangle != 0) {
		const float r = how.xangle * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
		const float ny = dy * c - dz * s, nz = dy * s + dz * c;
		dy = ny;
		dz = nz;
	}
	if (how.yangle != 0) {
		const float r = how.yangle * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
		const float nx = dx * c + dz * s, nz = -dx * s + dz * c;
		dx = nx;
		dz = nz;
	}
	outX = x + dx;
	outY = y + dy;
	if (!how.perspective)
		return;
	// Seen from focalLength in front of the screen's centre: nearer points spread out, farther ones gather in.
	const float f = how.focalLength > 1 ? how.focalLength : 1;
	const float depth = f - dz > 1 ? f - dz : 1;
	const float cx = platform::SCREEN_WIDTH / 2.0f, cy = platform::SCREEN_HEIGHT / 2.0f;
	outX = cx + (outX - cx) * f / depth;
	outY = cy + (outY - cy) * f / depth;
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
