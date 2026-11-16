#include "core/pause_menu.h"

#include <algorithm>
#include <cmath>

#include "core/file.h"
#include "core/font.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

// [Pause Menu] values for keys a screenpack leaves out (Ikemen's defaultMotif.ini), in its 320x240 localcoord.
const char *const PAUSE_DEFAULTS[][2] = {
	{ "title.font", "f-6x9.def, 0, 0, 255, 255, 255, 255" },
	{ "title.offset", "159, 19" },
	{ "title.text", "PAUSE" },
	{ "title.layerno", "2" },
	{ "menu.pos", "85, 33" },
	{ "menu.item.font", "f-6x9.def, 0, 1, 191, 191, 191, 255" },
	{ "menu.item.spacing", "0, 14" },
	{ "menu.item.layerno", "2" },
	{ "menu.item.active.font", "f-6x9.def, 0, 1, 255, 255, 255, 255" },
	{ "menu.item.active.layerno", "2" },
	{ "menu.boxcursor.visible", "1" },
	{ "menu.boxcursor.coords", "-5, -10, 154, 3" },
	{ "menu.boxcursor.col", "255, 255, 255" },
	{ "menu.boxcursor.pulse", "30, 20, 30" },
	{ "menu.boxbg.visible", "1" },
	{ "menu.boxbg.col", "0, 0, 0" },
	{ "menu.boxbg.alpha", "0, 128" },
	{ "cursor.move.snd", "100, 0" },
	{ "cursor.done.snd", "100, 1" },
	{ "cancel.snd", "100, 2" },
	{ "enter.snd", "100, 1" },
	{ "exit.snd", "100, 2" },
	{ "hidebars", "1" },
	{ "overlay.col", "0, 0, 0" },
	{ "overlay.alpha", "0, 128" },
	{ "movelist.pos", "10, 20" },
	{ "movelist.title.font", "Open_Sans.def, 0, 0, 255, 255, 255, 255" },
	{ "movelist.title.offset", "150, 0" },
	{ "movelist.title.scale", "0.4, 0.4" },
	{ "movelist.title.text", "%s" },
	{ "movelist.title.layerno", "2" },
	{ "movelist.text.font", "Open_Sans.def, 0, 1, 255, 255, 255, 255" },
	{ "movelist.text.offset", "0, 12" },
	{ "movelist.text.scale", "0.4, 0.4" },
	{ "movelist.text.text", "Command List not found." },
	{ "movelist.text.layerno", "2" },
	{ "movelist.text.spacing", "1, 1" },
	{ "movelist.glyphs.offset", "0, 2" },
	{ "movelist.glyphs.scale", "1.0, 1.0" },
	{ "movelist.glyphs.spacing", "2, 0" },
	{ "movelist.window.visibleitems", "18" },
	{ "movelist.window.width", "300" },
	{ "movelist.overlay.col", "0, 0, 0" },
	{ "movelist.overlay.alpha", "0, 128" },
};
// The items (Ikemen's menu.itemname.*), those mugen360 can do.
const char *const PAUSE_ITEMS[][2] = {
	{ "back", "Continue" },
	{ "commandlist", "Command List" },
	{ "characterchange", "Character Change" },
	{ "exit", "Exit" },
};
// Ikemen's [Glyphs] (defaultMotif.ini): movelist tokens and their sprites in glyphs.sff.
const struct {
	const char *token;
	uint16_t group;
} GLYPHS[] = {
	{ "^A", 1 }, { "^B", 2 }, { "^C", 3 }, { "^D", 4 }, { "^W", 23 }, { "^X", 24 }, { "^Y", 25 }, { "^Z", 26 },
	{ "_+", 39 }, { "_.", 40 }, { "_DB", 41 }, { "_D", 42 }, { "_DF", 43 }, { "_B", 44 }, { "_F", 46 }, { "_UB", 47 },
	{ "_U", 48 }, { "_UF", 49 }, { "^S", 51 }, { "^M", 52 }, { "^P", 53 }, { "^K", 54 }, { "^LP", 57 }, { "^MP", 58 },
	{ "^HP", 59 }, { "^LK", 60 }, { "^MK", 61 }, { "^HK", 62 }, { "^3K", 63 }, { "^3P", 64 }, { "^2K", 65 },
	{ "^2P", 66 }, { "_-", 90 }, { "_!", 91 }, { "~DB", 92 }, { "~D", 93 }, { "~DF", 94 }, { "~B", 95 }, { "~F", 96 },
	{ "~UB", 97 }, { "~U", 98 }, { "~UF", 99 }, { "_HCB", 100 }, { "_HUF", 101 }, { "_HCF", 102 }, { "_HUB", 103 },
	{ "_QFD", 104 }, { "_QDB", 105 }, { "_QCB", 105 }, { "_QBU", 106 }, { "_QUF", 107 }, { "_QBD", 108 },
	{ "_QDF", 109 }, { "_QCF", 109 }, { "_QFU", 110 }, { "_QUB", 111 }, { "_FDF", 112 }, { "_FUB", 113 },
	{ "_FUF", 114 }, { "_FDB", 115 }, { "_XFF", 116 }, { "_XBB", 117 }, { "_DSF", 118 }, { "_DSB", 119 },
	{ "_AIR", 121 }, { "_TAP", 122 }, { "_MAX", 123 }, { "_EX", 124 }, { "_^", 127 }, { "_=", 128 }, { "_)", 129 },
	{ "_(", 130 }, { "_`", 135 }, { "_CHARGE", 136 }, { "_HOLD", 137 }, { "_RELEASE", 138 }, { "_MASH", 139 },
	{ "_CLOSE", 140 }, { "_FAR", 141 }, { "_AIROK", 142 },
};

