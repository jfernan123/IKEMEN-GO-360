#include "core/font.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/bytes.h"
#include "core/file.h"
#include "core/ini.h"
#include "core/log.h"
#include "core/text.h"

#include "stb_truetype/stb_truetype.h"

namespace m360 {

struct Font::TrueType {
	std::vector<uint8_t> bytes;
	stbtt_fontinfo info;
};

namespace {

// "0x41" or "A": the character a map line names.
uint32_t mapCharacter(const std::string &token)
{
	if (token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
		uint32_t c = 0;
		for (size_t i = 2; i < token.size(); i++) {
			const char h = (char)std::tolower((unsigned char)token[i]);
			if (h >= '0' && h <= '9')
				c = c << 4 | (uint32_t)(h - '0');
			else if (h >= 'a' && h <= 'f')
				c = c << 4 | (uint32_t)(h - 'a' + 10);
			else
				break;
		}
		return c;
	}
	return (uint8_t)token[0];
}

// A line's words, split on spaces and tabs.
std::vector<std::string> words(const std::string &line)
{
	std::vector<std::string> out;
	size_t i = 0;
	while (i < line.size()) {
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
			i++;
		const size_t start = i;
		while (i < line.size() && line[i] != ' ' && line[i] != '\t')
			i++;
		if (i > start)
			out.push_back(line.substr(start, i - start));
	}
	return out;
}

// The lines of a text section ("[Map]"), comments removed, until the next section.
std::vector<std::string> sectionLines(const std::string &text, const std::string &section)
{
	std::vector<std::string> out;
	bool inside = false;
	for (const std::string &raw : text::splitLines(text)) {
		const std::string line(text::trim(raw));
		if (!line.empty() && line.front() == '[') {
			inside = text::toLower(line) == "[" + section + "]";
			continue;
		}
		if (inside) {
			const std::string content(text::trim(line.substr(0, line.find(';'))));
			if (!content.empty())
				out.push_back(content);
		}
	}
	return out;
}

} // namespace

bool Font::load(const std::string &path, std::string &error)
{
	std::vector<uint8_t> file;
	if (!readFile(path, file)) {
		error = "can't read " + path;
		return false;
	}
	const bool ok = file.size() >= 12 && std::memcmp(file.data(), "ElecbyteFnt\0", 12) == 0
		? loadV1(file, path, error)
		: loadV2(std::string(file.begin(), file.end()), path, error);
	if (!ok)
		return false;
	renderer_ = std::make_unique<SpriteRenderer>(glyphs_);
	return true;
}

void Font::readDef(const std::string &text)
{
	const std::vector<IniSection> ini = parseIni(text);
	const IniSection *def = findSection(ini, "def");
	if (!def)
		return;
	type_ = text::toLower(def->text("type"));
	spriteBanks_ = text::toLower(def->text("banktype")) == "sprite";
	def->ints("size", { &sizeX_, &sizeY_ });
	def->ints("spacing", { &spacingX_, &spacingY_ });
	def->ints("offset", { &offsetX_, &offsetY_ });
	def->readInt("colors", colors_);
	colors_ = std::clamp(colors_, 1, 255);
}

// FNT v1: header, a PCX holding every glyph in a row, and the text with [Def] and [Map] (Ikemen's loadFntV1).
bool Font::loadV1(const std::vector<uint8_t> &file, const std::string &path, std::string &error)
{
	ByteReader r(file);
	r.seek(12);
	r.u16(); // version
	r.u16();
	const uint32_t pcxOffset = r.u32(), pcxLength = r.u32(), textOffset = r.u32(), textLength = r.u32();
	ByteReader pcx(file);
	pcx.seek(pcxOffset + 2);
	const uint8_t encoding = pcx.u8(), bpp = pcx.u8();
	const uint16_t x0 = pcx.u16(), y0 = pcx.u16(), x1 = pcx.u16(), y1 = pcx.u16();
	pcx.seek(pcxOffset + 66);
	const uint16_t bytesPerLine = pcx.u16();
	const uint8_t *text = ByteReader(file).at(textOffset, textLength);
	if (!r.ok() || !pcx.ok() || bpp != 8 || !text || pcxLength < 128 + 768) {
		error = path + ": not a readable FNT v1 font";
		return false;
	}
	const int width = x1 - x0 + 1, height = y1 - y0 + 1;
	const size_t pixelBytes = pcxLength - 128 - 768;
	const std::vector<uint8_t> image = decodePcxRle(ByteReader(file).at(pcxOffset + 128, pixelBytes), pixelBytes,
		width, height, encoding == 1 ? bytesPerLine : 0);
	Palette base(256, 0);
	if (const uint8_t *rgb = ByteReader(file).at(pcxOffset + 128 + pixelBytes, 768)) {
		for (int c = 0; c < 256; c++)
			base[c] = (c == 0 ? 0u : 0xFF000000u) | (Argb)rgb[c * 3] << 16 | (Argb)rgb[c * 3 + 1] << 8 | rgb[c * 3 + 2];
	}

	const std::string defText((const char *)text, textLength);
	readDef(defText);
	// Banks: the palette's last colours, `colors` (rounded up to 16) at a time, swapped into the top of the palette.
	const int span = std::min(255, (int)std::ceil(colors_ / 16.0) * 16);
	banks_ = std::max(1, 255 / span);
	for (int b = 0; b < banks_; b++) {
		Palette p = base;
		for (int c = 0; c < span; c++)
			p[256 - span + c] = base[256 - span * (b + 1) + c];
		glyphs_.setPalette(0, (uint16_t)b, p);
	}

	// The map: "char [offset [width]]", each glyph starting where the last one ended unless an offset is given.
	int32_t offset = 0;
	for (const std::string &line : sectionLines(defText, "map")) {
		const std::vector<std::string> parts = words(line);
		if (parts.empty())
			continue;
		const uint32_t c = mapCharacter(parts[0]);
		if (parts.size() > 1)
			offset = text::atoi(parts[1]);
		int32_t w = sizeX_;
		if (parts.size() > 2) {
			w = text::atoi(parts[2]);
			if (w < 0) {
				offset += offset - w;
				w = -w;
			}
		}
		Sprite glyph;
		glyph.group = 0;
		glyph.number = (uint16_t)c;
		glyph.width = (uint16_t)std::max(0, w);
		glyph.height = (uint16_t)height;
		glyph.palette = glyphs_.paletteIndex(0, 0);
		glyph.indices.assign((size_t)glyph.width * glyph.height, 0);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < glyph.width && offset + x < width; x++)
				glyph.indices[(size_t)y * glyph.width + x] = image[(size_t)y * width + offset + x];
		}
		glyphs_.addSprite(glyph);
		offset += w;
	}
	return true;
}

