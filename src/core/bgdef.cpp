#include "core/bgdef.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

// "mask = 1" and the like.
bool readBool(const IniSection &s, const std::string &key, bool &out)
{
	int32_t v = 0;
	if (!s.readInt(key, v))
		return false;
	out = v != 0;
	return true;
}

} // namespace

void BgDef::load(const std::vector<IniSection> &sections, const std::string &prefix, const Sff &sff,
	const AnimationTable &anims, const std::string &defPath)
{
	bgs_.clear();
	sff_ = &sff;
	renderer_ = std::make_unique<SpriteRenderer>(sff);
	const std::string start = prefix + " ";
	// Backgrounds, in file order: later elements draw over earlier ones.
	// The last element that isn't position-linked, which a linked one copies its motion from.
	int linkIndex = -1;
	for (const IniSection &s : sections) {
		if (!text::startsWith(s.lower, start))
			continue;
		BgElement bg;
		bg.name = s.name.substr(start.size());
		const std::string type = text::toLower(s.get("type"));
		if (type.empty())
			continue;
		switch (type[0]) {
		case 'n': bg.type = BgElement::Type::Normal; break;
		case 'a': bg.type = BgElement::Type::Anim; break;
		case 'p': bg.type = BgElement::Type::Parallax; break;
		case 'd': bg.type = BgElement::Type::Dummy; break;
		default: logf("bgdef: %s: %s: type %s isn't supported", defPath.c_str(), bg.name.c_str(), type.c_str()); continue;
		}
		s.readInt("layerno", bg.layer);
		int32_t actionNo = -1;
		if ((bg.type != BgElement::Type::Normal || !s.has("spriteno")) && s.readInt("actionno", actionNo)) {
			if (anims.get(actionNo, bg.anim)) {
				bg.actionNo = actionNo;
				bg.type = bg.type == BgElement::Type::Parallax ? bg.type : BgElement::Type::Anim;
				// Animations are masked, as Ikemen's readAnimation sets.
				bg.mask = true;
			} else {
				logf("bgdef: %s: %s: no action %d", defPath.c_str(), bg.name.c_str(), actionNo);
			}
		}
		if (bg.actionNo < 0) {
			s.ints("spriteno", { &bg.group, &bg.number });
			readBool(s, "mask", bg.mask);
		}
		readBool(s, "positionlink", bg.positionlink);
		if (bg.positionlink && linkIndex >= 0) {
			const BgElement &link = bgs_[(size_t)linkIndex];
			bg.deltaX = link.deltaX;
			bg.deltaY = link.deltaY;
			bg.velocityX = link.velocityX;
			bg.velocityY = link.velocityY;
		}
		s.floats("start", { &bg.startX, &bg.startY });
		if (!bg.positionlink)
			s.floats("delta", { &bg.deltaX, &bg.deltaY });

		const std::string trans = text::toLower(s.get("trans"));
		if (trans == "add") {
			bg.trans = Trans::Add, bg.srcAlpha = 255, bg.dstAlpha = 255, bg.mask = true;
		} else if (trans == "add1") {
			bg.trans = Trans::Add, bg.srcAlpha = 255, bg.dstAlpha = 128, bg.mask = true;
		} else if (trans == "addalpha") {
			bg.trans = Trans::Add, bg.srcAlpha = 255, bg.dstAlpha = 0, bg.mask = true;
		} else if (trans == "sub") {
			bg.trans = Trans::Sub, bg.srcAlpha = 255, bg.dstAlpha = 255, bg.mask = true;
		} else if (!trans.empty() && trans != "none" && trans != "default") {
			logf("bgdef: %s: %s: trans %s isn't supported", defPath.c_str(), bg.name.c_str(), trans.c_str());
		}
		if (bg.trans != Trans::None && s.has("alpha")) {
			int32_t src = bg.srcAlpha, dst = bg.dstAlpha;
			s.ints("alpha", { &src, &dst });
			bg.srcAlpha = (uint8_t)std::clamp(src, 0, 255);
			bg.dstAlpha = (uint8_t)std::clamp(dst, 0, 255);
		}

		if (s.ints("tile", { &bg.tileX, &bg.tileY })) {
			if (bg.type == BgElement::Type::Parallax)
				bg.tileY = 0;
			if (bg.tileX < 0)
				bg.tileX = INT32_MAX;
		}
		if (bg.type == BgElement::Type::Parallax) {
			if (!s.ints("width", { &bg.widthTop, &bg.widthBottom }))
				s.floats("xscale", { &bg.rasterTop, &bg.rasterBottom });
			s.readFloat("yscalestart", bg.yscalestart);
			s.readFloat("yscaledelta", bg.yscaledelta);
		}
		// Tile spacing counts from one copy's start to the next: the sprite's size plus "tilespacing".
		s.ints("tilespacing", { &bg.spacingX, &bg.spacingY });
		if (bg.actionNo < 0) {
			if (const Sprite *sprite = sff.get((uint16_t)bg.group, (uint16_t)bg.number)) {
				bg.spacingX += sprite->width;
				bg.spacingY += sprite->height;
			}
		} else {
			if (bg.spacingX == 0)
				bg.tileX = 0;
			if (bg.spacingY == 0)
				bg.tileY = 0;
		}
		s.floats("velocity", { &bg.velocityX, &bg.velocityY });
		for (int i = 0; i < 2; i++) {
			float radius = 0, period = 0, phase = 0;
			if (!s.floats(i == 0 ? "sin.x" : "sin.y", { &radius, &period, &phase }))
				continue;
			bg.sinRadius[i] = radius;
			bg.sinPeriod[i] = (int32_t)period;
			// The phase, in degrees, is where in the swing it starts.
			if (bg.sinPeriod[i] > 0) {
				bg.sinStart[i] = (int32_t)(phase * (float)bg.sinPeriod[i] / 360) % bg.sinPeriod[i];
				if (bg.sinStart[i] < 0)
					bg.sinStart[i] += bg.sinPeriod[i];
				bg.sinTime[i] = bg.sinStart[i];
			}
		}
		if (s.has("window") || s.has("maskwindow"))
			logf("bgdef: %s: %s: window isn't supported yet", defPath.c_str(), bg.name.c_str());

		bgs_.push_back(std::move(bg));
		if (!bgs_.back().positionlink)
			linkIndex = (int)bgs_.size() - 1;
	}
	// A linked element's start is relative to the element it links to.
	size_t base = 0;
	for (size_t i = 0; i < bgs_.size(); i++) {
		if (bgs_[i].positionlink && i > 0) {
			bgs_[i].startX += bgs_[base].startX;
			bgs_[i].startY += bgs_[base].startY;
		} else {
			base = i;
		}
	}
}

