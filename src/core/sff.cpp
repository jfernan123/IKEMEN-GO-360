#include "core/sff.h"

#include <algorithm>
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

bool Sff::load(const std::string &path, std::string &error, bool character)
{
	std::vector<uint8_t> file;
	if (!readFile(path, file)) {
		error = "can't read " + path;
		return false;
	}
	return loadBytes(file, path, error, character);
}

bool Sff::decode(const Sprite &sprite) const
{
	if (!sprite.packed || !sprite.indices.empty() || !sprite.argb.empty())
		return !sprite.isBlank();
	const uint8_t *data = file_ ? ByteReader(*file_).at(sprite.packedOffset, sprite.packedSize) : nullptr;
	if (!data)
		return false;
	// Compressed formats start with the decoded size, 4 bytes, which the decoders don't need.
	const uint32_t size = sprite.packedSize;
	const uint8_t *packed = size >= 4 ? data + 4 : data;
	const size_t packedSize = size >= 4 ? size - 4 : 0;
	const size_t pixels = (size_t)sprite.width * sprite.height;
	// The pixels are a cache of the file's: filling them doesn't change the sprite as the rest of the engine sees it.
	Sprite &s = const_cast<Sprite &>(sprite);
	std::string decodeError;
	bool decoded = true;
	switch (sprite.format) {
	case FORMAT_RAW:
		if (sprite.depth == 8) {
			s.indices.assign(data, data + (size < pixels ? size : pixels));
			s.indices.resize(pixels);
		} else {
			decoded = false;
			decodeError = "raw " + std::to_string(sprite.depth) + "-bit sprites aren't supported yet";
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
		decodeError = "unknown format " + std::to_string(sprite.format);
		break;
	}
	if (!decoded) {
		logf("sff: %s: sprite %u,%u: %s", path_.c_str(), sprite.group, sprite.number, decodeError.c_str());
		s.packed = false;
	}
	return decoded && !sprite.isBlank();
}

void Sff::release(const Sprite &sprite) const
{
	// Palette sprites are small (a byte a pixel) and get made into several textures (palettes, colour effects, and
	// again after the texture budget drops them): they stay decoded. Full-colour ones (a screenpack's backgrounds,
	// four bytes a pixel, hundreds of MB decoded) go back to their packed bytes.
	if (!sprite.packed || !sprite.isTrueColor())
		return;
	std::vector<uint8_t>().swap(sprite.indices);
	std::vector<Argb>().swap(sprite.argb);
}

bool Sff::loadBytes(const std::vector<uint8_t> &file, const std::string &path, std::string &error, bool character)
{
	sprites_.clear();
	byKey_.clear();
	palettes_.clear();
	paletteByKey_.clear();
	v1_ = false;
	file_.reset();
	path_ = path;

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
	if (version[0] == 1)
		return loadV1(file, path, error, character);
	if (version[0] != 2) {
		error = path + ": SFF version " + std::to_string(version[0]) + " isn't supported";
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
				s.packed = src.packed;
				s.trueColor = src.trueColor;
				s.format = src.format;
				s.depth = src.depth;
				s.packedOffset = src.packedOffset;
				s.packedSize = src.packedSize;
			}
		} else {
			if (!ByteReader(file).at(offset, size)) {
				error = path + ": sprite " + std::to_string(s.group) + "," + std::to_string(s.number) + " runs past the file";
				return false;
			}
			// Kept packed; decoded when drawn (see Sff::decode).
			s.packed = true;
			s.format = format;
			s.depth = depth;
			s.trueColor = format == FORMAT_PNG24 || format == FORMAT_PNG32;
			s.packedOffset = offset;
			s.packedSize = size;
		}
		if (!byKey_.count(key(s.group, s.number)))
			byKey_[key(s.group, s.number)] = i;
	}
	file_ = std::make_shared<const std::vector<uint8_t>>(file);
	logf("sff: %s: %u sprites, %u palettes, %zu KB", path.c_str(), spriteCount, paletteCount, file.size() >> 10);
	return true;
}