// FNT v2: a .def whose [Def] names an SFF of glyphs (Ikemen's loadFntV2, LoadFntSff).
bool Font::loadV2(const std::string &text, const std::string &path, std::string &error)
{
	readDef(text);
	if (type_ == "truetype")
		return loadTrueType(text, path, error);
	const std::vector<IniSection> ini = parseIni(text);
	const IniSection *def = findSection(ini, "def");
	const std::string file = def ? def->text("file") : std::string();
	if (file.empty()) {
		error = path + ": no glyph file";
		return false;
	}
	// Next to the .def, else under font/ or data/.
	for (const std::string &candidate : { directoryOf(path) + file, "data/font/" + file, "font/" + file, "data/" + file }) {
		if (fileExists(candidate))
			return glyphs_.load(candidate, error);
	}
	error = path + ": can't find " + file;
	return false;
}

Font::Font() = default;

Font::~Font()
{
	for (auto &[key, g] : glyphCache_) {
		if (g.texture)
			platform::destroyTexture(g.texture);
	}
}

// Ikemen's LoadFntTtf: the .ttf found as SearchFile finds it (next to the .def, the motif, as given, data/, font/).
bool Font::loadTrueType(const std::string &text, const std::string &path, std::string &error)
{
	const std::vector<IniSection> ini = parseIni(text);
	const IniSection *def = findSection(ini, "def");
	const std::string file = def ? def->text("file") : std::string();
	auto ttf = std::make_unique<TrueType>();
	for (const std::string &candidate : { directoryOf(path) + file, "data/font/" + file, "font/" + file, "data/" + file, file }) {
		if (!file.empty() && readFile(candidate, ttf->bytes))
			break;
	}
	if (ttf->bytes.empty() || !stbtt_InitFont(&ttf->info, ttf->bytes.data(), stbtt_GetFontOffsetForIndex(ttf->bytes.data(), 0))) {
		error = path + ": can't load TrueType font " + file;
		return false;
	}
	if (sizeY_ <= 0)
		sizeY_ = 16;
	ttf_ = std::move(ttf);
	return true;
}