void BgDef::reset()
{
	for (BgElement &bg : bgs_) {
		bg.offsetX = bg.offsetY = 0;
		for (int i = 0; i < 2; i++) {
			bg.sinTime[i] = bg.sinStart[i];
			bg.sinOffset[i] = 0;
		}
		if (bg.actionNo >= 0)
			bg.anim.reset();
	}
}

void BgDef::update()
{
	for (BgElement &bg : bgs_) {
		bg.offsetX += bg.velocityX;
		bg.offsetY += bg.velocityY;
		// Keep a tiled element's scroll within one tile, so it never loses precision.
		if (bg.tileX && bg.spacingX > 0)
			bg.offsetX = std::fmod(bg.offsetX, (float)bg.spacingX);
		if (bg.tileY && bg.spacingY > 0)
			bg.offsetY = std::fmod(bg.offsetY, (float)bg.spacingY);
		for (int i = 0; i < 2; i++) {
			if (bg.sinPeriod[i] <= 0)
				continue;
			bg.sinOffset[i] = bg.sinRadius[i] * (float)std::sin(2 * 3.14159265358979 * bg.sinTime[i] / bg.sinPeriod[i]);
			if (++bg.sinTime[i] >= bg.sinPeriod[i])
				bg.sinTime[i] = 0;
		}
		if (bg.actionNo >= 0)
			bg.anim.action();
	}
}

void BgDef::draw(int32_t layer, const View &view)
{
	for (const BgElement &bg : bgs_) {
		if (bg.layer == layer && bg.type != BgElement::Type::Dummy)
			drawElement(bg, view);
	}
}