std::vector<uint8_t> decodePcxRle(const uint8_t *rle, size_t size, int width, int height, int bytesPerLine)
{
	std::vector<uint8_t> out((size_t)width * height, 0);
	if (!rle || size == 0)
		return out;
	if (bytesPerLine <= 0) {
		std::memcpy(out.data(), rle, std::min(size, out.size()));
		return out;
	}
	size_t i = 0, j = 0;
	int k = 0;
	// Bounded, so a damaged file can't loop forever on runs of length 0.
	for (size_t steps = 0; j < out.size() && steps < out.size() * 4 + size; steps++) {
		int n = 1;
		uint8_t d = rle[i];
		if (i < size - 1)
			i++;
		if (d >= 0xC0) {
			n = d & 0x3F;
			d = rle[i];
			if (i < size - 1)
				i++;
		}
		for (; n > 0; n--) {
			if (k < width && j < out.size())
				out[j++] = d;
			k++;
			if (k == bytesPerLine) {
				k = 0;
				n = 1;
			}
		}
	}
	return out;
}

namespace {

Palette readV1Palette(const std::vector<uint8_t> &file, size_t offset)
{
	Palette pal(256, 0);
	const uint8_t *rgb = ByteReader(file).at(offset, 768);
	if (!rgb)
		return pal;
	for (int c = 0; c < 256; c++)
		pal[c] = (c == 0 ? 0u : 0xFF000000u) | (Argb)rgb[c * 3] << 16 | (Argb)rgb[c * 3 + 1] << 8 | rgb[c * 3 + 2];
	return pal;
}

} // namespace

