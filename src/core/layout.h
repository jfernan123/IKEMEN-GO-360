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
// The fonts loaded by file name, and the table ids they were given.
using FontFiles = std::map<std::string, int32_t>;

// Where and how a screenpack element is drawn: the ".offset / .facing / .vfacing / .layerno / .scale / .angle /
// .xshear" keys after a prefix ("p1.bg0.", "menu.item."). Ikemen's Layout (Read), without 3D angles, projection or
// windows.
struct Layout {
	float offsetX = 0, offsetY = 0;
	int32_t facing = 1, vfacing = 1;
	int32_t layer = 0;
	float scaleX = 1, scaleY = 1;
	float angle = 0;
	// How far the sprite's bottom slants from its top, per pixel of height (Ikemen draws it negated).
	float xshear = 0;
	void read(const std::string &prefix, const IniSection &section);
};

// A screenpack element's colour effect: ".palfx.time / add / mul / sinadd / sinmul" (Ikemen's ReadPalFX and
// PalFX.refresh / tickTimers). Grayscale ("color"), hue and inversion aren't supported. The values it gives are rounded
// to steps of 8, because the sprite renderer makes a texture for each colour combination: a pulsing bar then makes a
// few textures instead of a new one each frame.
struct PalFX {
	// Ticks it lasts: -1 forever (the default for a screenpack), 0 off.
	int32_t time = -1, startTime = -1;
	int32_t add[3] = { 0, 0, 0 }, mul[3] = { 256, 256, 256 };
	int32_t sinAdd[3] = { 0, 0, 0 }, sinMul[3] = { 0, 0, 0 };
	// The sine waves' periods and how far through them it is: [0] sinadd, [1] sinmul.
	int32_t cycle[2] = { 0, 0 }, sinTime[2] = { 0, 0 };
	// The values this tick (Ikemen's eAdd / eMul), and whether it's on.
	int32_t effectAdd[3] = { 0, 0, 0 }, effectMul[3] = { 256, 256, 256 };
	bool enabled = false;

	void read(const std::string &prefix, const IniSection &section);
	// One tick: this tick's values, then the timers.
	void step();
	// Back to its starting time (Ikemen's resetTxtPfx).
	void restart() { time = startTime; }
	// Puts this tick's add and multiply into a draw.
	void apply(SpriteDraw &how) const;
};

// A rectangle draws are clipped to, in screen pixels.
struct ClipRect {
	int32_t x = 0, y = 0, width = 0, height = 0;
};

// A sprite (".spr = group, number") or an animation from the def's own actions (".anim = n"), with its layout.
// Ikemen's AnimLayout.
struct AnimLayout {
	Layout layout;
	Animation anim;
	PalFX palfx;
	bool present = false;
	void read(const std::string &prefix, const IniSection &section, const AnimationTable &anims, int32_t layer = 0);
	void reset();
	void action();
	// Draws it at local (x, y) plus its offset, if it's on this layer. A bar draws its fill clipped to a rectangle, or
	// scaled by fillX / fillY (Ikemen's DrawAnim window and xscl / yscl).
	void draw(float x, float y, int32_t layer, SpriteRenderer &renderer, const ScreenSpace &space,
		const ClipRect *clip = nullptr, float fillX = 1, float fillY = 1) const;
	// Whether its animation has played through, or (inf) reached a last frame that shows forever (Ikemen's
	// AnimTextSnd.End for an animation).
	bool ended(bool inf) const;
};

// A line of text: ".font = n, bank, align, r, g, b, a" and ".text", with its layout. "\n" in the text starts a new
// line. Ikemen's FSText (readFSText) and AnimTextSnd's text drawing. An Ikemen motif can name a font file instead of
// a number ("default-3x5.def, 0, 1"): see resolveFontFile.
struct TextLayout {
	Layout layout;
	int32_t font = -1, bank = 0, align = 0;
	uint32_t colour = 0xFFFFFFFF;
	std::string text;
	std::string fontFile;
	// Last: right after font, GCC merged their defaults into an instruction Xenia skips (see xenia_scan).
	PalFX palfx;
	void read(const std::string &prefix, const IniSection &section, int32_t defaultAlign = 0, int32_t layer = 0);
	void step() { palfx.step(); }
	bool present(const FontTable &fonts) const { return fonts.count(font) != 0; }
	// Draws this text, or other text in this one's font and layout (a name, the clock), at local (x, y).
	void draw(float x, float y, int32_t layer, const FontTable &fonts, const ScreenSpace &space) const;
	void draw(const std::string &line, float x, float y, int32_t layer, const FontTable &fonts,
		const ScreenSpace &space) const;
};

// Ikemen's Fade: a screen's fade in from and fade out to a colour ("fadein." / "fadeout."), without animations.
// Points a layout that names its font by file at that font, loading it into the table the first time (ids from
// 1000, clear of [Files]' numbers). A font that won't load leaves the text undrawn.
void resolveFontFile(TextLayout &text, FontTable &fonts, FontFiles &files, const std::string &defPath);

struct ScreenFade {
	int32_t time = 30;
	uint32_t colour = 0;
	int32_t snd[2] = { -1, 0 };
	bool active = false, fadeIn = false;
	int32_t totalTime = 0, timeRemaining = 0;

	void read(const std::string &prefix, const IniSection &section);
	int32_t duration() const { return time > 0 ? time : 0; }
	void start(bool in);
	void stop() { active = false; }
	bool isActive() const { return active && timeRemaining >= 0 && totalTime > 0; }
	// One tick; returns the sound to play on its first tick, if any.
	bool step();
	void draw() const;
};

} // namespace m360
