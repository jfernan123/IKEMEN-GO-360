#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace m360 {

// A colour as 0xAARRGGBB.
using Argb = uint32_t;
using Palette = std::vector<Argb>;

struct Sprite {
	uint16_t group = 0;
	uint16_t number = 0;
	uint16_t width = 0;
	uint16_t height = 0;
	// Where the sprite's axis sits, from its top left corner; MUGEN draws a sprite with its axis at the position.
	int16_t axisX = 0;
	int16_t axisY = 0;
	// The palette list index an indexed sprite uses.
	int palette = 0;
	// Palette indices, width * height, for an indexed sprite; empty for a true-colour one.
	std::vector<uint8_t> indices;
	// 0xAARRGGBB pixels for a true-colour sprite (PNG24/32 in SFF v2).
	std::vector<Argb> argb;

	bool isTrueColor() const { return !argb.empty(); }
	bool isBlank() const { return width == 0 || height == 0 || (indices.empty() && argb.empty()); }
	// The sprite's pixels as straight 0xAARRGGBB through a palette. With mask (characters always), index 0 is
	// transparent; without it (a background's "mask = 0"), index 0 draws as its palette colour, opaque.
	std::vector<Argb> toArgb(const Palette &palette, bool mask = true) const;
};

// An SFF sprite file. Version 2 only for now (SFF v1, PCX sprites, comes later; mugen360 says so when it meets one).
// Translated from Ikemen-GO src/image.go: SffHeader.Read, loadSff, readHeaderV2, readV2, the RLE8/RLE5/LZ5 decoders,
// Sff.loadPalettes and ReadPalette.
class Sff {
public:
	bool load(const std::string &path, std::string &error);

	const Sprite *get(uint16_t group, uint16_t number) const;
	const std::vector<Sprite> &sprites() const { return sprites_; }
	const std::vector<Palette> &palettes() const { return palettes_; }
	// The palette list index for palette group,number (a character's palette 1 is 1,1), or -1.
	int paletteIndex(uint16_t group, uint16_t number) const;
	const Palette &palette(int index) const;

private:
	std::vector<Sprite> sprites_;
	std::map<uint32_t, size_t> byKey_;
	std::vector<Palette> palettes_;
	std::map<uint32_t, int> paletteByKey_;
};

} // namespace m360
