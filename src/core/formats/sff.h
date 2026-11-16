#pragma once

#include <cstdint>
#include <map>
#include <memory>
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
	// Palette indices, width * height, for an indexed sprite; empty for a true-colour one. An SFF v2 sprite keeps
	// these empty until Sff::decode (see packed).
	mutable std::vector<uint8_t> indices;
	// 0xAARRGGBB pixels for a true-colour sprite (PNG24/32 in SFF v2).
	mutable std::vector<Argb> argb;
	// A sprite's compressed pixels, where they are in the file: decoded only while a texture is made from them, as
	// a screenpack's full-colour sprites take hundreds of MB decoded and the 360 has about 450. SFF v1 sprites too
	// (their PCX run-length pixels): a big roster's characters are mostly v1, tens of MB each decoded.
	bool packed = false, trueColor = false;
	uint8_t format = 0, depth = 0;
	uint32_t packedOffset = 0, packedSize = 0;
	// An SFF v1 sprite's PCX bytes per line (0: stored raw).
	uint16_t lineBytes = 0;

	bool isTrueColor() const { return trueColor || !argb.empty(); }
	bool isBlank() const { return width == 0 || height == 0 || (!packed && indices.empty() && argb.empty()); }
	// The sprite's pixels as straight 0xAARRGGBB through a palette. With mask (characters always), index 0 is
	// transparent; without it (a background's "mask = 0"), index 0 draws as its palette colour, opaque.
	std::vector<Argb> toArgb(const Palette &palette, bool mask = true) const;
};

// An SFF sprite file: version 2 (MUGEN 1.x) or version 1 (WinMUGEN: PCX sprites). Translated from Ikemen-GO
// src/image.go: SffHeader.Read, loadSff, readHeader / readHeaderV2, Sprite.read (PCX, RlePcxDecode) and readV2, the
// RLE8/RLE5/LZ5 decoders, Sff.loadPalettes, ReadPalette and readActPalette.
class Sff {
public:
	// character: a character's SFF v1, whose first sprite (0,0) is read the old way and holds the shared palette.
	bool load(const std::string &path, std::string &error, bool character = false);
	// Set before loading a character that has .act palettes: its SFF v1's 0,0 and the sprites sharing its palette
	// take the chosen .act one (Ikemen's isActPal).
	void setActPalettes(bool act) { actPalettes_ = act; }
	// Only these sprites (group, number) and the palettes, read from the file in pieces: a select screen's portraits
	// without reading or keeping a character's whole sprite file. An SFF v1 is read whole, then trimmed.
	bool loadSome(const std::string &path, const std::vector<std::pair<uint16_t, uint16_t>> &keys, std::string &error,
		bool character = false);
	// The same from bytes already read (tests build small SFF files in memory).
	bool loadBytes(const std::vector<uint8_t> &file, const std::string &name, std::string &error, bool character);
	// SFF v1: the sprites drawn with the character's selected palette are those using palette 0 (the shared one);
	// the rest keep their own.
	bool isV1() const { return v1_; }
	// Adds or replaces palette group,number (a .act file); returns its index.
	int setPalette(uint16_t group, uint16_t number, const Palette &palette);
	// Adds a sprite built elsewhere (an FNT v1 font cuts its glyphs out of one PCX); an existing group,number stays.
	void addSprite(const Sprite &sprite);
	// Drops every sprite but these (group, number) pairs; the select screen keeps only portraits.
	void keepOnly(const std::vector<std::pair<uint16_t, uint16_t>> &keys);

	const Sprite *get(uint16_t group, uint16_t number) const;
	// A packed sprite's pixels, decoded into it (false if they won't decode); release() drops them again once a
	// texture holds the picture.
	bool decode(const Sprite &sprite) const;
	void release(const Sprite &sprite) const;
	const std::vector<Sprite> &sprites() const { return sprites_; }
	const std::string &path() const { return path_; }
	const std::vector<Palette> &palettes() const { return palettes_; }
	// The palette list index for palette group,number (a character's palette 1 is 1,1), or -1.
	int paletteIndex(uint16_t group, uint16_t number) const;
	// Whether sprites drawn with this palette take a character's chosen palette: in an SFF v1 those sharing palette 0
	// or with a copy of it (Ikemen remaps palette 0; sffv1BasePal); in an SFF v2 those on palette 1,1 or a copy of it
	// (Ikemen remaps 1,1 and its duplicates), every one when the file has no 1,1. Effects and helpers with palettes
	// of their own keep them.
	bool takesChosenPalette(int palette) const;
	const Palette &palette(int index) const;

private:
	std::vector<Sprite> sprites_;
	// The file, which packed sprites decode from.
	std::shared_ptr<const std::vector<uint8_t>> file_;
	std::string path_;
	std::map<uint32_t, size_t> byKey_;
	std::vector<Palette> palettes_;
	std::map<uint32_t, int> paletteByKey_;
	bool v1_ = false;
	bool actPalettes_ = false;
	// 1 for the base palette (v2: 1,1; v1: 0) and the palettes the file holds as copies of it, found as it loads
	// (before .act files replace any).
	std::vector<uint8_t> baseCopies_;

	void markBaseCopies();
	// The file's bytes, kept as they are (no copy: on the 360 the heap never shrinks, so a copy's peak stays).
	bool loadShared(std::shared_ptr<const std::vector<uint8_t>> file, const std::string &name, std::string &error,
		bool character);
	bool loadV1(const std::vector<uint8_t> &file, const std::string &name, std::string &error, bool character);
};

// PCX run-length pixels: a byte of 0xC0 or more repeats the next one (its low 6 bits) times; each row is bytesPerLine
// long, of which the image keeps its width (Ikemen's RlePcxDecode). bytesPerLine 0: the pixels are stored raw.
std::vector<uint8_t> decodePcxRle(const uint8_t *rle, size_t size, int width, int height, int bytesPerLine);

// A .act palette file: 256 RGB colours stored last to first, colour 0 transparent (Ikemen's readActPalette).
bool readActPalette(const std::string &path, Palette &out);

} // namespace m360
