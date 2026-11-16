#include "core/sprite_renderer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>

#include "core/log.h"

namespace m360 {

namespace {

// Every renderer's textures together, as the 360 holds them: padded to 32x32 tiles, in 4 KB pages (a big one in 64 KB
// pages of its own; see platform_x360's texture blocks).
size_t textureCount = 0, textureBytes = 0, textureLogged = 0;
uint64_t useClock = 0;
// The textures' limit: the budget, lowered when the 360 runs short of memory (the heap never gives memory back, so
// the limit stays lowered).
size_t textureLimit = SpriteRenderer::TEXTURE_BUDGET;
// When the limit last came down: once a second at most, as freed textures come back to the 360 a few frames later.
std::chrono::steady_clock::time_point limitLowered;
std::vector<SpriteRenderer *> &renderers()
{
	static std::vector<SpriteRenderer *> all;
	return all;
}

size_t consoleBytes(int width, int height)
{
	const size_t w = ((size_t)width + 31) / 32 * 32, h = ((size_t)height + 31) / 32 * 32;
	const size_t bytes = w * h * 4;
	return bytes > 512 * 1024 ? (bytes + 65535) / 65536 * 65536 : (bytes + 4095) / 4096 * 4096;
}

} // namespace

size_t SpriteRenderer::totalTextureBytes()
{
	return textureBytes;
}

namespace {
size_t createdCount = 0;
uint64_t createdMicroseconds = 0;
// Pixels converted into new colour variants of already-made sprites this frame (newFrame resets it). A super's
// flashes and darkening ask for a variant of nearly every sprite at once; making them all in one frame is what
// stalls the 360, so past this many the rest wait a frame or two, drawn meanwhile with a variant already made.
size_t variantPixels = 0;
constexpr size_t VARIANT_PIXELS_PER_FRAME = 640 * 480;

// Palette effects in steps of 8 (of 256): a flash that changes a little every frame reuses its textures instead of
// making new ones each frame. Too small a difference to see.
int16_t step8(int16_t v)
{
	return (int16_t)((v >= 0 ? v + 4 : v - 4) / 8 * 8);
}
uint16_t step8(uint16_t v)
{
	return (uint16_t)((v + 4) / 8 * 8);
}
} // namespace

void SpriteRenderer::newFrame()
{
	variantPixels = 0;
}

void SpriteRenderer::takeCreationStats(size_t &count, uint64_t &microseconds)
{
	count = createdCount;
	microseconds = createdMicroseconds;
	createdCount = 0;
	createdMicroseconds = 0;
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

void SpriteRenderer::freeAll()
{
	for (SpriteRenderer *r : renderers())
		r->clear();
	platform::freeDestroyedTextures();
}

// Short of memory on the 360 (below MEMORY_RESERVE free, or a texture that couldn't be made): the limit drops to
// three quarters of what the textures hold now.
static void lowerTextureLimit(const char *why)
{
	const auto now = std::chrono::steady_clock::now();
	if (now - limitLowered < std::chrono::seconds(1))
		return;
	limitLowered = now;
	textureLimit = std::max<size_t>(32u * 1024 * 1024, std::min(textureLimit, textureBytes / 4 * 3));
	size_t free = 0;
	platform::freeMemory(free);
	logf("textures: %s (%zu MB free): limit now %zu MB", why, free >> 20, textureLimit >> 20);
}

void SpriteRenderer::trim(size_t target)
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
	size_t freed = 0, count = 0;
	for (const Entry &e : entries) {
		if (textureBytes <= target)
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

platform::Texture *SpriteRenderer::texture(const Sprite &sprite, int palette, const SpriteDraw &asked)
{
	const size_t index = (size_t)(&sprite - sff_.sprites().data());
	SpriteDraw how = asked;
	how.addR = step8(how.addR), how.addG = step8(how.addG), how.addB = step8(how.addB);
	how.mulR = step8(how.mulR), how.mulG = step8(how.mulG), how.mulB = step8(how.mulB);
	if (how.saturation < 256)
		how.saturation = step8(how.saturation);
	// Only what changes the pixels goes in the key.
	const Key key{ index, sprite.isTrueColor() ? -1 : palette, how.mask, how.shadow >= 0 ? Trans::None : how.trans,
		how.trans == Trans::None ? (uint8_t)255 : how.srcAlpha, how.trans == Trans::None ? (uint8_t)0 : how.dstAlpha,
		how.shadow, how.addR, how.addG, how.addB, how.mulR, how.mulG, how.mulB, how.tint, how.saturation, how.invert };
	const auto it = cache_.find(key);
	if (it != cache_.end()) {
		it->second.lastUse = ++useClock;
		return it->second.texture;
	}
	// Over this frame's share: another colour of the same sprite, drawn the same way, stands in until there's room.
	const size_t pixels = (size_t)sprite.width * sprite.height;
	if (variantPixels + pixels > VARIANT_PIXELS_PER_FRAME) {
		const Key first{ std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key), std::get<4>(key),
			std::get<5>(key), std::get<6>(key), INT16_MIN, INT16_MIN, INT16_MIN, 0, 0, 0, 0, 0, false };
		const auto near = cache_.lower_bound(first);
		if (near != cache_.end() && std::get<0>(near->first) == std::get<0>(key) &&
			std::get<1>(near->first) == std::get<1>(key) && std::get<2>(near->first) == std::get<2>(key) &&
			std::get<3>(near->first) == std::get<3>(key) && std::get<4>(near->first) == std::get<4>(key) &&
			std::get<5>(near->first) == std::get<5>(key) && std::get<6>(near->first) == std::get<6>(key)) {
			near->second.lastUse = ++useClock;
			return near->second.texture;
		}
	}
	variantPixels += pixels;

	platform::Texture *tex = nullptr;
	bool createFailed = false;
	const auto started = std::chrono::steady_clock::now();
	if (!sprite.isBlank() && sff_.decode(sprite)) {
		std::vector<Argb> pixels = sprite.toArgb(sff_.palette(palette), how.mask);
		// The texture keeps the picture; the sprite goes back to its packed bytes.
		sff_.release(sprite);
		// Integer maths throughout: on the 360's CPU each float-to-integer conversion stalls, and this runs per pixel.
		// x * y / 255, rounded.
		const auto times = [](uint32_t x, uint32_t y) { const uint32_t t = x * y + 128; return (t + (t >> 8)) >> 8; };
		const uint32_t s = how.trans == Trans::None ? 255 : how.srcAlpha;
		const uint32_t keep = how.trans == Trans::None ? 0 : how.dstAlpha;
		const bool plain = how.shadow < 0 && how.trans == Trans::None && how.addR == 0 && how.addG == 0 &&
			how.addB == 0 && how.mulR == 256 && how.mulG == 256 && how.mulB == 256 && how.tint == 0 &&
			how.saturation >= 256 && !how.invert;
		const uint32_t saturation = how.saturation < 256 ? how.saturation : 256;
		const uint32_t tintA = how.tint >> 24, tintR = how.tint >> 16 & 0xFF, tintG = how.tint >> 8 & 0xFF,
					   tintB = how.tint & 0xFF;
		// The sprite shader's mix(c, tint, tint.a), on straight colour.
		const auto mix = [&times, tintA](uint32_t c, uint32_t t) { return times(c, 255 - tintA) + times(t, tintA); };
		const auto channel = [](uint32_t c, int add, uint32_t mul) {
			const int v = ((int)c + add) * (int)mul / 256;
			return (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v);
		};
		for (Argb &p : pixels) {
			const uint32_t a = p >> 24;
			if (a == 0) {
				p = 0;
				continue;
			}
			uint32_t r = p >> 16 & 0xFF, g = p >> 8 & 0xFF, b = p & 0xFF, outA = a;
			if (!plain) {
				// Ikemen's getFxPal: inverted, then mixed toward grey, then the add and multiply.
				if (how.invert)
					r = 255 - r, g = 255 - g, b = 255 - b;
				if (saturation < 256) {
					const uint32_t grey = (r + g + b) / 3;
					r = (grey * (256 - saturation) + r * saturation) >> 8;
					g = (grey * (256 - saturation) + g * saturation) >> 8;
					b = (grey * (256 - saturation) + b * saturation) >> 8;
				}
				r = channel(r, how.addR, how.mulR);
				g = channel(g, how.addG, how.mulG);
				b = channel(b, how.addB, how.mulB);
				if (tintA > 0) {
					r = mix(r, tintR);
					g = mix(g, tintG);
					b = mix(b, tintB);
				}
			}
			// Premultiplied by alpha (see platform::Blend).
			r = times(r, a);
			g = times(g, a);
			b = times(b, a);
			if (how.shadow >= 0) {
				// The tint's colour (black without one), covering the screen by the shadow's strength where the sprite
				// is: Ikemen's first shadow pass, Add with src intensity and dst 255 - intensity.
				outA = times(a, (uint32_t)how.shadow);
				r = times(tintR, outA), g = times(tintG, outA), b = times(tintB, outA);
			} else if (how.trans == Trans::Add) {
				r = times(r, s), g = times(g, s), b = times(b, s);
				outA = times(a, 255 - keep);
			} else if (how.trans == Trans::Sub || how.trans == Trans::SubAdd) {
				r = times(r, s), g = times(g, s), b = times(b, s);
				outA = 0;
			}
			p = outA << 24 | r << 16 | g << 8 | b;
		}
		tex = platform::createTexture(sprite.width, sprite.height, pixels.data());
		createFailed = !tex;
	}
	createdCount++;
	createdMicroseconds += (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - started).count();
	size_t free = 0;
	if (createFailed) {
		// Out of texture memory: free some, and leave this one out of the cache so it's tried again next draw.
		lowerTextureLimit("a texture couldn't be made");
		trim(textureLimit);
		return nullptr;
	}
	if (platform::freeMemory(free) && free < MEMORY_RESERVE)
		lowerTextureLimit("memory is short");
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
	if (textureBytes > textureLimit) {
		// Down to three quarters, so this runs now and then rather than every draw.
		trim(textureLimit / 4 * 3);
		textureLogged = 0;
		// The one just made may have been among the oldest only if everything else was drawn since; keep it.
		const auto made = cache_.find(key);
		return made == cache_.end() ? nullptr : made->second.texture;
	}
	return tex;
}

void SpriteRenderer::drawSpriteTrapezoid(int32_t group, int32_t number, float topLeft, float topRight, float top,
	float bottomLeft, float bottomRight, float bottom, const SpriteDraw &how)
{
	if (group < 0 || number < 0)
		return;
	const Sprite *sprite = sff_.get((uint16_t)group, (uint16_t)number);
	if (!sprite || sprite->isBlank())
		return;
	const bool chosen = palette_ >= 0 && sff_.takesChosenPalette(sprite->palette);
	const int palette = how.palette >= 0 ? how.palette : chosen ? palette_ : sprite->palette;
	platform::Texture *tex = texture(*sprite, palette, how);
	if (!tex)
		return;
	const platform::Blend blend = how.trans == Trans::Sub || how.trans == Trans::SubAdd ? platform::Blend::Subtract
																					   : platform::Blend::Over;
	// A grid of small pieces: each is nearly a parallelogram, so the texture's straight lines stay straight where a
	// piece's two triangles meet.
	constexpr int ROWS = 8, COLUMNS = 16;
	const auto point = [&](int column, int row, platform::Vertex &out) {
		const float fu = (float)column / COLUMNS, fv = (float)row / ROWS;
		const float left = topLeft + (bottomLeft - topLeft) * fv, right = topRight + (bottomRight - topRight) * fv;
		out.x = left + (right - left) * fu;
		out.y = top + (bottom - top) * fv;
		out.u = fu;
		out.v = fv;
	};
	for (int row = 0; row < ROWS; row++) {
		for (int column = 0; column < COLUMNS; column++) {
			platform::Vertex piece[4];
			point(column, row, piece[0]);
			point(column + 1, row, piece[1]);
			point(column, row + 1, piece[2]);
			point(column + 1, row + 1, piece[3]);
			platform::drawQuad(tex, piece, blend);
		}
	}
}

void SpriteRenderer::drawSprite(int32_t group, int32_t number, float x, float y, const SpriteDraw &how)
{
	if (group < 0 || number < 0)
		return;
	const Sprite *sprite = sff_.get((uint16_t)group, (uint16_t)number);
	if (!sprite || sprite->isBlank())
		return;
	// The renderer's palette (a character's chosen one) replaces the sprite's own where the sprite is drawn with the
	// character's base palette (Sff::takesChosenPalette); effects with palettes of their own keep them.
	const bool chosen = palette_ >= 0 && sff_.takesChosenPalette(sprite->palette);
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