// "r, g, b" with "alpha = src, dst" as one colour: dst/256 of the screen kept, as a plain blend.
uint32_t blendColour(const IniSection &s, const std::string &prefix)
{
	int32_t c[3] = { 0, 0, 0 }, alpha[2] = { 255, 0 };
	s.ints(prefix + "col", { &c[0], &c[1], &c[2] });
	s.ints(prefix + "alpha", { &alpha[0], &alpha[1] });
	return (uint32_t)std::clamp(255 - alpha[1], 0, 255) << 24 | (uint32_t)(c[0] & 0xFF) << 16 |
		(uint32_t)(c[1] & 0xFF) << 8 | (uint32_t)(c[2] & 0xFF);
}

// "<#rrggbb>" as a colour.
uint32_t hexColour(const std::string &hex)
{
	uint32_t value = 0;
	for (char c : hex) {
		const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
			: c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (digit < 0)
			break;
		value = value << 4 | (uint32_t)digit;
	}
	return 0xFF000000u | (value & 0xFFFFFF);
}

} // namespace

bool PauseMenu::load(Motif &motif)
{
	IniSection s;
	for (const auto &pair : PAUSE_DEFAULTS)
		s.values[pair[0]] = pair[1];
	const IniSection *own = motif.section("pause menu");
	if (own) {
		for (const auto &[key, value] : own->values)
			s.values[key] = value;
		int32_t enabled = 1;
		if (own->readInt("enabled", enabled) && enabled == 0)
			return false;
	}
	// A screenpack's own [Pause Menu] is in its localcoord (ikemen1's is 1280x720); Ikemen's defaults are in 320x240.
	int32_t localW = own ? motif.localWidth() : 320, localH = own ? motif.localHeight() : 240;
	s.ints("menu.item.localcoord", { &localW, &localH });
	space_ = ScreenSpace::forLocalcoord(localW > 0 ? localW : 320, localH > 0 ? localH : 240);

	s.floats("menu.pos", { &posX_, &posY_ });
	float spacingX = 0;
	s.floats("menu.item.spacing", { &spacingX, &spacingY_ });
	title_.read("title.", s, 0, 2);
	itemText_.read("menu.item.", s, 1, 2);
	activeText_.read("menu.item.active.", s, 1, 2);
	movelistTitle_.read("movelist.title.", s, 0, 2);
	movelistText_.read("movelist.text.", s, 1, 2);
	for (TextLayout *t : { &title_, &itemText_, &activeText_, &movelistTitle_, &movelistText_ })
		motif.resolveFont(*t);
	int32_t flag = 1;
	s.readInt("menu.boxcursor.visible", flag);
	boxCursor_ = flag != 0;
	boxCoords_[0] = -5, boxCoords_[1] = -10, boxCoords_[2] = 154, boxCoords_[3] = 3;
	s.ints("menu.boxcursor.coords", { &boxCoords_[0], &boxCoords_[1], &boxCoords_[2], &boxCoords_[3] });
	int32_t c[3] = { 255, 255, 255 };
	s.ints("menu.boxcursor.col", { &c[0], &c[1], &c[2] });
	boxColour_ = (uint32_t)(c[0] & 0xFF) << 16 | (uint32_t)(c[1] & 0xFF) << 8 | (uint32_t)(c[2] & 0xFF);
	boxPulse_[0] = 30, boxPulse_[1] = 20, boxPulse_[2] = 30;
	s.ints("menu.boxcursor.pulse", { &boxPulse_[0], &boxPulse_[1], &boxPulse_[2] });
	boxPhase_ = 0;
	flag = 1;
	s.readInt("menu.boxbg.visible", flag);
	boxBg_ = flag != 0;
	boxBgColour_ = blendColour(s, "menu.boxbg.");
	flag = 1;
	s.readInt("hidebars", flag);
	hideBars_ = flag != 0;
	overlayColour_ = blendColour(s, "overlay.");
	movelistOverlayColour_ = blendColour(s, "movelist.overlay.");
	movelistPosX_ = 10, movelistPosY_ = 20;
	s.floats("movelist.pos", { &movelistPosX_, &movelistPosY_ });
	movelistWidth_ = 300;
	s.readFloat("movelist.window.width", movelistWidth_);
	movelistVisible_ = 18;
	s.readInt("movelist.window.visibleitems", movelistVisible_);
	textSpacingX_ = 1, textSpacingY_ = 1;
	s.floats("movelist.text.spacing", { &textSpacingX_, &textSpacingY_ });
	glyphOffsetX_ = 0, glyphOffsetY_ = 2;
	s.floats("movelist.glyphs.offset", { &glyphOffsetX_, &glyphOffsetY_ });
	glyphScaleX_ = 1, glyphScaleY_ = 1;
	s.floats("movelist.glyphs.scale", { &glyphScaleX_, &glyphScaleY_ });
	glyphSpacingX_ = 2;
	float glyphSpacingY = 0;
	s.floats("movelist.glyphs.spacing", { &glyphSpacingX_, &glyphSpacingY });
	notFound_ = s.text("movelist.text.text");
	const auto sound = [&](const char *key, int32_t out[2]) {
		out[0] = -1, out[1] = 0;
		s.ints(key, { &out[0], &out[1] });
	};
	sound("cursor.move.snd", moveSnd_);
	sound("cursor.done.snd", doneSnd_);
	sound("cancel.snd", cancelSnd_);
	sound("enter.snd", enterSnd_);
	sound("exit.snd", exitSnd_);

	// The items in the screenpack's order, else Ikemen's; only those mugen360 can do.
	items_.clear();
	if (own) {
		for (const std::string &key : own->keys) {
			if (!text::startsWith(key, "menu.itemname."))
				continue;
			const std::string name = key.substr(14);
			for (const auto &pair : PAUSE_ITEMS)
				if (name == pair[0])
					items_.push_back(Item{ name, own->text(key) });
		}
	}
	if (items_.empty())
		for (const auto &pair : PAUSE_ITEMS)
			items_.push_back(Item{ pair[0], pair[1] });

	// The glyphs, longest token first so "_QDF" wins over "_D".
	glyphs_.clear();
	for (const auto &g : GLYPHS)
		glyphs_.push_back({ g.token, { g.group, (uint16_t)0 } });
	std::stable_sort(glyphs_.begin(), glyphs_.end(),
		[](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
	std::string sffError;
	if (glyphSff_.load(searchFile("glyphs.sff", { "data/" }), sffError))
		glyphRenderer_ = std::make_unique<SpriteRenderer>(glyphSff_);
	else
		logf("pause: %s; the command list shows its tokens as text", sffError.c_str());
	motif_ = &motif;
	return true;
}

void PauseMenu::open(std::vector<Movelist> movelists)
{
	movelists_ = std::move(movelists);
	if (movelists_.empty())
		movelists_.push_back(Movelist{});
	parsed_.assign(movelists_.size(), {});
	firstLine_.assign(movelists_.size(), 0);
	for (size_t i = 0; i < movelists_.size(); i++)
		parseMovelist(i);
	shownList_ = 0;
	item_ = 0;
	commandList_ = false;
	result_ = PauseChoice::None;
	boxPhase_ = 0;
	// The button that opened it is still held.
	for (bool &h : held_)
		h = true;
	motif_->play(enterSnd_);
}

bool PauseMenu::pressed(bool now, bool &before) const
{
	const bool press = now && !before;
	before = now;
	return press;
}

// Ikemen's f_commandlistParse: glyph tokens become <token>, each tab-separated part is aligned by its leading tabs
// (none left, one centre, more right), and "<#rrggbb>" ... "</>" colour the text.
void PauseMenu::parseMovelist(size_t which)
{
	std::vector<Line> &lines = parsed_[which];
	lines.clear();
	const std::string &path = movelists_[which].path;
	std::vector<uint8_t> bytes;
	if (path.empty() || !readFile(path, bytes))
		return;
	uint32_t colour = 0;
	for (std::string line : text::splitLines(std::string(bytes.begin(), bytes.end()))) {
		while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
			line.pop_back();
		Line out;
		size_t i = 0;
		while (i < line.size()) {
			size_t tabs = 0;
			while (i < line.size() && line[i] == '\t') {
				tabs++;
				i++;
			}
			const size_t end = std::min(line.find('\t', i), line.size());
			const std::string part = line.substr(i, end - i);
			i = end;
			const int align = tabs == 0 ? 1 : tabs == 1 ? 0 : -1;
			Line pieces;
			std::string current;
			const auto flush = [&]() {
				if (!current.empty())
					pieces.push_back(Piece{ false, current, align, colour });
				current.clear();
			};
			for (size_t k = 0; k < part.size();) {
				if (part[k] == '<') {
					const size_t close = part.find('>', k);
					if (close != std::string::npos) {
						const std::string tag = part.substr(k + 1, close - k - 1);
						if (tag == "/" || tag == "#") {
							flush();
							colour = 0;
							k = close + 1;
							continue;
						}
						if (tag.size() > 1 && tag[0] == '#') {
							flush();
							colour = hexColour(tag.substr(1));
							k = close + 1;
							continue;
						}
					}
				}
				bool matched = false;
				for (const auto &[token, sprite] : glyphs_) {
					if (part.compare(k, token.size(), token) == 0) {
						flush();
						pieces.push_back(Piece{ true, token, align, colour });
						k += token.size();
						matched = true;
						break;
					}
				}
				if (!matched)
					current += part[k++];
			}
			flush();
			// A right-aligned part is laid out from its end (Ikemen inserts its pieces in reverse).
			if (align == -1)
				std::reverse(pieces.begin(), pieces.end());
			out.insert(out.end(), pieces.begin(), pieces.end());
		}
		lines.push_back(std::move(out));
	}
	while (!lines.empty() && lines.back().empty())
		lines.pop_back();
}

// Ikemen's menu.f_run with the pause menu's items, and f_commandlistRender's keys.
void PauseMenu::update(const platform::Pad &pad1, const platform::Pad &pad2)
{
	if (!motif_ || result_ != PauseChoice::None)
		return;
	const bool up = pressed(pad1.up || pad2.up, held_[0]);
	const bool down = pressed(pad1.down || pad2.down, held_[1]);
	const bool left = pressed(pad1.left || pad2.left, held_[2]);
	const bool right = pressed(pad1.right || pad2.right, held_[3]);
	const auto done = [](const platform::Pad &p) { return p.a || p.b || p.x || p.y || p.rb || p.rt; };
	const bool confirm = pressed(done(pad1) || done(pad2), held_[4]);
	const bool back = pressed(pad1.back || pad2.back, held_[5]);

	if (commandList_) {
		std::vector<Line> &lines = parsed_[shownList_];
		int32_t &first = firstLine_[shownList_];
		const int32_t count = lines.empty() ? 1 : (int32_t)lines.size();
		if (back || confirm) {
			motif_->play(cancelSnd_);
			commandList_ = false;
		} else if ((left || right) && movelists_.size() > 1) {
			motif_->play(moveSnd_);
			shownList_ = (shownList_ + (right ? 1 : movelists_.size() - 1)) % movelists_.size();
		} else if (up && first > 0) {
			motif_->play(moveSnd_);
			first--;
		} else if (down && first < count - movelistVisible_) {
			motif_->play(moveSnd_);
			first++;
		}
		return;
	}
	// Back closes it, as Continue (Ikemen's menu key).
	if (back) {
		motif_->play(exitSnd_);
		result_ = PauseChoice::Continue;
		return;
	}
	const int32_t count = (int32_t)items_.size();
	if (up || down) {
		motif_->play(moveSnd_);
		item_ = (item_ + (down ? 1 : count - 1)) % count;
	}
	if (!confirm)
		return;
	const std::string &name = items_[(size_t)item_].name;
	logf("pause: %s", items_[(size_t)item_].display.c_str());
	if (name == "back") {
		motif_->play(exitSnd_);
		result_ = PauseChoice::Continue;
	} else if (name == "commandlist") {
		motif_->play(doneSnd_);
		commandList_ = true;
	} else {
		motif_->play(doneSnd_);
		result_ = name == "characterchange" ? PauseChoice::CharacterChange : PauseChoice::Exit;
	}
}

void PauseMenu::draw()
{
	if (!motif_)
		return;
	FontTable &fonts = motif_->fonts();
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, overlayColour_);
	if (commandList_) {
		drawCommandList();
		return;
	}
	title_.draw(title_.text, 0, 0, title_.layout.layer, fonts, space_);
	const int32_t count = (int32_t)items_.size();
	// The box behind the items and the pulsing box on the picked one (Ikemen's boxbg and boxcursor).
	const float boxW = (float)(boxCoords_[2] - boxCoords_[0] + 1), boxH = (float)(boxCoords_[3] - boxCoords_[1] + 1);
	if (boxBg_ && count > 0)
		platform::fillRect(space_.x(posX_ + (float)boxCoords_[0]), space_.y(posY_ + (float)boxCoords_[1]),
			boxW * space_.scale, ((float)(count - 1) * spacingY_ + boxH) * space_.scale, boxBgColour_);
	if (boxCursor_ && count > 0) {
		if (boxPulse_[2] > 0)
			boxPhase_ += 2 * 3.14159265f / (float)boxPulse_[2];
		const float alpha = (float)boxPulse_[0] + (float)boxPulse_[1] * std::sin(boxPhase_);
		const uint32_t a = (uint32_t)std::clamp((int)std::lround(alpha), 0, 255);
		platform::fillRect(space_.x(posX_ + (float)boxCoords_[0]),
			space_.y(posY_ + (float)item_ * spacingY_ + (float)boxCoords_[1]), boxW * space_.scale, boxH * space_.scale,
			a << 24 | boxColour_);
	}
	for (int32_t i = 0; i < count; i++) {
		const TextLayout &t = i == item_ ? activeText_ : itemText_;
		t.draw(items_[(size_t)i].display, posX_, posY_ + (float)i * spacingY_, t.layout.layer, fonts, space_);
	}
}

// Ikemen's f_commandlistRender: the character's name, then its lines from the scrolled-to one, text and glyphs
// laid out left to right, centred or from the right edge of the window's width.
void PauseMenu::drawCommandList()
{
	FontTable &fonts = motif_->fonts();
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, movelistOverlayColour_);
	const Movelist &list = movelists_[shownList_];
	std::string heading = movelistTitle_.text;
	const size_t mark = heading.find("%s");
	if (mark != std::string::npos)
		heading.replace(mark, 2, list.name);
	movelistTitle_.draw(heading, movelistPosX_, movelistPosY_, movelistTitle_.layout.layer, fonts, space_);

	const auto fontIt = fonts.find(movelistText_.font);
	const Font *font = fontIt != fonts.end() ? fontIt->second.get() : nullptr;
	const float fontHeight = font ? (float)font->height() : 9;
	const float scaleY = movelistText_.layout.scaleY, scaleX = movelistText_.layout.scaleX;
	const float lineHeight =
		std::round((fontHeight + (font ? (float)font->spacingY() : 0)) * scaleY + textSpacingY_);
	const float x0 = movelistPosX_ + movelistText_.layout.offsetX, y0 = movelistPosY_ + movelistText_.layout.offsetY;

	std::vector<Line> notFound;
	const std::vector<Line> *lines = &parsed_[shownList_];
	if (lines->empty()) {
		notFound.push_back(Line{ Piece{ false, notFound_, 1, 0 } });
		lines = &notFound;
	}
	const int32_t first = firstLine_[shownList_];
	for (int32_t n = first, row = 0; n < (int32_t)lines->size() && row <= movelistVisible_; n++, row++) {
		float lengthOffset = 0;
		int align = 1;
		const float y = y0 + lineHeight * (float)row;
		const Line &line = (*lines)[(size_t)n];
		for (size_t k = 0; k < line.size(); k++) {
			const Piece &piece = line[k];
			float alignOffset = piece.align == 0 ? movelistWidth_ * 0.5f : piece.align == -1 ? movelistWidth_ : 0;
			if (piece.align != align) {
				lengthOffset = 0;
				align = piece.align;
			}
			float width = 0;
			const Sprite *glyph = nullptr;
			if (piece.glyph && glyphRenderer_) {
				for (const auto &[token, sprite] : glyphs_)
					if (token == piece.text)
						glyph = glyphSff_.get(sprite.first, sprite.second);
			}
			if (glyph && glyph->height > 0) {
				// Glyphs are as tall as the text (Ikemen: font height * text scale / glyph height).
				const float gx = fontHeight * scaleY / (float)glyph->height * glyphScaleX_;
				const float gy = fontHeight * scaleY / (float)glyph->height * glyphScaleY_;
				if (piece.align == -1)
					alignOffset -= (float)glyph->width * gx;
				SpriteDraw how;
				how.scaleX = gx * space_.scale;
				how.scaleY = gy * space_.scale;
				glyphRenderer_->drawSprite(glyph->group, glyph->number,
					space_.x(std::floor(x0 + glyphOffsetX_ + alignOffset + lengthOffset)),
					space_.y(y + glyphOffsetY_), how);
				if (k + 1 < line.size())
					width = (float)glyph->width * gx + glyphSpacingX_;
			} else {
				TextLayout t = movelistText_;
				t.align = piece.align == 0 && lengthOffset != 0 ? 1 : piece.align;
				if (piece.colour != 0)
					t.colour = piece.colour;
				t.layout.offsetX = t.layout.offsetY = 0;
				t.draw(piece.text, x0 + alignOffset + lengthOffset, y, t.layout.layer, fonts, space_);
				if (k + 1 < line.size() && font)
					width = (float)font->textWidth(piece.text, t.bank) * scaleX + textSpacingX_;
			}
			if (piece.align == 0 && !piece.glyph && lengthOffset == 0)
				lengthOffset += width / 2;
			else if (piece.align == -1)
				lengthOffset -= width;
			else
				lengthOffset += width;
		}
	}
}

} // namespace m360