// A glyph rasterized at pixelHeight, coloured: white coverage times the colour, premultiplied as the renderer's
// textures are (see platform::Blend).
const Font::Glyph &Font::glyph(uint32_t code, int pixelHeight, uint32_t colour) const
{
	const auto key = std::make_tuple(code, pixelHeight, colour);
	const auto found = glyphCache_.find(key);
	if (found != glyphCache_.end())
		return found->second;
	Glyph &g = glyphCache_[key];
	const float scale = stbtt_ScaleForPixelHeight(&ttf_->info, (float)pixelHeight);
	int advance = 0, bearing = 0;
	stbtt_GetCodepointHMetrics(&ttf_->info, (int)code, &advance, &bearing);
	g.advance = (float)advance * scale;
	unsigned char *bitmap = stbtt_GetCodepointBitmap(&ttf_->info, scale, scale, (int)code, &g.width, &g.height,
		&g.offsetX, &g.offsetY);
	if (bitmap && g.width > 0 && g.height > 0) {
		const uint32_t a0 = colour >> 24, r = colour >> 16 & 0xFF, gr = colour >> 8 & 0xFF, b = colour & 0xFF;
		std::vector<uint32_t> pixels((size_t)g.width * (size_t)g.height);
		for (size_t i = 0; i < pixels.size(); i++) {
			const uint32_t a = bitmap[i] * a0 / 255;
			pixels[i] = a << 24 | (r * a / 255) << 16 | (gr * a / 255) << 8 | (b * a / 255);
		}
		g.texture = platform::createTexture(g.width, g.height, pixels.data());
	}
	if (bitmap)
		stbtt_FreeBitmap(bitmap, nullptr);
	return g;
}

// UTF-8 to code points; a broken sequence reads as its bytes.
static std::vector<uint32_t> codePoints(const std::string &text)
{
	std::vector<uint32_t> out;
	for (size_t i = 0; i < text.size();) {
		const uint8_t c = (uint8_t)text[i];
		int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
		if (i + (size_t)extra >= text.size())
			extra = 0;
		uint32_t code = extra == 0 ? c : c & (0x3F >> extra);
		for (int k = 1; k <= extra; k++)
			code = code << 6 | ((uint8_t)text[i + (size_t)k] & 0x3F);
		out.push_back(code);
		i += (size_t)extra + 1;
	}
	return out;
}

float Font::trueTypeWidth(const std::string &text) const
{
	const float scale = stbtt_ScaleForPixelHeight(&ttf_->info, (float)sizeY_);
	float width = 0;
	for (uint32_t code : codePoints(text)) {
		int advance = 0, bearing = 0;
		stbtt_GetCodepointHMetrics(&ttf_->info, (int)code, &advance, &bearing);
		width += (float)advance * scale;
	}
	return width;
}

