#include "core/victory_screen.h"

#include <algorithm>

#include "core/font.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

// [Victory Screen] values for keys a screenpack leaves out (Ikemen's defaultMotif.ini).
const char *const VICTORY_DEFAULTS[][2] = {
	{ "enabled", "0" },
	{ "cpu.enabled", "1" },
	{ "vs.enabled", "1" },
	{ "fadein.time", "0" },
	{ "fadeout.time", "15" },
	{ "time", "300" },
	{ "winquote.text", "Winner!" },
	{ "winquote.layerno", "2" },
	{ "winquote.textdelay", "2" },
	{ "p1.spr", "9000, 2" },
	{ "p1.facing", "1" },
	{ "p2.spr", "9000, 2" },
	{ "p2.facing", "-1" },
};

// Every attack button and Start end it; Start alone first finishes the quote (Ikemen's cancel and skip keys).
bool doneKeys(const platform::Pad &p)
{
	return p.a || p.b || p.x || p.y || p.rb || p.rt || p.start;
}

// The first n letters of UTF-8 text.
std::string firstLetters(const std::string &line, int32_t n)
{
	size_t i = 0;
	for (int32_t count = 0; i < line.size() && count < n; count++) {
		const uint8_t c = (uint8_t)line[i];
		i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
	}
	return line.substr(0, std::min(i, line.size()));
}

int32_t letterCount(const std::string &line)
{
	int32_t count = 0;
	for (const char c : line)
		count += ((uint8_t)c & 0xC0) != 0x80;
	return count;
}

} // namespace

void VictoryScreen::load(Motif &motif)
{
	motif_ = &motif;
	motif.loadBg("victorybg", bg_, clearColour_);
	IniSection s;
	for (const auto &pair : VICTORY_DEFAULTS)
		s.values[pair[0]] = pair[1];
	if (const IniSection *own = motif.section("victory screen")) {
		for (const auto &[key, value] : own->values)
			s.values[key] = value;
	}
	int32_t v = 0;
	enabled_ = s.readInt("enabled", v) && v != 0;
	cpuEnabled_ = s.readInt("cpu.enabled", v) && v != 0;
	vsEnabled_ = s.readInt("vs.enabled", v) && v != 0;
	s.readInt("time", time_);
	fadeIn_.read("fadein.", s);
	fadeOut_.read("fadeout.", s);
	quote_.read("winquote.", s, 0, 2);
	defaultQuote_ = s.text("winquote.text");
	s.readFloat("winquote.textdelay", textDelay_);
	readSlot(slots_[0], "p1.", s);
	readSlot(slots_[1], "p2.", s);
	for (TextLayout *t : { &quote_, &slots_[0].name, &slots_[1].name })
		motif.resolveFont(*t);
}

void VictoryScreen::readSlot(Slot &slot, const std::string &p, const IniSection &s)
{
	s.ints(p + "spr", { &slot.spr[0], &slot.spr[1] });
	slot.face.read(p, s);
	int32_t v = 0;
	slot.applyPal = s.readInt(p + "applypal", v) && v != 0;
	s.readInt(p + "lose.brightness", slot.loseBrightness);
	slot.name.read(p + "name.", s);
}

bool VictoryScreen::wanted(bool computerWon, bool versusMode) const
{
	return enabled_ && (!computerWon || cpuEnabled_) && (!versusMode || vsEnabled_);
}

std::string VictoryScreen::wrapped(const std::string &line) const
{
	const auto font = motif_->fonts().find(quote_.font);
	if (!quote_.layout.hasWindow || font == motif_->fonts().end() || !font->second)
		return line;
	const float room = quote_.layout.window[2] - quote_.layout.offsetX;
	const float scale = quote_.layout.scaleX;
	std::string out, current;
	for (const std::string &word : text::split(line, ' ')) {
		const std::string tryLine = current.empty() ? word : current + " " + word;
		if (!current.empty() && (float)font->second->textWidth(tryLine, quote_.bank) * scale > room) {
			out += current + "\n";
			current = word;
		} else {
			current = tryLine;
		}
	}
	return out + current;
}

void VictoryScreen::start(const VersusScreen::Fighter fighters[2], const std::string &quote)
{
	for (int i = 0; i < 2; i++)
		slots_[i].fighter = fighters[i];
	text_ = wrapped(quote.empty() ? defaultQuote_ : quote);
	bg_.reset();
	fadeIn_.start(true);
	fadeOut_.stop();
	fadingOut_ = false;
	done_ = false;
	counter_ = 0;
	typed_ = 0;
	for (platform::Pad &p : before_) {
		p = platform::Pad{};
		p.a = p.b = p.x = p.y = p.rb = p.rt = p.start = true;
	}
	logf("victory: %s beats %s: %s", fighters[0].name.c_str(), fighters[1].name.c_str(), text_.c_str());
}

