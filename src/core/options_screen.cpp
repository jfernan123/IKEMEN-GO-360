#include "core/options_screen.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/log.h"
#include "core/music.h"
#include "core/sound.h"
#include "core/text.h"

namespace m360 {

namespace {

// [Option Info] values for keys a screenpack leaves out (Ikemen's data/system.base.def).
const char *const OPTION_DEFAULTS[][2] = {
	{ "fadein.time", "10" },
	{ "fadeout.time", "10" },
	{ "title.font", "f-6x9.def, 0, 0, 255, 255, 255, 255" },
	{ "title.offset", "159, 19" },
	{ "title.text", "OPTIONS" },
	{ "menu.pos", "85, 33" },
	{ "menu.item.font", "f-6x9.def, 0, 1, 191, 191, 191, 255" },
	{ "menu.item.spacing", "0, 14" },
	{ "menu.item.active.font", "f-6x9.def, 0, 1, 255, 255, 255, 255" },
	{ "menu.item.value.font", "f-6x9.def, 0, -1, 191, 191, 191, 255" },
	{ "menu.item.value.offset", "150, 0" },
	{ "menu.item.value.active.font", "f-6x9.def, 0, -1, 255, 255, 255, 255" },
	{ "menu.boxcursor.visible", "1" },
	{ "menu.boxcursor.coords", "-5, -10, 154, 3" },
	{ "menu.boxcursor.col", "255, 255, 255" },
	{ "menu.boxcursor.pulse", "30, 20, 30" },
	{ "menu.boxcursor.tween.factor", "0.5" },
	{ "menu.boxbg.visible", "1" },
	{ "menu.boxbg.col", "0, 0, 0" },
	{ "menu.boxbg.alpha", "0, 128" },
	{ "menu.valuename.none", "None" },
	{ "menu.valuename.normal", "Normal" },
	{ "menu.valuename.slow", "Slow %i" },
	{ "menu.valuename.fast", "Fast %i" },
	{ "cursor.move.snd", "100, 0" },
	{ "cursor.done.snd", "100, 1" },
	{ "cancel.snd", "100, 2" },
};

// Ikemen's options menu (system.base.def's menu.itemname order), for the items mugen360 has.
const char *const OPTION_ITEMS[][2] = {
	{ "menugame", "Game Settings" },
	{ "menugame.difficulty", "Difficulty Level" },
	{ "menugame.roundtime", "Time Limit" },
	{ "menugame.lifemul", "Life" },
	{ "menugame.gamespeed", "Game Speed" },
	{ "menugame.roundsnumsingle", "Rounds to Win (Single)" },
	{ "menugame.maxdrawgames", "Max Draw Games" },
	{ "menugame.spacer1", "-" },
	{ "menugame.menusimul", "Simul Settings" },
	{ "menugame.menusimul.roundsnumsimul", "Rounds to Win (Simul)" },
	{ "menugame.menusimul.spacer1", "-" },
	{ "menugame.menusimul.back", "Back" },
	{ "menugame.back", "Back" },
	{ "menuaudio", "Audio Settings" },
	{ "menuaudio.mastervolume", "Master Volume" },
	{ "menuaudio.bgmvolume", "BGM Volume" },
	{ "menuaudio.sfxvolume", "SFX Volume" },
	{ "menuaudio.spacer1", "-" },
	{ "menuaudio.back", "Back" },
	{ "menuinput", "Input Settings" },
	{ "menuinput.spacer1", "-" },
	{ "menuinput.inputdefault", "Default" },
	{ "menuinput.back", "Back" },
	{ "spacer1", "-" },
	{ "default", "Default Values" },
	{ "spacer2", "-" },
	{ "savereturn", "Save and Return" },
	{ "return", "Return Without Saving" },
};

bool isSpacer(const std::string &name)
{
	return text::startsWith(name, "spacer");
}

OptionsScreen::Item *findChild(std::vector<OptionsScreen::Item> &items, const std::string &name)
{
	for (OptionsScreen::Item &item : items) {
		if (item.name == name)
			return &item;
	}
	return nullptr;
}

std::string upper(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
	return s;
}

std::string replaceFirst(std::string s, const std::string &what, const std::string &with)
{
	const size_t at = s.find(what);
	if (at != std::string::npos)
		s.replace(at, what.size(), with);
	return s;
}

// "col = r, g, b" as 0xRRGGBB.
uint32_t colour(const IniSection &s, const std::string &key)
{
	int32_t c[3] = { 0, 0, 0 };
	s.ints(key, { &c[0], &c[1], &c[2] });
	return (uint32_t)(c[0] & 0xFF) << 16 | (uint32_t)(c[1] & 0xFF) << 8 | (uint32_t)(c[2] & 0xFF);
}

} // namespace

bool OptionsScreen::load(Motif &motif, Settings &settings, std::string &error)
{
	motif_ = &motif;
	settings_ = &settings;
	motif.loadBg("optionbg", bg_, clearColour_);
	IniSection s;
	for (const auto &pair : OPTION_DEFAULTS)
		s.values[pair[0]] = pair[1];
	const IniSection *own = motif.section("option info");
	if (own) {
		for (const auto &[key, value] : own->values)
			s.values[key] = value;
	}
	s.floats("menu.pos", { &posX_, &posY_ });
	s.floats("menu.item.spacing", { &spacingX_, &spacingY_ });
	title_.read("title.", s);
	titleText_ = s.text("title.text");
	item_.read("menu.item.", s);
	active_.read("menu.item.active.", s);
	value_.read("menu.item.value.", s);
	valueActive_.read("menu.item.value.active.", s);
	for (TextLayout *t : { &title_, &item_, &active_, &value_, &valueActive_ })
		motif.resolveFont(*t);
	int32_t v = 0;
	titleUppercase_ = s.readInt("menu.title.uppercase", v) && v != 0;
	s.ints("menu.boxcursor.coords", { &boxCoords_[0], &boxCoords_[1], &boxCoords_[2], &boxCoords_[3] });
	boxCursor_ = s.readInt("menu.boxcursor.visible", v) && v != 0;
	boxBg_ = s.readInt("menu.boxbg.visible", v) && v != 0;
	boxCursorColour_ = colour(s, "menu.boxcursor.col");
	s.ints("menu.boxcursor.pulse", { &pulse_[0], &pulse_[1], &pulse_[2] });
	s.readFloat("menu.boxcursor.tween.factor", boxTween_);
	// "alpha = src, dst": the box's colour at src and the screen kept at dst, as a plain blend of 255 - dst.
	int32_t alpha[2] = { 0, 128 };
	s.ints("menu.boxbg.alpha", { &alpha[0], &alpha[1] });
	boxBgColour_ = (uint32_t)std::clamp(255 - alpha[1], 0, 255) << 24 | colour(s, "menu.boxbg.col");
	none_ = s.text("menu.valuename.none");
	normal_ = s.text("menu.valuename.normal");
	slow_ = s.text("menu.valuename.slow");
	fast_ = s.text("menu.valuename.fast");
	s.ints("cursor.move.snd", { &moveSnd_[0], &moveSnd_[1] });
	s.ints("cursor.done.snd", { &doneSnd_[0], &doneSnd_[1] });
	s.ints("cancel.snd", { &cancelSnd_[0], &cancelSnd_[1] });
	fadeIn_.read("fadein.", s);
	fadeOut_.read("fadeout.", s);

	// The menu tree; a screenpack can rename items with its own "menu.itemname." keys.
	menu_.clear();
	for (const auto &pair : OPTION_ITEMS) {
		const std::string path = pair[0];
		std::string display = pair[1];
		if (own && own->has("menu.itemname." + path))
			display = own->text("menu.itemname." + path);
		std::vector<Item> *level = &menu_;
		const std::vector<std::string> parts = text::split(path, '.');
		for (size_t i = 0; i < parts.size(); i++) {
			Item *found = findChild(*level, parts[i]);
			if (!found) {
				level->push_back(Item{ parts[i], i + 1 == parts.size() ? display : parts[i], {} });
				found = &level->back();
			}
			level = &found->children;
		}
	}
	if (menu_.empty()) {
		error = "no options";
		return false;
	}
	reset();
	return true;
}

void OptionsScreen::reset()
{
	levels_.clear();
	Level top;
	top.items = &menu_;
	top.title = titleText_;
	levels_.push_back(top);
	bg_.reset();
	fadeIn_.start(true);
	fadeOut_.stop();
	closing_ = done_ = false;
	cursorPlaced_ = false;
	cursorY_ = pulsePhase_ = 0;
	for (bool &h : held_)
		h = true;
}

bool OptionsScreen::pressed(bool now, bool &before) const
{
	const bool press = now && !before;
	before = now;
	return press;
}

const std::string &OptionsScreen::selected() const
{
	const Level &level = levels_.back();
	return (*level.items)[(size_t)level.item].name;
}

// Ikemen's f_menuCommonCalc: up / down wrap, stepping over spacers.
void OptionsScreen::move(Level &level, int32_t direction)
{
	const int32_t count = (int32_t)level.items->size();
	for (int32_t tries = 0; tries < count; tries++) {
		level.item = (level.item + direction + count) % count;
		if (!isSpacer((*level.items)[(size_t)level.item].name))
			break;
	}
}

// Ikemen's t_vardisplay for the items mugen360 has.
std::string OptionsScreen::valueText(const std::string &name) const
{
	const Settings &s = *settings_;
	if (name == "difficulty")
		return std::to_string(s.difficulty);
	if (name == "roundtime")
		return s.time < 0 ? none_ : std::to_string(s.time);
	if (name == "lifemul")
		return std::to_string(s.life) + "%";
	if (name == "gamespeed")
		return s.gameSpeed == 0 ? normal_
			: s.gameSpeed < 0   ? replaceFirst(slow_, "%i", std::to_string(-s.gameSpeed))
								: replaceFirst(fast_, "%i", std::to_string(s.gameSpeed));
	if (name == "roundsnumsingle")
		return std::to_string(s.matchWins);
	if (name == "maxdrawgames")
		return std::to_string(s.maxDrawGames);
	if (name == "roundsnumsimul")
		return std::to_string(s.simulWins);
	if (name == "mastervolume")
		return std::to_string(s.masterVolume) + "%";
	if (name == "bgmvolume")
		return std::to_string(s.bgmVolume) + "%";
	if (name == "sfxvolume")
		return std::to_string(s.wavVolume) + "%";
	return {};
}

// Ikemen's t_itemname add / subtract with its limits; false when the value can't move that way.
bool OptionsScreen::adjust(const std::string &name, int32_t direction)
{
	Settings &s = *settings_;
	const auto step = [direction](int32_t &value, int32_t lo, int32_t hi, int32_t by) {
		const int32_t next = value + direction * by;
		if (next < lo || next > hi)
			return false;
		value = next;
		return true;
	};
	bool changed = false;
	if (name == "difficulty")
		changed = step(s.difficulty, 1, 8, 1);
	else if (name == "roundtime")
		changed = step(s.time, -1, 1000, 1);
	else if (name == "lifemul")
		changed = step(s.life, 10, 300, 10);
	else if (name == "gamespeed")
		changed = step(s.gameSpeed, -9, 9, 1);
	else if (name == "roundsnumsingle")
		changed = step(s.matchWins, 1, 10, 1);
	else if (name == "maxdrawgames")
		changed = step(s.maxDrawGames, 0, 10, 1);
	else if (name == "roundsnumsimul")
		changed = step(s.simulWins, 1, 10, 1);
	else if (name == "mastervolume")
		changed = step(s.masterVolume, 0, 200, 1);
	else if (name == "bgmvolume")
		changed = step(s.bgmVolume, 0, 100, 1);
	else if (name == "sfxvolume")
		changed = step(s.wavVolume, 0, 100, 1);
	if (changed) {
		motif_->play(moveSnd_);
		applyVolumes();
	}
	return changed;
}

void OptionsScreen::applyVolumes() const
{
	Music::setVolumes(settings_->masterVolume, settings_->bgmVolume);
	SoundBank::setVolume(settings_->soundGain());
}

void OptionsScreen::run(const std::string &name)
{
	Settings &s = *settings_;
	if (name == "default") {
		// Ikemen's 'default': the game options back to their defaults; the buttons stay.
		Settings defaults;
		uint8_t buttons[2][Settings::MUGEN_BUTTONS];
		std::copy(&s.buttons[0][0], &s.buttons[0][0] + 2 * Settings::MUGEN_BUTTONS, &buttons[0][0]);
		s = defaults;
		std::copy(&buttons[0][0], &buttons[0][0] + 2 * Settings::MUGEN_BUTTONS, &s.buttons[0][0]);
		motif_->play(doneSnd_);
		applyVolumes();
		logf("options: default values");
	} else if (name == "inputdefault") {
		const Settings defaults;
		std::copy(&defaults.buttons[0][0], &defaults.buttons[0][0] + 2 * Settings::MUGEN_BUTTONS, &s.buttons[0][0]);
		motif_->play(doneSnd_);
		logf("options: default buttons");
	} else if (name == "savereturn") {
		motif_->play(doneSnd_);
		s.save(savePath_);
		fadeOut_.start(false);
		closing_ = true;
	} else if (name == "return") {
		motif_->play(cancelSnd_);
		logf("options: return without saving");
		fadeOut_.start(false);
		closing_ = true;
	}
}

void OptionsScreen::update(const platform::Pad &pad1, const platform::Pad &pad2)
{
	bg_.update();
	const auto done = [](const platform::Pad &p) { return p.a || p.b || p.x || p.y || p.rb || p.rt || p.start; };
	const bool up = pressed(pad1.up || pad2.up, held_[0]);
	const bool down = pressed(pad1.down || pad2.down, held_[1]);
	const bool left = pressed(pad1.left || pad2.left, held_[2]);
	const bool right = pressed(pad1.right || pad2.right, held_[3]);
	const bool confirm = pressed(done(pad1) || done(pad2), held_[4]);
	const bool cancel = pressed(pad1.back || pad2.back, held_[5]);
	if (fadeIn_.isActive())
		fadeIn_.step();
	if (closing_) {
		fadeOut_.step();
		if (!fadeOut_.isActive())
			done_ = true;
		return;
	}
	if (fadeIn_.isActive())
		return;
	Level &level = levels_.back();
	if (up || down) {
		motif_->play(moveSnd_);
		move(level, down ? 1 : -1);
		return;
	}
	const Item &item = (*level.items)[(size_t)level.item];
	if (left || right) {
		if (adjust(item.name, right ? 1 : -1))
			logf("options: %s %s", item.name.c_str(), valueText(item.name).c_str());
		return;
	}
	if (cancel) {
		motif_->play(cancelSnd_);
		if (levels_.size() > 1) {
			levels_.pop_back();
		} else {
			// Esc at the top returns without saving (Ikemen leaves the save commented out there).
			logf("options: return without saving");
			fadeOut_.start(false);
			closing_ = true;
		}
		return;
	}
	if (!confirm)
		return;
	if (item.name == "back") {
		motif_->play(cancelSnd_);
		levels_.pop_back();
	} else if (!item.children.empty()) {
		motif_->play(doneSnd_);
		Level sub;
		sub.items = &item.children;
		sub.title = titleUppercase_ ? upper(item.display) : item.display;
		while (isSpacer((*sub.items)[(size_t)sub.item].name))
			sub.item++;
		levels_.push_back(sub);
	} else {
		run(item.name);
	}
}

void OptionsScreen::draw()
{
	Motif &m = *motif_;
	const ScreenSpace &space = m.space();
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, clearColour_);
	const View view = m.view();
	bg_.draw(0, view);
	const Level &level = levels_.back();
	const std::vector<Item> &items = *level.items;
	const int32_t count = (int32_t)items.size();

