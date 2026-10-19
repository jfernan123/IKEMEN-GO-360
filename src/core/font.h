#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "core/sff.h"
#include "core/sprite_renderer.h"

namespace m360 {

// A MUGEN bitmap font, for screenpacks: FNT v1 (one .fnt file holding a PCX of every glyph side by side and a text map
// of where each one is) or FNT v2 (a .def naming an SFF with one sprite per glyph, its number the character code).
// "Banks" are colour variants: palettes 0,bank (bank type "palette"), or sprite groups (bank type "sprite").
// Translated from Ikemen-GO src/font.go (loadFntV1, loadFntV2, loadDefInfo, LoadFntSff, CharWidth, TextWidth,
// DrawText). A TrueType .def ("type = truetype") draws its .ttf with stb_truetype at "size"'s height (Ikemen's
// LoadFntTtf and the renderers' Printf: glyphs placed by their bearings, advanced by their advance widths), each
// glyph rasterized at the size it's drawn and kept as a texture per colour.
class Font {
public:
	Font();
	~Font();
	// The renderer points at glyphs_, so a font stays where it was made.
	Font(const Font &) = delete;
	Font &operator=(const Font &) = delete;

	bool load(const std::string &path, std::string &error);

	// Width in font pixels: one character, and a line with the font's spacing between characters.
	int32_t charWidth(uint32_t c, int32_t bank) const;
	int32_t textWidth(const std::string &text, int32_t bank) const;
	// Draws a line with its baseline at screen (x, y), scale screen pixels per font pixel. align: 1 left (x is the
	// start), 0 centred, -1 right (x is the end). colour multiplies the glyphs (0xAARRGGBB, white for none); the
	// add and multiply in effects (a screenpack's palfx) apply too.
	void draw(const std::string &text, float x, float y, float scaleX, float scaleY, int32_t bank, int align,
		uint32_t colour = 0xFFFFFFFF, const SpriteDraw &effects = SpriteDraw()) const;

	int32_t width() const { return sizeX_; }
	int32_t height() const { return sizeY_; }
	int32_t spacingY() const { return spacingY_; }

private:
	bool loadV1(const std::vector<uint8_t> &file, const std::string &path, std::string &error);
	bool loadTrueType(const std::string &text, const std::string &path, std::string &error);
	// A TrueType line's width in font pixels, and its drawing.
	float trueTypeWidth(const std::string &text) const;
	void drawTrueType(const std::string &text, float x, float y, float scaleX, float scaleY, int align,
		uint32_t colour) const;
	bool loadV2(const std::string &text, const std::string &path, std::string &error);
	// The [Def] section's size, spacing, offset, colours, type and bank type.
	void readDef(const std::string &text);
	// The sprite group holding a bank's glyphs, and the palette list index it draws with (-1: the glyph's own).
	int32_t glyphGroup(int32_t bank) const { return spriteBanks_ ? bank : 0; }
	int paletteFor(int32_t bank) const;

	Sff glyphs_;
	std::unique_ptr<SpriteRenderer> renderer_;
	int32_t sizeX_ = 0, sizeY_ = 0, spacingX_ = 0, spacingY_ = 0, offsetX_ = 0, offsetY_ = 0, colors_ = 1;
	bool spriteBanks_ = false;
	std::string type_ = "bitmap";
	int banks_ = 1;
	// TrueType: the .ttf's bytes and stb_truetype's view of them, and the glyph textures made so far, by code point,
	// pixel height and colour.
	struct TrueType;
	std::unique_ptr<TrueType> ttf_;
	struct Glyph {
		platform::Texture *texture = nullptr;
		int width = 0, height = 0, offsetX = 0, offsetY = 0;
		float advance = 0;
	};
	mutable std::map<std::tuple<uint32_t, int, uint32_t>, Glyph> glyphCache_;
	const Glyph &glyph(uint32_t code, int pixelHeight, uint32_t colour) const;
};

} // namespace m360
