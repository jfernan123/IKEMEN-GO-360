#include "core/bgdef.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "core/layout.h"
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
	controllers_.clear();
	time_ = 0;
	sff_ = &sff;
	anims_ = &anims;
	renderer_ = std::make_unique<SpriteRenderer>(sff);
	const std::string start = prefix + " ";
	// Backgrounds, in file order: later elements draw over earlier ones.
	// The last element that isn't position-linked, which a linked one copies its motion from.
	int linkIndex = -1;
	// Keys some element sets that aren't read yet, logged once for the set.
	std::vector<std::string> unread;
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
		s.floats("scalestart", { &bg.scaleStartX, &bg.scaleStartY });
		s.floats("scaledelta", { &bg.scaleDeltaX, &bg.scaleDeltaY });
		// MUGEN takes a window only with all four values.
		for (const char *key : { "window", "maskwindow" }) {
			int32_t w[4] = { 0, 0, 0, 0 };
			if (text::split(s.get(key), ',').size() >= 4 && s.ints(key, { &w[0], &w[1], &w[2], &w[3] })) {
				for (int i = 0; i < 4; i++)
					bg.window[i] = w[i];
				bg.hasWindow = true;
				bg.maskWindow = key[0] == 'm';
			}
		}
		s.floats("windowdelta", { &bg.windowDeltaX, &bg.windowDeltaY });
		s.readInt("id", bg.id);
		bg.startVelocityX = bg.velocityX;
		bg.startVelocityY = bg.velocityY;
		for (int i = 0; i < 2; i++) {
			bg.startSinRadius[i] = bg.sinRadius[i];
			bg.startSinPeriod[i] = bg.sinPeriod[i];
		}
		for (const std::string &key : unreadKeys(s, { "type", "layerno", "actionno", "spriteno", "mask",
					 "positionlink", "start", "delta", "trans", "alpha", "tile", "width", "xscale", "yscalestart",
					 "yscaledelta", "tilespacing", "velocity", "sin.x", "sin.y", "scalestart", "scaledelta", "window",
					 "maskwindow", "windowdelta", "id" })) {
			if (std::find(unread.begin(), unread.end(), key) == unread.end())
				unread.push_back(key);
		}

		bgs_.push_back(std::move(bg));
		if (!bgs_.back().positionlink)
			linkIndex = (int)bgs_.size() - 1;
	}
	if (!unread.empty()) {
		std::string list;
		for (const std::string &key : unread)
			list += (list.empty() ? "" : ", ") + key;
		logf("bgdef: %s: %s elements: not read yet: %s", defPath.c_str(), prefix.c_str(), list.c_str());
	}
	if (stage_)
		loadControllers(sections, defPath);
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

