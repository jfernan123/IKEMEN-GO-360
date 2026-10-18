#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "core/air.h"
#include "core/font.h"
#include "core/ini.h"
#include "core/sprite_renderer.h"

namespace m360 {

// A screenpack's coordinates: its localcoord (fight.def's or system.def's) fitted to the screen's width, centred
// vertically when the shapes differ.
struct ScreenSpace {
	float scale = 1;
	float originY = 0;
	static ScreenSpace forLocalcoord(int32_t width, int32_t height);
	float x(float localX) const { return localX * scale; }
	float y(float localY) const { return originY + localY * scale; }
};

// The fonts a def file names in [Files] as font1, font2...: the number is what ".font = n, bank, align" picks.
using FontTable = std::map<int32_t, std::unique_ptr<Font>>;
// Loads [Files] font1..font<n>, each looked for next to the def, then under data/ and data/font/. A font that's
// missing or TrueType is logged and left out, so text naming it isn't drawn.
void loadFonts(const IniSection &files, const std::string &defPath, FontTable &out);

// Where and how a screenpack element is drawn: the ".offset / .facing / .vfacing / .layerno / .scale / .angle" keys
// after a prefix ("p1.bg0.", "menu.item."). Ikemen's Layout (Read), without xshear, 3D angles, projection or windows.
struct Layout {
	float offsetX = 0, offsetY = 0;
	int32_t facing = 1, vfacing = 1;
	int32_t layer = 0;
	float scaleX = 1, scaleY = 1;
	float angle = 0;
	void read(const std::string &prefix, const IniSection &section);
};

// A sprite (".spr = group, number") or an animation from the def's own actions (".anim = n"), with its layout.
// Ikemen's AnimLayout.
struct AnimLayout {
	Layout layout;
	Animation anim;
	bool present = false;
	void read(const std::string &prefix, const IniSection &section, const AnimationTable &anims, int32_t layer = 0);
	void reset();
	void action();
	// Draws it at local (x, y) plus its offset, if it's on this layer.
	void draw(float x, float y, int32_t layer, SpriteRenderer &renderer, const ScreenSpace &space) const;
};

// A line of text: ".font = n, bank, align, r, g, b, a" and ".text", with its layout. "\n" in the text starts a new
// line. Ikemen's FSText (readFSText) and AnimTextSnd's text drawing.
struct TextLayout {
	Layout layout;
	int32_t font = -1, bank = 0, align = 0;
	uint32_t colour = 0xFFFFFFFF;
	std::string text;
	void read(const std::string &prefix, const IniSection &section, int32_t defaultAlign = 0, int32_t layer = 0);
	bool present(const FontTable &fonts) const { return fonts.count(font) != 0; }
	// Draws this text, or other text in this one's font and layout (a name, the clock), at local (x, y).
	void draw(float x, float y, int32_t layer, const FontTable &fonts, const ScreenSpace &space) const;
	void draw(const std::string &line, float x, float y, int32_t layer, const FontTable &fonts,
		const ScreenSpace &space) const;
};

} // namespace m360
