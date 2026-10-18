// Milestone 7e: WinMUGEN-era content. A small SFF v1 is built here byte by byte (a character's first sprite with the
// shared palette, a sprite reusing it, one with its own palette after the 0x0C marker, and a linked copy), then
// loaded through the same reader the game uses; and a .act palette is written and read back.

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "core/sff.h"

using namespace m360;

namespace {
int passed, failed;

void expect(bool value, const char *name)
{
	if (value)
		passed++;
	else {
		failed++;
		std::printf("FAIL  7e: %s\n", name);
	}
}

void u16(std::vector<uint8_t> &b, uint16_t v)
{
	b.push_back((uint8_t)v);
	b.push_back((uint8_t)(v >> 8));
}

void u32(std::vector<uint8_t> &b, uint32_t v)
{
	u16(b, (uint16_t)v);
	u16(b, (uint16_t)(v >> 16));
}

// A 32-byte SFF v1 sprite header.
void spriteHeader(std::vector<uint8_t> &b, uint32_t next, uint32_t size, uint16_t group, uint16_t number,
	uint16_t link, uint8_t samePalette)
{
	const size_t start = b.size();
	u32(b, next);
	u32(b, size);
	u16(b, 1); // axis x
	u16(b, 2); // axis y
	u16(b, group);
	u16(b, number);
	u16(b, link);
	b.push_back(samePalette);
	b.resize(start + 32, 0);
}

// A 2x2 8-bit RLE PCX: header, then the pixel bytes as given.
void pcx(std::vector<uint8_t> &b, const std::vector<uint8_t> &rle)
{
	const size_t start = b.size();
	b.push_back(10); // manufacturer
	b.push_back(5);  // version
	b.push_back(1);  // RLE
	b.push_back(8);  // bits per pixel
	u16(b, 0);
	u16(b, 0);
	u16(b, 1);
	u16(b, 1);
	b.resize(start + 66, 0);
	u16(b, 2); // bytes per line
	b.resize(start + 128, 0);
	b.insert(b.end(), rle.begin(), rle.end());
}

void palette(std::vector<uint8_t> &b, uint8_t r, uint8_t g, uint8_t bl)
{
	const size_t start = b.size();
	b.resize(start + 768, 0);
	b[start + 3] = r;
	b[start + 4] = g;
	b[start + 5] = bl;
}
} // namespace

int run7eTests()
{
	passed = failed = 0;

	std::vector<uint8_t> f(512, 0);
	const char signature[] = "ElecbyteSpr";
	for (int i = 0; i < 11; i++)
		f[i] = (uint8_t)signature[i];
	f[15] = 1; // version 1
	f[20] = 4; // four sprites
	f[24] = 0;
	f[25] = 2; // first header at 512

	// Sprite 0,0: the character's first sprite, its palette the block's last 768 bytes. Pixels 1, 2, then a run of 5.
	const uint32_t s0 = 512, s0size = 128 + 4 + 768, s1 = s0 + 32 + s0size;
	spriteHeader(f, s1, s0size, 0, 0, 0, 0);
	pcx(f, { 1, 2, 0xC2, 5 });
	palette(f, 10, 20, 30);
	// Sprite 1,0: shares the palette before it; all 7.
	const uint32_t s1size = 128 + 4, s2 = s1 + 32 + s1size;
	spriteHeader(f, s2, s1size, 1, 0, 0, 1);
	pcx(f, { 0xC2, 7, 0xC2, 7 });
	// Sprite 2,0: its own palette after a 0x0C marker; all 3.
	const uint32_t s2size = 128 + 4 + 1 + 768, s3 = s2 + 32 + s2size;
	spriteHeader(f, s3, s2size, 2, 0, 0, 0);
	pcx(f, { 3, 3, 3, 3 });
	f.push_back(0x0C);
	palette(f, 200, 0, 0);
	// Sprite 3,0: a link to sprite 0.
	spriteHeader(f, 0, 0, 3, 0, 0, 0);

	Sff sff;
	std::string error;
	expect(sff.loadBytes(f, "test.sff", error, true), "SFF v1 loads");
	expect(sff.isV1(), "SFF v1 is recognised");
	const Sprite *a = sff.get(0, 0), *b = sff.get(1, 0), *c = sff.get(2, 0), *d = sff.get(3, 0);
	expect(a && b && c && d, "all four sprites are found");
	if (a && b && c && d) {
		expect(a->width == 2 && a->height == 2 && a->axisX == 1 && a->axisY == 2, "size and axis");
		expect(a->indices == std::vector<uint8_t>({ 1, 2, 5, 5 }), "PCX run-length decoding");
		expect(sff.palette(a->palette)[1] == 0xFF0A141Eu, "first sprite's palette");
		expect(sff.palette(a->palette)[0] >> 24 == 0, "colour 0 is transparent");
		expect(b->palette == a->palette && b->indices == std::vector<uint8_t>(4, 7), "shared palette");
		expect(c->palette != a->palette && sff.palette(c->palette)[1] == 0xFFC80000u, "own palette after 0x0C");
		expect(c->indices == std::vector<uint8_t>(4, 3), "own palette sprite pixels");
		expect(d->indices == a->indices && d->palette == a->palette, "linked sprite");
	}

	// A .act file: colours stored last to first.
	const std::string actPath = "tests/out/test.act";
	{
		std::ofstream act(actPath, std::ios::binary);
		std::vector<uint8_t> bytes(768, 0);
		bytes[0] = 255; // the file's first colour is palette index 255
		bytes[765] = 1; // the last is index 0
		act.write((const char *)bytes.data(), (std::streamsize)bytes.size());
	}
	Palette act;
	expect(readActPalette(actPath, act), ".act loads");
	expect(act.size() == 256 && act[255] == 0xFFFF0000u, ".act colours are reversed");
	expect(act[0] >> 24 == 0, ".act colour 0 is transparent");
	const int slot = sff.setPalette(1, 1, act);
	expect(sff.paletteIndex(1, 1) == slot && sff.palette(slot)[255] == 0xFFFF0000u, ".act becomes palette 1,1");
	std::remove(actPath.c_str());

	std::printf("7e legacy content: %d passed, %d failed\n", passed, failed);
	return failed;
}