void BgDef::drawElement(const BgElement &bg, const View &view)
{
	// The element's position in stage units, relative to the screen's centre column and the stage's top.
	const float x = bg.startX + bg.offsetX + bg.sinOffset[0] - view.camX * bg.deltaX;
	const float y = bg.startY + bg.offsetY + bg.sinOffset[1] - view.camY * bg.deltaY;

	int32_t group = bg.group, number = bg.number;
	if (bg.actionNo >= 0) {
		group = bg.anim.spriteGroup();
		number = bg.anim.spriteNumber();
	}
	const Sprite *sprite = group >= 0 ? sff_->get((uint16_t)group, (uint16_t)number) : nullptr;
	if (!sprite || sprite->isBlank())
		return;

	SpriteDraw how;
	how.scaleX = how.scaleY = view.scale;
	how.mask = bg.mask;
	how.trans = bg.trans;
	how.srcAlpha = bg.srcAlpha;
	how.dstAlpha = bg.dstAlpha;
	if (bg.actionNo >= 0) {
		if (const AnimFrame *f = bg.anim.drawFrame()) {
			how.flipH = f->hflip < 0;
			how.flipV = f->vflip < 0;
			if (bg.trans == Trans::None) {
				how.trans = f->trans;
				how.srcAlpha = f->srcAlpha;
				how.dstAlpha = f->dstAlpha;
			}
		}
	}

	float drawY = y;
	if (bg.type == BgElement::Type::Parallax) {
		// The vertical scale follows the camera's height, as a reciprocal (Ikemen's drawTransformed).
		float ysdelta = bg.yscalestart + bg.yscaledelta * view.camY;
		if (bg.yscalestart * ysdelta < 0)
			ysdelta = -ysdelta;
		const float ys = ysdelta != 0 ? 100.0f / ysdelta : 1.0f;
		how.scaleY = view.scale * ys;
		if (bg.widthTop != 0 || bg.widthBottom != 0) {
			// "width": a trapezoid from widthTop to widthBottom, in strips so the texture doesn't bend.
			// TODO: draw these as strips; for now the average width, unsheared.
			how.scaleX = view.scale * (bg.widthTop + bg.widthBottom) / 2.0f / sprite->width;
		} else if (bg.rasterTop != 0) {
			// "xscale": the bottom row scrolls rasterBottom / rasterTop as fast as the top, a shear that grows with
			// how far the camera has moved it.
			const float xras = (bg.rasterBottom - bg.rasterTop) / bg.rasterTop;
			how.shear = xras * x / (std::fabs(ys) * sprite->height);
		}
	}

	// Copies to draw: one, n, or as many as cover the screen.
	const float spanX = bg.spacingX > 0 ? (float)bg.spacingX : 0.0f;
	const float spanY = bg.spacingY > 0 ? (float)bg.spacingY : 0.0f;
	const float halfW = view.halfWidth();
	const float visibleTop = view.topCut, visibleBottom = view.topCut + platform::SCREEN_HEIGHT / view.scale;
	int32_t firstX = 0, lastX = 0, firstY = 0, lastY = 0;
	if (bg.tileX && spanX > 0) {
		if (bg.tileX == 1 || bg.tileX == INT32_MAX) {
			const float left = x - sprite->axisX;
			firstX = (int32_t)std::floor((-halfW - left - sprite->width) / spanX);
			lastX = (int32_t)std::ceil((halfW - left) / spanX);
		} else {
			lastX = bg.tileX - 1;
		}
	}
	if (bg.tileY && spanY > 0) {
		if (bg.tileY == 1 || bg.tileY == INT32_MAX) {
			const float top = drawY - sprite->axisY;
			firstY = (int32_t)std::floor((visibleTop - top - sprite->height) / spanY);
			lastY = (int32_t)std::ceil((visibleBottom - top) / spanY);
		} else {
			lastY = bg.tileY - 1;
		}
	}
	for (int32_t ty = firstY; ty <= lastY; ty++) {
		for (int32_t tx = firstX; tx <= lastX; tx++) {
			renderer_->drawSprite(group, number, view.screenX(x + tx * spanX), view.screenY(drawY + ty * spanY), how);
		}
	}
}

} // namespace m360