// Ikemen's MotifVictory.step: type the quote; Start finishes it, then ends the screen; time ends it once typed.
void VictoryScreen::update(const platform::Pad &pad1, const platform::Pad &pad2)
{
	if (done_)
		return;
	bg_.update();
	if (fadeIn_.isActive())
		fadeIn_.step();
	const platform::Pad now[2] = { pad1, pad2 };
	bool skip = false, cancel = false;
	for (int i = 0; i < 2; i++) {
		skip = skip || (now[i].start && !before_[i].start);
		cancel = cancel || (doneKeys(now[i]) && !doneKeys(before_[i]) && !now[i].start);
		before_[i] = now[i];
	}
	const int32_t letters = letterCount(text_);
	const bool fullyTyped = typed_ >= letters;
	if (skip && !fullyTyped)
		typed_ = letters;
	else if (!fullyTyped && textDelay_ > 0)
		typed_ = std::min(letters, (int32_t)((float)counter_ / textDelay_) + 1);
	else if (textDelay_ <= 0)
		typed_ = letters;
	if (!fadingOut_ && (cancel || (skip && fullyTyped) || (typed_ >= letters && counter_ >= time_))) {
		fadeOut_.start(false);
		fadingOut_ = true;
		logf("victory: done at %d", (int)counter_);
	}
	if (fadingOut_) {
		fadeOut_.step();
		if (!fadeOut_.isActive())
			done_ = true;
	}
	counter_++;
}

// A slot's portrait (in the chosen palette if applypal, the loser dimmed by lose.brightness) and name.
void VictoryScreen::drawSlot(const Slot &slot, bool loser, int32_t layer)
{
	const VersusScreen::Fighter &f = slot.fighter;
	const ScreenSpace &space = motif_->space();
	if (f.renderer && f.sff && slot.spr[0] >= 0 && slot.face.layer == layer) {
		ClipRect window;
		const bool clip = slot.face.windowRect(space, window);
		if (clip)
			platform::setClip(window.x, window.y, window.width, window.height);
		SpriteDraw how;
		slot.face.apply(how, space);
		const int base = f.sff->paletteIndex(1, 1), chosen = f.sff->paletteIndex(1, (uint16_t)f.palette);
		const Sprite *sprite = f.sff->get((uint16_t)slot.spr[0], (uint16_t)slot.spr[1]);
		if (slot.applyPal && sprite && sprite->palette == base)
			how.palette = chosen;
		how.scaleX = slot.face.scaleX * f.resFix * space.scale;
		how.scaleY = slot.face.scaleY * f.resFix * space.scale;
		how.flipH = slot.face.facing < 0;
		if (loser && slot.loseBrightness != 100) {
			const uint16_t mul = (uint16_t)std::clamp(slot.loseBrightness * 256 / 100, 0, 1024);
			how.mulR = how.mulG = how.mulB = mul;
		}
		f.renderer->setPalette(-1);
		f.renderer->drawSprite(slot.spr[0], slot.spr[1], space.x(slot.face.offsetX), space.y(slot.face.offsetY), how);
		if (clip)
			platform::clearClip();
	}
	if (slot.name.layout.layer == layer)
		slot.name.draw(f.name, 0, 0, layer, motif_->fonts(), space);
}

void VictoryScreen::draw()
{
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, clearColour_);
	const View view = motif_->view();
	const ScreenSpace &space = motif_->space();
	for (int32_t layer = 0; layer <= 2; layer++) {
		if (layer < 2)
			bg_.draw(layer, view);
		drawSlot(slots_[1], true, layer);
		drawSlot(slots_[0], false, layer);
		if (quote_.layout.layer == layer) {
			ClipRect window;
			const bool clip = quote_.layout.windowRect(space, window);
			if (clip)
				platform::setClip(window.x, window.y, window.width, window.height);
			quote_.draw(firstLetters(text_, typed_), 0, 0, layer, motif_->fonts(), space);
			if (clip)
				platform::clearClip();
		}
	}
	if (fadeOut_.isActive())
		fadeOut_.draw();
	else if (fadeIn_.isActive())
		fadeIn_.draw();
}

} // namespace m360