// "[BGCtrlDef ...]" sets the elements (ctrlid, all if none) and looptime for the "[BGCtrl ...]" after it, each of
// which can name its own elements (Ikemen's loadStage BGCtrl reading and bgCtrl.read).
void BgDef::loadControllers(const std::vector<IniSection> &sections, const std::string &defPath)
{
	const auto elements = [&](const IniSection &s, std::vector<size_t> &out) {
		const std::vector<std::string> ids = text::split(s.get("ctrlid"), ',');
		if (ids.empty() || ids[0].empty())
			return false;
		out.clear();
		const bool all = ids.size() == 1 && std::atoi(ids[0].c_str()) == -1;
		for (size_t i = 0; i < bgs_.size(); i++) {
			bool match = all;
			for (const std::string &id : ids)
				match = match || bgs_[i].id == std::atoi(id.c_str());
			if (match)
				out.push_back(i);
		}
		return true;
	};
	std::vector<size_t> defTargets;
	int32_t defLoop = -1;
	for (const IniSection &s : sections) {
		const std::string kind = s.lower.substr(0, s.lower.find(' '));
		if (kind == "bgctrldef") {
			defLoop = -1;
			if (!elements(s, defTargets)) {
				defTargets.clear();
				for (size_t i = 0; i < bgs_.size(); i++)
					defTargets.push_back(i);
			}
			s.readInt("looptime", defLoop);
			continue;
		}
		if (kind != "bgctrl")
			continue;
		BgCtrl ctrl;
		ctrl.targets = defTargets;
		ctrl.loop = defLoop;
		elements(s, ctrl.targets);
		const std::string type = text::toLower(s.text("type"));
		static const std::pair<const char *, BgCtrl::Type> TYPES[] = { { "null", BgCtrl::Type::Null },
			{ "anim", BgCtrl::Type::Anim }, { "visible", BgCtrl::Type::Visible }, { "enable", BgCtrl::Type::Enable },
			{ "posset", BgCtrl::Type::PosSet }, { "posadd", BgCtrl::Type::PosAdd }, { "sinx", BgCtrl::Type::SinX },
			{ "siny", BgCtrl::Type::SinY }, { "velset", BgCtrl::Type::VelSet }, { "veladd", BgCtrl::Type::VelAdd } };
		bool known = false;
		for (const auto &[name, value] : TYPES) {
			if (type == name) {
				ctrl.type = value;
				known = true;
			}
		}
		if (!known) {
			logf("bgdef: %s: %s: BGCtrl type %s isn't supported", defPath.c_str(), s.name.c_str(), type.c_str());
			continue;
		}
		s.readInt("time", ctrl.start);
		ctrl.end = ctrl.start;
		s.ints("time", { &ctrl.start, &ctrl.end, &ctrl.loop });
		const bool xy = ctrl.type == BgCtrl::Type::PosSet || ctrl.type == BgCtrl::Type::PosAdd ||
			ctrl.type == BgCtrl::Type::VelSet || ctrl.type == BgCtrl::Type::VelAdd;
		if (xy) {
			ctrl.hasX = s.readFloat("x", ctrl.x);
			ctrl.hasY = s.readFloat("y", ctrl.y);
		} else if (s.readFloat("value", ctrl.x)) {
			s.ints("value", { &ctrl.value[0], &ctrl.value[1], &ctrl.value[2] });
		}
		controllers_.push_back(std::move(ctrl));
	}
}

// Ikemen's runBgCtrl, for the types supported.
void BgDef::runController(BgCtrl &ctrl)
{
	for (size_t i : ctrl.targets) {
		BgElement &bg = bgs_[i];
		switch (ctrl.type) {
		case BgCtrl::Type::Null: break;
		case BgCtrl::Type::Anim:
			if (anims_ && anims_->get(ctrl.value[0], bg.anim)) {
				bg.actionNo = ctrl.value[0];
				bg.anim.reset();
			}
			break;
		case BgCtrl::Type::Visible: bg.visible = ctrl.value[0] != 0; break;
		case BgCtrl::Type::Enable: bg.enabled = ctrl.value[0] != 0; break;
		case BgCtrl::Type::PosSet:
			if (ctrl.hasX)
				bg.offsetX = ctrl.x;
			if (ctrl.hasY)
				bg.offsetY = ctrl.y;
			break;
		case BgCtrl::Type::PosAdd:
			if (ctrl.hasX)
				bg.offsetX += ctrl.x;
			if (ctrl.hasY)
				bg.offsetY += ctrl.y;
			break;
		case BgCtrl::Type::VelSet:
			if (ctrl.hasX)
				bg.velocityX = ctrl.x;
			if (ctrl.hasY)
				bg.velocityY = ctrl.y;
			break;
		case BgCtrl::Type::VelAdd:
			if (ctrl.hasX)
				bg.velocityX += ctrl.x;
			if (ctrl.hasY)
				bg.velocityY += ctrl.y;
			break;
		case BgCtrl::Type::SinX:
		case BgCtrl::Type::SinY: {
			// The last value is a time offset into the swing, not a phase as in sin.x.
			const int axis = ctrl.type == BgCtrl::Type::SinY ? 1 : 0;
			if (ctrl.value[0] == 0)
				ctrl.value[1] = 0;
			int32_t time = 0;
			if (ctrl.value[1] != 0) {
				const float phase = (float)ctrl.value[2] / (float)ctrl.value[1];
				time = (int32_t)((phase - (float)(int32_t)phase) * (float)ctrl.value[1]);
				if (time < 0)
					time += std::abs(ctrl.value[1]);
			}
			bg.sinRadius[axis] = ctrl.x;
			bg.sinPeriod[axis] = ctrl.value[1];
			bg.sinTime[axis] = time;
			break;
		}
		}
	}
}