// SFF v1: a linked list of 32-byte sprite headers, each followed by a PCX image: a 128-byte header, RLE pixels, then a
// 0x0C marker and a 256-colour palette unless it shares the previous sprite's (Ikemen's loadSff, Sprite.read).
bool Sff::loadV1(const std::vector<uint8_t> &file, const std::string &path, std::string &error, bool character)
{
	v1_ = true;
	ByteReader r(file);
	r.seek(20);
	const uint32_t spriteCount = r.u32();
	uint32_t headerOffset = r.u32();
	if (!r.ok()) {
		error = path + ": truncated SFF v1 header";
		return false;
	}
	sprites_.resize(spriteCount);
	int prevPalette = -1;
	for (uint32_t i = 0; i < spriteCount; i++) {
		r.seek(headerOffset);
		const uint32_t next = r.u32(), size = r.u32();
		Sprite &s = sprites_[i];
		s.axisX = r.s16();
		s.axisY = r.s16();
		s.group = r.u16();
		s.number = r.u16();
		const uint16_t link = r.u16();
		const uint8_t samePalette = r.u8();
		if (!r.ok()) {
			error = path + ": truncated sprite header " + std::to_string(i);
			return false;
		}
		const size_t offset = headerOffset + 32;
		if (size == 0) {
			// Linked: an earlier sprite's pixels and palette.
			if (link < i) {
				const Sprite &src = sprites_[link];
				s.width = src.width;
				s.height = src.height;
				s.indices = src.indices;
				s.palette = src.palette;
			}
		} else {
			// The PCX header: encoding, bits per pixel (8 only), the image's rectangle, bytes per line.
			ByteReader pcx(file);
			pcx.seek(offset + 2);
			const uint8_t encoding = pcx.u8(), bpp = pcx.u8();
			const uint16_t x0 = pcx.u16(), y0 = pcx.u16(), x1 = pcx.u16(), y1 = pcx.u16();
			pcx.seek(offset + 66);
			const uint16_t bytesPerLine = pcx.u16();
			if (!pcx.ok() || bpp != 8) {
				logf("sff: %s: sprite %u,%u: not an 8-bit PCX", path.c_str(), s.group, s.number);
			} else {
				s.width = (uint16_t)(x1 - x0 + 1);
				s.height = (uint16_t)(y1 - y0 + 1);
				const bool shared = samePalette != 0 && prevPalette >= 0;
				const bool firstSprite = character && (prevPalette < 0 || (s.group == 0 && s.number == 0));
				const size_t dataStart = offset + 128;
				size_t dataEnd = dataStart, paletteOffset = 0;
				if (firstSprite) {
					// A character's first sprite: its palette is the block's last 768 bytes.
					const size_t blockSize = next > offset ? next - offset : size;
					if (blockSize >= 768 + 128)
						dataEnd = offset + blockSize - 768;
					paletteOffset = offset + blockSize - 768;
				} else {
					const size_t blockEnd = next > offset ? next : offset + size;
					if (shared) {
						dataEnd = blockEnd;
					} else {
						// The palette follows a 0x0C marker; search back for it from where it should be.
						size_t marker = blockEnd >= 769 ? blockEnd - 769 : dataStart;
						for (size_t pos = marker; pos >= dataStart && pos < file.size(); pos--) {
							if (file[pos] == 0x0C) {
								marker = pos;
								break;
							}
						}
						dataEnd = marker;
						paletteOffset = marker + 1;
					}
				}
				if (dataEnd < dataStart)
					dataEnd = dataStart;
				const uint8_t *rle = ByteReader(file).at(dataStart, dataEnd - dataStart);
				s.indices = decodePcxRle(rle, rle ? dataEnd - dataStart : 0, s.width, s.height,
					encoding == 1 ? bytesPerLine : 0);
				if (shared) {
					s.palette = prevPalette;
				} else {
					s.palette = (int)palettes_.size();
					palettes_.push_back(readV1Palette(file, paletteOffset));
				}
				prevPalette = s.palette;
			}
		}
		if (!byKey_.count(key(s.group, s.number)))
			byKey_[key(s.group, s.number)] = i;
		if (next == 0 || next >= file.size())
			break;
		headerOffset = next;
	}
	logf("sff: %s: SFF v1, %u sprites, %zu palettes", path.c_str(), spriteCount, palettes_.size());
	return true;
}

int Sff::setPalette(uint16_t group, uint16_t number, const Palette &palette)
{
	const auto it = paletteByKey_.find(key(group, number));
	if (it != paletteByKey_.end()) {
		palettes_[it->second] = palette;
		return it->second;
	}
	palettes_.push_back(palette);
	paletteByKey_[key(group, number)] = (int)palettes_.size() - 1;
	return (int)palettes_.size() - 1;
}

void Sff::addSprite(const Sprite &sprite)
{
	if (byKey_.count(key(sprite.group, sprite.number)))
		return;
	byKey_[key(sprite.group, sprite.number)] = sprites_.size();
	sprites_.push_back(sprite);
}

void Sff::keepOnly(const std::vector<std::pair<uint16_t, uint16_t>> &keys)
{
	std::vector<Sprite> kept;
	std::map<uint32_t, size_t> byKey;
	for (const auto &[group, number] : keys) {
		if (const Sprite *sprite = get(group, number)) {
			byKey[key(group, number)] = kept.size();
			kept.push_back(*sprite);
		}
	}
	sprites_ = std::move(kept);
	byKey_ = std::move(byKey);
}

bool readActPalette(const std::string &path, Palette &out)
{
	std::vector<uint8_t> data;
	if (!readFile(path, data))
		return false;
	out.assign(256, 0);
	const size_t count = std::min<size_t>(data.size() / 3, 256);
	for (size_t i = 0; i < count; i++) {
		const size_t dest = 255 - i;
		out[dest] = (dest == 0 ? 0u : 0xFF000000u) | (Argb)data[i * 3] << 16 | (Argb)data[i * 3 + 1] << 8 |
			data[i * 3 + 2];
	}
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