// Ikemen's Printf: aligned by the line's width, each glyph at its bearing from the baseline at y.
void Font::drawTrueType(const std::string &text, float x, float y, float scaleX, float scaleY, int align,
	uint32_t colour) const
{
	const float width = trueTypeWidth(text) * scaleX;
	if (align == 0)
		x -= width * 0.5f;
	else if (align < 0)
		x -= width;
	x += (float)offsetX_ * scaleX;
	// Rasterized at the height it's drawn, so it stays sharp; positions scale from that size back to the font's.
	const int pixelHeight = std::max(1, (int)std::lround((float)sizeY_ * scaleY));
	const float toScreenX = scaleX * (float)sizeY_ / (float)pixelHeight, toScreenY = scaleY * (float)sizeY_ / (float)pixelHeight;
	for (uint32_t code : codePoints(text)) {
		const Glyph &g = glyph(code, pixelHeight, colour);
		if (g.texture)
			platform::drawTexture(g.texture, x + (float)g.offsetX * toScreenX, y + (float)g.offsetY * toScreenY,
				(float)g.width * toScreenX, (float)g.height * toScreenY, platform::Blend::Over);
		x += g.advance * toScreenX;
	}
}

int Font::paletteFor(int32_t bank) const
{
	if (spriteBanks_)
		return -1;
	int index = glyphs_.paletteIndex(0, (uint16_t)std::max(0, bank));
	if (index < 0)
		index = glyphs_.paletteIndex(0, 0);
	return index;
}

int32_t Font::charWidth(uint32_t c, int32_t bank) const
{
	if (c == ' ')
		return sizeX_;
	const Sprite *s = glyphs_.get((uint16_t)glyphGroup(bank), (uint16_t)c);
	return s ? s->width : 0;
}

int32_t Font::textWidth(const std::string &text, int32_t bank) const
{
	if (ttf_)
		return (int32_t)std::lround(trueTypeWidth(text));
	int32_t w = 0;
	for (size_t i = 0; i < text.size(); i++) {
		const uint8_t c = (uint8_t)text[i];
		// A character the font lacks counts as a space.
		const int32_t cw = glyphs_.get((uint16_t)glyphGroup(bank), c) || c == ' ' ? charWidth(c, bank) : sizeX_;
		// MUGEN skips a character whose width the negative spacing cancels out.
		if (cw + spacingX_ > 0) {
			w += cw;
			if (i + 1 < text.size())
				w += spacingX_;
		}
	}
	return w;
}

void Font::draw(const std::string &text, float x, float y, float scaleX, float scaleY, int32_t bank, int align,
	uint32_t colour, const SpriteDraw &effects) const
{
	if (ttf_ && !text.empty() && scaleX != 0 && scaleY != 0) {
		drawTrueType(text, x, y, scaleX, scaleY, align, colour);
		return;
	}
	if (!renderer_ || text.empty() || scaleX == 0 || scaleY == 0)
		return;
	if (!spriteBanks_ && (bank < 0 || bank >= std::max(banks_, 1) + 64))
		bank = 0;
	x += offsetX_ * scaleX;
	y += (offsetY_ - sizeY_ + 1) * scaleY;
	if (align == 0)
		x -= textWidth(text, bank) * scaleX * 0.5f;
	else if (align < 0)
		x -= textWidth(text, bank) * scaleX;
	renderer_->setPalette(paletteFor(bank));
	SpriteDraw how = effects;
	how.scaleX = scaleX;
	how.scaleY = scaleY;
	how.mulR = (uint16_t)(effects.mulR * (colour >> 16 & 0xFF) / 255);
	how.mulG = (uint16_t)(effects.mulG * (colour >> 8 & 0xFF) / 255);
	how.mulB = (uint16_t)(effects.mulB * (colour & 0xFF) / 255);
	const uint8_t alpha = (uint8_t)(colour >> 24);
	if (alpha < 255) {
		how.trans = Trans::Add;
		how.srcAlpha = alpha;
		how.dstAlpha = (uint8_t)(255 - alpha);
	}
	const int32_t group = glyphGroup(bank);
	for (const char ch : text) {
		const uint8_t c = (uint8_t)ch;
		const Sprite *s = c == ' ' ? nullptr : glyphs_.get((uint16_t)group, c);
		if (s)
			renderer_->drawSprite(group, c, x, y, how);
		x += (s ? s->width : sizeX_) * scaleX + spacingX_ * scaleX;
	}
}

} // namespace m360
