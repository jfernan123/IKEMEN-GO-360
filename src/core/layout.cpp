#include "core/layout.h"

#include <algorithm>
#include <cmath>

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"
#include "platform/platform.h"

namespace m360 {

ScreenSpace ScreenSpace::forLocalcoord(int32_t width, int32_t height)
{
	ScreenSpace space;
	if (width <= 0 || height <= 0)
		return space;
	space.scale = (float)platform::SCREEN_WIDTH / (float)width;
	space.originY = (platform::SCREEN_HEIGHT - height * space.scale) / 2;
	return space;
}

void loadFonts(const IniSection &files, const std::string &defPath, FontTable &out)
{
	for (const auto &[key, value] : files.values) {
		if (!text::startsWith(key, "font") || key.size() < 5 || key.find_first_not_of("0123456789", 4) != std::string::npos)
			continue;
		const int32_t number = text::atoi(key.substr(4));
		const std::string name = files.text(key);
		if (name.empty())
			continue;
		std::string error = "can't find " + name;
		for (const std::string &path : { directoryOf(defPath) + name, "data/" + name, "data/font/" + name }) {
			std::vector<uint8_t> probe;
			if (!readFile(path, probe))
				continue;
			auto font = std::make_unique<Font>();
			if (font->load(path, error))
				out[number] = std::move(font);
			break;
		}
		if (!out.count(number))
			logf("fonts: %s: font%d: %s", defPath.c_str(), number, error.c_str());
	}
}

void Layout::read(const std::string &prefix, const IniSection &section)
{
	section.floats(prefix + "offset", { &offsetX, &offsetY });
	int32_t v = 0;
	if (section.readInt(prefix + "facing", v))
		facing = v < 0 ? -1 : 1;
	if (section.readInt(prefix + "vfacing", v))
		vfacing = v < 0 ? -1 : 1;
	section.readInt(prefix + "layerno", layer);
	layer = std::min(layer, 2);
	section.floats(prefix + "scale", { &scaleX, &scaleY });
	section.readFloat(prefix + "angle", angle);
	section.readFloat(prefix + "xshear", xshear);
}

void PalFX::read(const std::string &prefix, const IniSection &section)
{
	*this = PalFX();
	section.readInt(prefix + "time", time);
	section.ints(prefix + "add", { &add[0], &add[1], &add[2] });
	section.ints(prefix + "mul", { &mul[0], &mul[1], &mul[2] });
	// "sinadd = r, g, b, period": a negative period swings the other way.
	for (int k = 0; k < 2; k++) {
		int32_t s[4] = { 0, 0, 0, 0 };
		if (!section.ints(prefix + (k == 0 ? "sinadd" : "sinmul"), { &s[0], &s[1], &s[2], &s[3] }))
			continue;
		int32_t *amplitude = k == 0 ? sinAdd : sinMul;
		const int32_t sign = s[3] < 0 ? -1 : 1;
		for (int i = 0; i < 3; i++)
			amplitude[i] = s[i] * sign;
		cycle[k] = s[3] * sign;
	}
	startTime = time;
}

void PalFX::step()
{
	enabled = time != 0;
	if (!enabled)
		return;
	for (int i = 0; i < 3; i++) {
		effectAdd[i] = add[i];
		effectMul[i] = mul[i];
	}
	// The sine waves (Ikemen's sinAdd / sinMul); a period of 2 starts a quarter turn in.
	for (int k = 0; k < 2; k++) {
		if (cycle[k] <= 1)
			continue;
		double turn = 2 * 3.14159265358979 * sinTime[k];
		if (cycle[k] == 2)
			turn += 3.14159265358979 / 2;
		const double wave = std::sin(turn / cycle[k]);
		for (int i = 0; i < 3; i++) {
			if (k == 0)
				effectAdd[i] += (int32_t)(wave * sinAdd[i]);
			else
				effectMul[i] += (int32_t)(wave * sinMul[i]);
		}
	}
	for (int k = 0; k < 2; k++) {
		if (cycle[k] > 0)
			sinTime[k] = (sinTime[k] + 1) % cycle[k];
	}
	if (time > 0)
		time--;
}

void PalFX::apply(SpriteDraw &how) const
{
	if (!enabled)
		return;
	// Steps of 8, so the renderer's texture cache sees few distinct colours.
	const auto steps = [](int32_t v) { return (v >= 0 ? v + 4 : v - 4) / 8 * 8; };
	how.addR = (int16_t)std::clamp(steps(effectAdd[0]), -255, 255);
	how.addG = (int16_t)std::clamp(steps(effectAdd[1]), -255, 255);
	how.addB = (int16_t)std::clamp(steps(effectAdd[2]), -255, 255);
	how.mulR = (uint16_t)std::clamp(steps(effectMul[0]), 0, 1024);
	how.mulG = (uint16_t)std::clamp(steps(effectMul[1]), 0, 1024);
	how.mulB = (uint16_t)std::clamp(steps(effectMul[2]), 0, 1024);
}

void AnimLayout::read(const std::string &prefix, const IniSection &section, const AnimationTable &anims, int32_t layer)
{
	// ".spr" is a one-frame animation that shows forever; ".anim" replaces it when both are given.
	AnimFrame frame;
	if (section.ints(prefix + "spr", { &frame.group, &frame.number })) {
		anim = Animation();
		anim.frames.push_back(frame);
		present = true;
	}
	int32_t no = -1;
	if (section.readInt(prefix + "anim", no)) {
		Animation found;
		if (anims.get(no, found)) {
			anim = found;
			present = true;
		}
	}
	layout.layer = layer;
	layout.read(prefix, section);
	palfx.read(prefix + "palfx.", section);
	reset();
}

void AnimLayout::reset()
{
	if (present)
		anim.reset();
}

void AnimLayout::action()
{
	palfx.step();
	if (present)
		anim.action();
}

bool AnimLayout::ended(bool inf) const
{
	if (!present || anim.frames.empty() || anim.loopEnd())
		return true;
	const AnimFrame *last = anim.curFrame();
	return inf && (size_t)anim.curElem() == anim.frames.size() - 1 && last && last->time == -1;
}

void AnimLayout::draw(float x, float y, int32_t layer, SpriteRenderer &renderer, const ScreenSpace &space,
	const ClipRect *clip, float fillX, float fillY) const
{
	if (!present || layout.layer != layer)
		return;
	SpriteDraw how;
	how.scaleX = layout.scaleX * fillX;
	how.scaleY = layout.scaleY * fillY * layout.vfacing;
	how.angle = layout.angle;
	// Ikemen shears by -xshear about the sprite's top, then moves it back by the shear at the axis: a shear about the
	// axis, which is what SpriteDraw's is.
	how.shear = -layout.xshear;
	palfx.apply(how);
	if (clip)
		platform::setClip(clip->x, clip->y, clip->width, clip->height);
	renderer.drawAnimation(anim, space.x(x + layout.offsetX), space.y(y + layout.offsetY), space.scale, layout.facing,
		how);
	if (clip)
		platform::clearClip();
}

void TextLayout::read(const std::string &prefix, const IniSection &section, int32_t defaultAlign, int32_t layer)
{
	align = defaultAlign;
	int32_t r = -1, g = -1, b = -1, a = 255;
	const std::vector<std::string> parts = text::split(section.get(prefix + "font"), ',');
	if (!parts.empty() && !parts[0].empty() && parts[0].find_first_not_of("0123456789-+ ") != std::string::npos) {
		// A font file instead of a number; the rest as usual.
		fontFile = parts[0];
		font = -1;
		int32_t *rest[6] = { &bank, &align, &r, &g, &b, &a };
		for (size_t i = 1; i < parts.size() && i <= 6; i++) {
			if (!parts[i].empty())
				*rest[i - 1] = text::atoi(parts[i]);
		}
	} else {
		section.ints(prefix + "font", { &font, &bank, &align, &r, &g, &b, &a });
	}
	if (r >= 0 && g >= 0 && b >= 0) {
		colour = (uint32_t)std::clamp(a, 0, 255) << 24 | (uint32_t)std::clamp(r, 0, 255) << 16 |
			(uint32_t)std::clamp(g, 0, 255) << 8 | (uint32_t)std::clamp(b, 0, 255);
	}
	if (section.has(prefix + "text"))
		text = section.text(prefix + "text");
	layout.layer = layer;
	layout.read(prefix, section);
	palfx.read(prefix + "palfx.", section);
}

void TextLayout::draw(float x, float y, int32_t layer, const FontTable &fonts, const ScreenSpace &space) const
{
	draw(text, x, y, layer, fonts, space);
}

void TextLayout::draw(const std::string &line, float x, float y, int32_t layer, const FontTable &fonts,
	const ScreenSpace &space) const
{
	const auto found = fonts.find(font);
	if (layout.layer != layer || line.empty() || found == fonts.end())
		return;
	const Font &f = *found->second;
	SpriteDraw effects;
	palfx.apply(effects);
	const float sx = layout.scaleX * layout.facing * space.scale, sy = layout.scaleY * layout.vfacing * space.scale;
	const float lineHeight = (float)(f.height() + f.spacingY()) * layout.scaleY;
	// A literal "\n" (backslash, n) in the def splits lines.
	size_t start = 0;
	for (int row = 0;; row++) {
		const size_t end = line.find("\\n", start);
		const std::string part = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
		f.draw(part, space.x(x + layout.offsetX), space.y(y + layout.offsetY + row * lineHeight), sx, sy, bank, align,
			colour, effects);
		if (end == std::string::npos)
			break;
		start = end + 2;
	}
}

void resolveFontFile(TextLayout &text, FontTable &fonts, FontFiles &files, const std::string &defPath)
{
	if (text.fontFile.empty())
		return;
	const auto known = files.find(text.fontFile);
	if (known != files.end()) {
		text.font = known->second;
		return;
	}
	int32_t id = 1000;
	while (fonts.count(id))
		id++;
	files[text.fontFile] = -1;
	std::string error = "can't find " + text.fontFile;
	for (const std::string &path : { directoryOf(defPath) + text.fontFile, "data/" + text.fontFile,
			 "data/font/" + text.fontFile }) {
		std::vector<uint8_t> probe;
		if (!readFile(path, probe))
			continue;
		auto font = std::make_unique<Font>();
		if (font->load(path, error)) {
			fonts[id] = std::move(font);
			files[text.fontFile] = id;
		}
		break;
	}
	if (files[text.fontFile] < 0)
		logf("fonts: %s: %s: %s", defPath.c_str(), text.fontFile.c_str(), error.c_str());
	text.font = files[text.fontFile];
}

void ScreenFade::read(const std::string &prefix, const IniSection &section)
{
	section.readInt(prefix + "time", time);
	int32_t c[3] = { 0, 0, 0 };
	section.ints(prefix + "col", { &c[0], &c[1], &c[2] });
	colour = (uint32_t)(c[0] & 0xFF) << 16 | (uint32_t)(c[1] & 0xFF) << 8 | (uint32_t)(c[2] & 0xFF);
	section.ints(prefix + "snd", { &snd[0], &snd[1] });
}

void ScreenFade::start(bool in)
{
	totalTime = duration();
	if (totalTime <= 0) {
		active = false;
		return;
	}
	fadeIn = in;
	active = true;
	timeRemaining = totalTime;
}

bool ScreenFade::step()
{
	if (!active)
		return false;
	const bool sound = timeRemaining == totalTime && snd[0] != -1 && snd[1] != -1;
	if (--timeRemaining < 0)
		active = false;
	return sound;
}

void ScreenFade::draw() const
{
	if (!isActive() || time <= 0)
		return;
	const int32_t elapsed = totalTime - timeRemaining;
	if (elapsed < 0 || elapsed > time)
		return;
	const int32_t alpha = fadeIn ? (int32_t)std::floor(255.0 - 255.0 * (elapsed - 1) / time)
								 : (int32_t)std::floor(255.0 * elapsed / time);
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT,
		(uint32_t)std::clamp(alpha, 0, 255) << 24 | colour);
}

} // namespace m360