void BgDef::reset()
{
	time_ = 0;
	for (BgElement &bg : bgs_) {
		bg.offsetX = bg.offsetY = 0;
		bg.visible = bg.enabled = true;
		bg.velocityX = bg.startVelocityX;
		bg.velocityY = bg.startVelocityY;
		for (int i = 0; i < 2; i++) {
			bg.sinRadius[i] = bg.startSinRadius[i];
			bg.sinPeriod[i] = bg.startSinPeriod[i];
			bg.sinTime[i] = bg.sinStart[i];
			bg.sinOffset[i] = 0;
		}
		if (bg.actionNo >= 0)
			bg.anim.reset();
	}
}

void BgDef::update()
{
	// The controllers first, as their time stands (Ikemen's bgCtrlAction).
	for (BgCtrl &ctrl : controllers_) {
		if (ctrl.start < 0 || (ctrl.loop >= 0 && ctrl.start >= ctrl.loop))
			continue;
		if (ctrl.loop > 0 && ctrl.end > ctrl.loop)
			ctrl.end = ctrl.loop;
		bool active = false;
		if (time_ >= ctrl.start) {
			if (ctrl.loop > 0)
				active = (time_ - ctrl.start) % ctrl.loop <= ctrl.end - ctrl.start;
			else
				active = time_ <= ctrl.end;
		}
		if (active)
			runController(ctrl);
	}
	for (BgElement &bg : bgs_) {
		bg.offsetX += bg.velocityX;
		bg.offsetY += bg.velocityY;
		// Keep a tiled element's scroll within one tile, so it never loses precision.
		if (bg.tileX && bg.spacingX > 0)
			bg.offsetX = std::fmod(bg.offsetX, (float)bg.spacingX);
		if (bg.tileY && bg.spacingY > 0)
			bg.offsetY = std::fmod(bg.offsetY, (float)bg.spacingY);
		// A disabled element's swing and animation stop where they are (Ikemen's bgAction with updateTime off).
		for (int i = 0; i < 2; i++) {
			if (bg.sinPeriod[i] <= 0) {
				bg.sinOffset[i] = 0;
				continue;
			}
			bg.sinOffset[i] = bg.sinRadius[i] * (float)std::sin(2 * 3.14159265358979 * bg.sinTime[i] / bg.sinPeriod[i]);
			if (bg.enabled && ++bg.sinTime[i] >= bg.sinPeriod[i])
				bg.sinTime[i] = 0;
		}
		if (bg.actionNo >= 0 && bg.enabled)
			bg.anim.action();
	}
	time_++;
}

void BgDef::draw(int32_t layer, const View &view)
{
	for (const BgElement &bg : bgs_) {
		if (bg.layer == layer && bg.type != BgElement::Type::Dummy && bg.visible && bg.enabled)
			drawElement(bg, view);
	}
}

