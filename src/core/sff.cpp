#include "core/sff.h"

#include <cstring>

#include "core/bytes.h"
#include "core/file.h"
#include "core/log.h"
#include "lodepng.h"

namespace m360 {

namespace {

uint32_t key(uint16_t group, uint16_t number)
{
	return (uint32_t)group << 16 | number;
}

// SFF v2 sprite formats (the header's format byte).
enum Format : uint8_t {
	FORMAT_RAW = 0,
	FORMAT_RLE8 = 2,
	FORMAT_RLE5 = 3,
	FORMAT_LZ5 = 4,
	FORMAT_PNG8 = 10,
	FORMAT_PNG24 = 11,
	FORMAT_PNG32 = 12,
};

// The decoders below follow Ikemen-GO's byte for byte, out-of-range reads included: they clamp to the last byte
// instead of failing, so a sprite that's short a few bytes still decodes as Ikemen and MUGEN show it.
class Clamped {
public:
	Clamped(const uint8_t *data, size_t size) : data_(data), size_(size) {}
	uint8_t peek() const { return size_ ? data_[i_] : 0; }
	uint8_t next()
	{
		const uint8_t v = peek();
		if (i_ + 1 < size_)
			i_++;
		return v;
	}

private:
	const uint8_t *data_;
	size_t size_;
	size_t i_ = 0;
};

std::vector<uint8_t> rle8Decode(const uint8_t *rle, size_t size, size_t pixels)
{
	std::vector<uint8_t> p(pixels);
	if (!size)
		return p;
	Clamped in(rle, size);
	size_t j = 0;
	while (j < p.size()) {
		int n = 1;
		uint8_t d = in.next();
		if ((d & 0xC0) == 0x40) {
			n = d & 0x3F;
			d = in.next();
		}
		for (; n > 0 && j < p.size(); n--)
			p[j++] = d;
	}
	return p;
}

std::vector<uint8_t> rle5Decode(const uint8_t *rle, size_t size, size_t pixels)
{
	std::vector<uint8_t> p(pixels);
	if (!size)
		return p;
	Clamped in(rle, size);
	size_t j = 0;
	while (j < p.size()) {
		int rl = in.next();
		const uint8_t dlByte = in.peek();
		int dl = dlByte & 0x7F;
		uint8_t c = 0;
		if (dlByte >> 7) {
			in.next();
			c = in.peek();
		}
		in.next();
		for (;;) {
			if (j < p.size())
				p[j++] = c;
			if (--rl < 0) {
				if (--dl < 0)
					break;
				const uint8_t b = in.next();
				c = b & 0x1F;
				rl = b >> 5;
			}
		}
	}
	return p;
}

std::vector<uint8_t> lz5Decode(const uint8_t *rle, size_t size, size_t pixels)
{
	std::vector<uint8_t> p(pixels);
	if (!size)
		return p;
	Clamped in(rle, size);
	size_t j = 0;
	int n = 0;
	uint8_t ct = in.next(), rb = 0;
	unsigned cts = 0, rbc = 0;
	while (j < p.size()) {
		int d = in.next();
		if (ct & (1u << cts)) {
			if ((d & 0x3F) == 0) {
				d = (d << 2 | in.next()) + 1;
				n = in.next() + 2;
			} else {
				rb |= (uint8_t)((d & 0xC0) >> rbc);
				rbc += 2;
				n = d & 0x3F;
				if (rbc < 8) {
					d = in.next() + 1;
				} else {
					d = rb + 1;
					rb = 0;
					rbc = 0;
				}
			}
			for (;;) {
				if (j < p.size()) {
					// A copy from before the image's start reads 0, where Ikemen's would panic.
					p[j] = (size_t)d <= j ? p[j - d] : 0;
					j++;
				}
				if (--n < 0)
					break;
			}
		} else {
			if ((d & 0xE0) == 0) {
				n = in.next() + 8;
			} else {
				n = d >> 5;
				d &= 0x1F;
			}
			for (; n > 0 && j < p.size(); n--)
				p[j++] = (uint8_t)d;
		}
		if (++cts >= 8) {
			ct = in.next();
			cts = 0;
		}
	}
	return p;
}

// A paletted PNG's indices, one byte each, whatever its bit depth.
bool decodePng8(const uint8_t *data, size_t size, Sprite &s, std::string &error)
{
	lodepng::State state;
	state.decoder.color_convert = 0;
	std::vector<unsigned char> raw;
	unsigned w = 0, h = 0;
	if (unsigned e = lodepng::decode(raw, w, h, state, data, size)) {
		error = lodepng_error_text(e);
		return false;
	}
	const LodePNGColorMode &mode = state.info_png.color;
	if (mode.colortype != LCT_PALETTE && mode.colortype != LCT_GREY) {
		error = "PNG8 sprite isn't paletted";
		return false;
	}
	// lodepng packs sub-byte pixels with no padding between rows.
	const unsigned bits = mode.bitdepth;
	s.width = (uint16_t)w;
	s.height = (uint16_t)h;
	s.indices.resize((size_t)w * h);
	for (size_t i = 0; i < s.indices.size(); i++) {
		if (bits == 8) {
			s.indices[i] = raw[i];
		} else {
			const size_t bit = i * bits;
			const unsigned shift = 8 - bits - (unsigned)(bit % 8);
			s.indices[i] = (uint8_t)(raw[bit / 8] >> shift & ((1u << bits) - 1));
		}
	}
	return true;
}

bool decodePngTrueColor(const uint8_t *data, size_t size, Sprite &s, std::string &error)
{
	std::vector<unsigned char> rgba;
	unsigned w = 0, h = 0;
	if (unsigned e = lodepng::decode(rgba, w, h, data, size, LCT_RGBA, 8)) {
		error = lodepng_error_text(e);
		return false;
	}
	s.width = (uint16_t)w;
	s.height = (uint16_t)h;
	s.argb.resize((size_t)w * h);
	for (size_t i = 0; i < s.argb.size(); i++) {
		const uint8_t *px = &rgba[i * 4];
		s.argb[i] = (Argb)px[3] << 24 | (Argb)px[0] << 16 | (Argb)px[1] << 8 | px[2];
	}
	return true;
}

} // namespace

std::vector<Argb> Sprite::toArgb(const Palette &pal, bool mask) const
{
	if (isTrueColor())
		return argb;
	std::vector<Argb> out(indices.size());
	const Argb zero = mask || pal.empty() ? 0 : pal[0] | 0xFF000000;
	for (size_t i = 0; i < indices.size(); i++) {
		const uint8_t index = indices[i];
		out[i] = index == 0 ? zero : index < pal.size() ? pal[index] : 0;
	}
	return out;
}

bool Sff::load(const std::string &path, std::string &error)
{
	sprites_.clear();
	byKey_.clear();
	palettes_.clear();
	paletteByKey_.clear();

	std::vector<uint8_t> file;
	if (!readFile(path, file)) {
		error = "can't read " + path;
		return false;
	}
	ByteReader r(file);
	char signature[12];
	if (!r.bytes(signature, sizeof signature) || std::memcmp(signature, "ElecbyteSpr\0", 12) != 0) {
		error = path + " isn't an SFF file";
		return false;
	}
	// Version bytes, lowest first: verlo3, verlo2, verlo1, verhi.
	uint8_t version[4];
	version[3] = r.u8();
	version[2] = r.u8();
	version[1] = r.u8();
	version[0] = r.u8();
	if (version[0] != 2) {
		error = path + ": SFF version " + std::to_string(version[0]) + " isn't supported yet (only SFF v2)";
		return false;
	}
	r.skip(4 + 16);
	const uint32_t spriteHeaders = r.u32();
	const uint32_t spriteCount = r.u32();
	const uint32_t paletteHeaders = r.u32();
	const uint32_t paletteCount = r.u32();
	const uint32_t literalOffset = r.u32(); // "ldata": palettes and most sprites
	r.skip(4);
	const uint32_t translatedOffset = r.u32(); // "tdata"
	if (!r.ok()) {
		error = path + ": truncated header";
		return false;
	}

	// Palettes: 16-byte headers, then RGBA bytes in ldata. A palette with no data links to an earlier one.
	palettes_.resize(paletteCount);
	for (uint32_t i = 0; i < paletteCount; i++) {
		r.seek(paletteHeaders + i * 16);
		const uint16_t group = r.u16(), number = r.u16();
		r.skip(2); // colour count; the data's size is the reliable one
		const uint16_t link = r.u16();
		const uint32_t offset = r.u32(), size = r.u32();
		const auto existing = paletteByKey_.find(key(group, number));
		if (existing != paletteByKey_.end()) {
			palettes_[i] = palettes_[existing->second];
			continue;
		}
		if (size == 0) {
			if (link < i)
				palettes_[i] = palettes_[link];
		} else {
			const uint32_t colors = size / 4;
			Palette &pal = palettes_[i];
			pal.assign(colors < 256 ? 256 : colors, 0);
			ByteReader pr(file);
			pr.seek(literalOffset + offset);
			for (uint32_t c = 0; c < colors && c < 256; c++) {
				uint8_t rgba[4];
				if (!pr.bytes(rgba, 4))
					break;
				// SFF 2.0.0.0 stores no alpha: index 0 clear, the rest opaque. Later versions store it.
				if (version[2] == 0)
					rgba[3] = c == 0 ? 0 : 255;
				pal[c] = (Argb)rgba[3] << 24 | (Argb)rgba[0] << 16 | (Argb)rgba[1] << 8 | rgba[2];
			}
		}
		paletteByKey_[key(group, number)] = (int)i;
	}

	// Sprites: 28-byte headers.
	sprites_.resize(spriteCount);
	for (uint32_t i = 0; i < spriteCount; i++) {
		r.seek(spriteHeaders + i * 28);
		Sprite &s = sprites_[i];
		s.group = r.u16();
		s.number = r.u16();
		s.width = r.u16();
		s.height = r.u16();
		s.axisX = r.s16();
		s.axisY = r.s16();
		const uint16_t link = r.u16();
		const uint8_t format = r.u8();
		const uint8_t depth = r.u8();
		uint32_t offset = r.u32();
		const uint32_t size = r.u32();
		s.palette = r.u16();
		const uint16_t flags = r.u16();
		offset += (flags & 1) ? translatedOffset : literalOffset;
		if (!r.ok()) {
			error = path + ": truncated sprite header " + std::to_string(i);
			return false;
		}

		if (size == 0) {
			// A linked sprite shares an earlier one's pixels but keeps its own axis and palette.
			if (link < i) {
				const Sprite &src = sprites_[link];
				s.width = src.width;
				s.height = src.height;
				s.indices = src.indices;
				s.argb = src.argb;
			}
		} else {
			const size_t pixels = (size_t)s.width * s.height;
			const uint8_t *data = ByteReader(file).at(offset, size);
			if (!data) {
				error = path + ": sprite " + std::to_string(s.group) + "," + std::to_string(s.number) + " runs past the file";
				return false;
			}
			// Compressed formats start with the decoded size, 4 bytes, which the decoders don't need.
			const uint8_t *packed = size >= 4 ? data + 4 : data;
			const size_t packedSize = size >= 4 ? size - 4 : 0;
			std::string decodeError;
			bool decoded = true;
			switch (format) {
			case FORMAT_RAW:
				if (depth == 8) {
					s.indices.assign(data, data + (size < pixels ? size : pixels));
					s.indices.resize(pixels);
				} else {
					decoded = false;
					decodeError = "raw " + std::to_string(depth) + "-bit sprites aren't supported yet";
				}
				break;
			case FORMAT_RLE8: s.indices = rle8Decode(packed, packedSize, pixels); break;
			case FORMAT_RLE5: s.indices = rle5Decode(packed, packedSize, pixels); break;
			case FORMAT_LZ5: s.indices = lz5Decode(packed, packedSize, pixels); break;
			case FORMAT_PNG8: decoded = decodePng8(packed, packedSize, s, decodeError); break;
			case FORMAT_PNG24:
			case FORMAT_PNG32: decoded = decodePngTrueColor(packed, packedSize, s, decodeError); break;
			default:
				decoded = false;
				decodeError = "unknown format " + std::to_string(format);
				break;
			}
			if (!decoded)
				logf("sff: %s: sprite %u,%u: %s", path.c_str(), s.group, s.number, decodeError.c_str());
		}
		if (!byKey_.count(key(s.group, s.number)))
			byKey_[key(s.group, s.number)] = i;
	}
	logf("sff: %s: %u sprites, %u palettes", path.c_str(), spriteCount, paletteCount);
	return true;
}

const Sprite *Sff::get(uint16_t group, uint16_t number) const
{
	const auto it = byKey_.find(key(group, number));
	return it == byKey_.end() ? nullptr : &sprites_[it->second];
}

int Sff::paletteIndex(uint16_t group, uint16_t number) const
{
	const auto it = paletteByKey_.find(key(group, number));
	return it == paletteByKey_.end() ? -1 : it->second;
}

const Palette &Sff::palette(int index) const
{
	static const Palette empty(256, 0);
	return index >= 0 && (size_t)index < palettes_.size() && !palettes_[index].empty() ? palettes_[index] : empty;
}

} // namespace m360