	// The box behind the items (Ikemen's boxbg), as tall as the rows shown.
	const float boxW = (float)(boxCoords_[2] - boxCoords_[0] + 1), boxH = (float)(boxCoords_[3] - boxCoords_[1] + 1);
	if (boxBg_) {
		platform::fillRect(space.x(posX_ + (float)boxCoords_[0]), space.y(posY_ + (float)boxCoords_[1]), boxW * space.scale,
			(boxH + (float)(count - 1) * spacingY_) * space.scale, boxBgColour_);
	}
	title_.draw(level.title, 0, 0, title_.layout.layer, m.fonts(), space);

	// The rows: label, and the value at value.offset; the chosen one in the active fonts.
	for (int pass = 0; pass < 2; pass++) {
		for (int32_t i = 0; i < count; i++) {
			const bool active = i == level.item;
			if (active != (pass == 1))
				continue;
			const Item &it = items[(size_t)i];
			const float x = posX_ + (float)i * spacingX_, y = posY_ + (float)i * spacingY_;
			const TextLayout &label = active ? active_ : item_;
			if (!isSpacer(it.name))
				label.draw(it.display, x, y, label.layout.layer, m.fonts(), space);
			const std::string value = valueText(it.name);
			const TextLayout &valueFont = active ? valueActive_ : value_;
			if (!value.empty())
				valueFont.draw(value, x, y, valueFont.layout.layer, m.fonts(), space);
		}
	}