void BgDef::drawElement(const BgElement &bg, const View &cameraView)
{
	// The camera's zoom, softened by the element's delta: an element that doesn't scroll doesn't zoom either
	// (Ikemen's drawTransformed without zoomdelta).
	View view = cameraView;
	const float zoom = cameraView.zoomX;
	view.zoomX = std::max(0.0f, zoom + (1 - zoom) * (1 - std::max(0.0f, bg.deltaX)));
	view.zoomY = std::max(0.0f, zoom + (1 - zoom) * (1 - std::max(0.0f, bg.deltaY)));
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

	// The element's own scale: scalestart, plus scaledelta times how far the camera has scrolled it, and a parallax
	// element's yscalestart / yscaledelta as a reciprocal of the camera's height (Ikemen's drawTransformed).
	float ysdelta = bg.yscalestart + bg.yscaledelta * view.camY;
	if (bg.yscalestart * ysdelta < 0)
		ysdelta = -ysdelta;
	const float ys = (ysdelta != 0 ? 100.0f / ysdelta : 1.0f) * bg.scaleStartY + bg.scaleDeltaY * view.camY * bg.deltaY;
	const float xs = bg.scaleStartX + bg.scaleDeltaX * view.camX * bg.deltaX;
	how.scaleX = view.drawScaleX() * xs;
	how.scaleY = view.drawScaleY() * ys;
	const bool trapezoid = bg.type == BgElement::Type::Parallax && (bg.widthTop != 0 || bg.widthBottom != 0);
	// "width": the top and bottom rows' scales (Ikemen's xscale from width / sprite width), the top row centred on x.
	float topScale = xs, bottomScale = xs, anchorX = x;
	if (trapezoid) {
		topScale = (float)bg.widthTop / (float)sprite->width;
		bottomScale = (float)bg.widthBottom / (float)sprite->width;
		anchorX = x + std::fabs(bg.scaleStartX) * (-(float)bg.widthTop / 2 + (float)sprite->axisX * topScale);
	} else if (bg.type == BgElement::Type::Parallax && bg.rasterTop != 0) {
		// "xscale": the bottom row scrolls rasterBottom / rasterTop as fast as the top, a shear that grows with how far
		// the camera has moved it.
		const float xras = (bg.rasterBottom - bg.rasterTop) / bg.rasterTop;
		how.shear = xras * x / (std::fabs(ys) * sprite->height);
	}

	// Copies to draw: one, n, or as many as cover the screen; the spacing scales with the element.
	const float spanX = bg.spacingX > 0 ? (float)bg.spacingX * std::fabs(topScale) : 0.0f;
	const float spanY = bg.spacingY > 0 ? (float)bg.spacingY * std::fabs(ys) : 0.0f;
	const float spriteW = (float)sprite->width * std::fabs(topScale), spriteH = (float)sprite->height * std::fabs(ys);
	const float halfW = view.halfWidth();
	// The rows on screen, before the zoom about the ground line.
	const float zoomY = view.zoomY > 0 ? view.zoomY : 1;
	const float visibleTop = view.ground + (view.topCut - view.ground) / zoomY;
	const float visibleBottom = view.ground + (view.topCut + platform::SCREEN_HEIGHT / view.scale - view.ground) / zoomY;

	int32_t firstX = 0, lastX = 0, firstY = 0, lastY = 0;
	if (bg.tileX && spanX > 0) {
		if (bg.tileX == 1 || bg.tileX == INT32_MAX) {
			const float left = anchorX - (float)sprite->axisX * std::fabs(topScale);
			firstX = (int32_t)std::floor((-halfW - left - spriteW) / spanX);
			lastX = (int32_t)std::ceil((halfW - left) / spanX);
		} else {
			lastX = bg.tileX - 1;
		}
	}
	if (bg.tileY && spanY > 0) {
		if (bg.tileY == 1 || bg.tileY == INT32_MAX) {
			const float top = y - (float)sprite->axisY * std::fabs(ys);
			firstY = (int32_t)std::floor((visibleTop - top - spriteH) / spanY);
			lastY = (int32_t)std::ceil((visibleBottom - top) / spanY);
		} else {
			lastY = bg.tileY - 1;
		}
	}

	ClipRect window;
	const bool clip = bg.hasWindow && windowRect(bg, cameraView, view, window);
	if (bg.hasWindow && !clip)
		return;
	if (clip)
		platform::setClip(window.x, window.y, window.width, window.height);
	const float centre = view.originX + view.width / 2;
	for (int32_t ty = firstY; ty <= lastY; ty++) {
		for (int32_t tx = firstX; tx <= lastX; tx++) {
			const float px = view.screenX(anchorX + (float)tx * spanX), py = view.screenY(y + (float)ty * spanY);
			if (!trapezoid) {
				renderer_->drawSprite(group, number, px, py, how);
				continue;
			}
			// The bottom row is the top row scaled about the screen's centre column, so it scrolls faster as the camera
			// moves (Ikemen's renderSpriteQuad trapezoid convergence).
			const float topLeft = px - (float)sprite->axisX * topScale * view.drawScaleX();
			const float topWidth = (float)sprite->width * topScale * view.drawScaleX();
			const float ratio = topScale != 0 ? bottomScale / topScale : 1.0f;
			const float top = py - (float)sprite->axisY * how.scaleY, bottom = top + (float)sprite->height * how.scaleY;
			const float bottomLeft = centre + (topLeft - centre) * ratio;
			renderer_->drawSpriteTrapezoid(group, number, topLeft, topLeft + topWidth, top, bottomLeft,
				bottomLeft + topWidth * ratio, bottom, how);
		}
	}
	if (clip)
		platform::clearClip();
}

