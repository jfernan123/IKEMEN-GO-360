#include "core/layout.h"

#include <algorithm>

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
	reset();
}

void AnimLayout::reset()
{
	if (present)
		anim.reset();
}

void AnimLayout::action()
{
	if (present)
		anim.action();
}

void AnimLayout::draw(float x, float y, int32_t layer, SpriteRenderer &renderer, const ScreenSpace &space) const
{
	if (!present || layout.layer != layer)
		return;
	SpriteDraw how;
	how.scaleX = layout.scaleX;
	how.scaleY = layout.scaleY * layout.vfacing;
	how.angle = layout.angle;
	renderer.drawAnimation(anim, space.x(x + layout.offsetX), space.y(y + layout.offsetY), space.scale, layout.facing,
		how);
}

void TextLayout::read(const std::string &prefix, const IniSection &section, int32_t defaultAlign, int32_t layer)
{
	align = defaultAlign;
	int32_t r = -1, g = -1, b = -1, a = 255;
	section.ints(prefix + "font", { &font, &bank, &align, &r, &g, &b, &a });
	if (r >= 0 && g >= 0 && b >= 0) {
		colour = (uint32_t)std::clamp(a, 0, 255) << 24 | (uint32_t)std::clamp(r, 0, 255) << 16 |
			(uint32_t)std::clamp(g, 0, 255) << 8 | (uint32_t)std::clamp(b, 0, 255);
	}
	if (section.has(prefix + "text"))
		text = section.text(prefix + "text");
	layout.layer = layer;
	layout.read(prefix, section);
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
	const float sx = layout.scaleX * layout.facing * space.scale, sy = layout.scaleY * layout.vfacing * space.scale;
	const float lineHeight = (float)(f.height() + f.spacingY()) * layout.scaleY;
	// A literal "\n" (backslash, n) in the def splits lines.
	size_t start = 0;
	for (int row = 0;; row++) {
		const size_t end = line.find("\\n", start);
		const std::string part = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
		f.draw(part, space.x(x + layout.offsetX), space.y(y + layout.offsetY + row * lineHeight), sx, sy, bank, align,
			colour);
		if (end == std::string::npos)
			break;
		start = end + 2;
	}
}

} // namespace m360