	// The cursor box, eased to the chosen row, its alpha pulsing (Ikemen's boxcursor and Rect.SetAlphaPulse).
	const float targetY = posY_ + (float)boxCoords_[1] + (float)level.item * spacingY_;
	if (!cursorPlaced_ || boxTween_ <= 0) {
		cursorY_ = targetY;
		cursorPlaced_ = true;
	} else {
		cursorY_ += (targetY - cursorY_) * std::min(boxTween_, 1.0f);
		if (std::fabs(targetY - cursorY_) < 1)
			cursorY_ = targetY;
	}
	if (pulse_[2] > 0)
		pulsePhase_ += 2 * 3.14159265f / (float)pulse_[2];
	if (boxCursor_ && !fadeIn_.isActive() && !fadeOut_.isActive()) {
		const float alpha = (float)pulse_[0] + (float)pulse_[1] * std::sin(pulsePhase_);
		const uint32_t a = (uint32_t)std::clamp((int)std::lround(alpha), 0, 255);
		platform::fillRect(space.x(posX_ + (float)boxCoords_[0] + (float)level.item * spacingX_), space.y(cursorY_),
			boxW * space.scale, boxH * space.scale, a << 24 | boxCursorColour_);
	}
	bg_.draw(1, view);
	if (fadeOut_.isActive())
		fadeOut_.draw();
	else if (fadeIn_.isActive())
		fadeIn_.draw();
}

} // namespace m360