// The element's window in screen pixels (Ikemen's drawTransformed window): for a stage, in stage units from the top
// left (or top centre, "maskwindow"), moved by windowdelta of the camera's scroll and zoomed by it; for a
// screenpack, in its units from the top left. False when nothing of it is on screen.
bool BgDef::windowRect(const BgElement &bg, const View &cameraView, const View &view, ClipRect &out) const
{
	const float x1 = (float)std::min(bg.window[0], bg.window[2]), y1 = (float)std::min(bg.window[1], bg.window[3]);
	const float extra = bg.maskWindow ? 0.0f : 1.0f;
	const float w = (float)std::abs(bg.window[2] - bg.window[0]) + extra;
	const float h = (float)std::abs(bg.window[3] - bg.window[1]) + extra;
	float left = 0, top = 0, width = 0, height = 0;
	if (stage_) {
		const float zoom = cameraView.zoomX;
		const float wzx = std::max(0.0f, zoom + (1 - zoom) * (1 - std::max(0.0f, bg.windowDeltaX)));
		const float wzy = std::max(0.0f, zoom + (1 - zoom) * (1 - std::max(0.0f, bg.windowDeltaY)));
		const float halfStage = view.width / 2 / view.scale;
		const float zx = view.zoomX > 0 ? view.zoomX : 1, zy = view.zoomY > 0 ? view.zoomY : 1;
		float wx = x1 - view.camX * bg.windowDeltaX + halfStage / zx;
		if (!bg.maskWindow)
			wx -= halfStage;
		const float wy = y1 - view.camY * bg.windowDeltaY + view.ground / zy - view.ground;
		left = view.originX + wx * view.scale * wzx;
		top = (wy * wzy - view.topCut) * view.scale;
		width = w * view.scale * wzx;
		height = h * view.scale * wzy;
		// Inside the fight's area.
		const float right = std::min(left + width, view.originX + view.width);
		left = std::max(left, view.originX);
		width = right - left;
	} else {
		left = view.originX + x1 * view.scale;
		top = (y1 - view.topCut) * view.scale;
		width = w * view.scale;
		height = h * view.scale;
	}
	out.x = (int)std::floor(left);
	out.y = (int)std::floor(top);
	out.width = (int)std::floor(left + width) - out.x;
	out.height = (int)std::floor(top + height) - out.y;
	return out.width > 0 && out.height > 0 && out.x < platform::SCREEN_WIDTH && out.y < platform::SCREEN_HEIGHT &&
		out.x + out.width > 0 && out.y + out.height > 0;
}

} // namespace m360
