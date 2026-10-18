#include "core/title_screen.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

// [Title Info] values for keys a screenpack leaves out (Ikemen's data/system.base.def). The footer's title and
// version name this engine, as Ikemen's name Ikemen; there's no info box to point at.
const char *const TITLE_DEFAULTS[][2] = {
	{ "fadein.time", "10" },
	{ "fadein.col", "0, 0, 0" },
	{ "fadeout.time", "10" },
	{ "fadeout.col", "0, 0, 0" },
	{ "menu.pos", "159, 158" },
	{ "menu.tween.factor", "0.5" },
	{ "menu.item.font", "f-6x9.def, 0, 1, 191, 191, 191, 255" },
	{ "menu.item.spacing", "0, 13" },
	{ "menu.item.active.font", "f-6x9.def, 0, 0, 255, 255, 255, 255" },
	{ "menu.window.margins.y", "12, 8" },
	{ "menu.window.visibleitems", "5" },
	{ "cursor.move.snd", "100, 0" },
	{ "cursor.done.snd", "100, 1" },
	{ "cancel.snd", "100, 2" },
	{ "footer.title.font", "f-4x6.def, 0, 1, 191, 191, 191, 255" },
	{ "footer.title.text", "MUGEN360" },
	{ "footer.info.font", "f-4x6.def, 0, 0, 191, 191, 191, 255" },
	{ "footer.info.text", "" },
	{ "footer.version.font", "f-4x6.def, 0, -1, 191, 191, 191, 255" },
	{ "footer.version.text", "MILESTONE 10C" },
	{ "footer.overlay.col", "0, 0, 64" },
	{ "footer.overlay.alpha", "255, 100" },
};
const char *const DEFAULT_ITEMS[][2] = {
	{ "menu.itemname.arcade", "ARCADE" },
	{ "menu.itemname.versus", "VS MODE" },
	{ "menu.itemname.training", "TRAINING" },
	{ "menu.itemname.options", "OPTIONS" },
	{ "menu.itemname.exit", "EXIT" },
};

// What mugen360 can run from the menu so far; other modes are left out.
const std::set<std::string> SUPPORTED = { "arcade", "teamarcade", "versus", "teamversus", "freebattle", "training",
	"survival", "options", "exit", "back" };

// Ikemen's f_tweenStep: a factor of the way to the target, snapping once within a pixel.
float tweenStep(float value, float target, float factor)
{
	if (factor <= 0)
		return target;
	const float next = value + (target - value) * std::min(factor, 1.0f);
	return std::fabs(next - target) < 1 ? target : next;
}

TitleScreen::Item *findChild(std::vector<TitleScreen::Item> &items, const std::string &name)
{
	for (TitleScreen::Item &item : items) {
		if (item.name == name)
			return &item;
	}
	return nullptr;
}

// Drops the modes that can't run and the submenus left with nothing but "back".
void prune(std::vector<TitleScreen::Item> &items, bool top)
{
	std::vector<TitleScreen::Item> kept;
	for (TitleScreen::Item &item : items) {
		if (!item.children.empty()) {
			prune(item.children, false);
			const bool useful = std::any_of(item.children.begin(), item.children.end(),
				[](const TitleScreen::Item &child) { return child.name != "back"; });
			if (useful)
				kept.push_back(std::move(item));
		} else if (SUPPORTED.count(item.name) && !(top && item.name == "back")) {
			kept.push_back(std::move(item));
		}
	}
	items = std::move(kept);
}

} // namespace

const TitleScreen::Item *TitleScreen::singleAction(const Item &item)
{
	const Item *only = nullptr;
	for (const Item &child : item.children) {
		if (child.name == "back")
			continue;
		if (only)
			return nullptr;
		only = &child;
	}
	if (only && !only->children.empty())
		return singleAction(*only);
	return only;
}

bool TitleScreen::load(Motif &motif, std::string &error)
{
	motif_ = &motif;
	motif.loadBg("titlebg", bg_, clearColour_);
	const IniSection *own = motif.section("title info");
	if (!own) {
		error = motif.path() + " has no [Title Info]";
		return false;
	}
	// The skin's keys over the defaults.
	IniSection s;
	for (const auto &pair : TITLE_DEFAULTS)
		s.values[pair[0]] = pair[1];
	const int32_t w = motif.localWidth(), h = motif.localHeight();
	s.values["footer.title.offset"] = std::to_string(2 * w / 320) + ", " + std::to_string(h - 2);
	s.values["footer.info.offset"] = std::to_string(w / 2) + ", " + std::to_string(h - 2);
	s.values["footer.version.offset"] = std::to_string(w - 1 - 2 * w / 320) + ", " + std::to_string(h - 2);
	s.values["footer.overlay.window"] = "0, " + std::to_string(h - 7) + ", " + std::to_string(w) + ", " + std::to_string(h);
	for (const auto &[key, value] : own->values)
		s.values[key] = value;

	s.floats("menu.pos", { &posX_, &posY_ });
	s.floats("menu.item.spacing", { &spacingX_, &spacingY_ });
	s.readInt("menu.window.visibleitems", visibleItems_);
	s.floats("menu.window.margins.y", { &marginTop_, &marginBottom_ });
	s.readFloat("menu.tween.factor", tweenFactor_);
	item_.read("menu.item.", s);
	active_.read("menu.item.active.", s);
	footerTitle_.read("footer.title.", s);
	footerInfo_.read("footer.info.", s);
	footerVersion_.read("footer.version.", s);
	for (TextLayout *t : { &item_, &active_, &footerTitle_, &footerInfo_, &footerVersion_ })
		motif.resolveFont(*t);
	int32_t col[3] = { 0, 0, 0 }, alpha[2] = { 255, 0 };
	footer_ = s.ints("footer.overlay.col", { &col[0], &col[1], &col[2] });
	s.ints("footer.overlay.alpha", { &alpha[0], &alpha[1] });
	s.ints("footer.overlay.window", { &footerWindow_[0], &footerWindow_[1], &footerWindow_[2], &footerWindow_[3] });
	// "alpha = src, dst" adds the screen back at dst/255; a plain blend with alpha 255 - dst is the closest.
	footerColour_ = (uint32_t)std::clamp(255 - alpha[1], 0, 255) << 24 | (uint32_t)(col[0] & 0xFF) << 16 |
		(uint32_t)(col[1] & 0xFF) << 8 | (uint32_t)(col[2] & 0xFF);
	s.ints("cursor.move.snd", { &moveSnd_[0], &moveSnd_[1] });
	s.ints("cursor.done.snd", { &doneSnd_[0], &doneSnd_[1] });
	s.ints("cancel.snd", { &cancelSnd_[0], &cancelSnd_[1] });
	fadeIn_.read("fadein.", s);
	fadeOut_.read("fadeout.", s);

	// The menu, in the def's order: "menu.itemname.menuversus.versus = 1P VS 2P" is "1P VS 2P" under "menuversus".
	std::vector<std::pair<std::string, std::string>> names;
	for (const std::string &key : own->keys) {
		if (text::startsWith(key, "menu.itemname."))
			names.emplace_back(key.substr(14), own->text(key));
	}
	if (names.empty()) {
		for (const auto &pair : DEFAULT_ITEMS)
			names.emplace_back(std::string(pair[0]).substr(14), pair[1]);
	}
	menu_.clear();
	for (const auto &[path, display] : names) {
		std::vector<Item> *level = &menu_;
		const std::vector<std::string> parts = text::split(path, '.');
		for (size_t i = 0; i < parts.size(); i++) {
			Item *found = findChild(*level, parts[i]);
			if (!found) {
				level->push_back(Item{ parts[i], i + 1 == parts.size() ? display : parts[i], {} });
				found = &level->back();
			} else if (i + 1 == parts.size()) {
				found->display = display;
			}
			level = &found->children;
		}
	}
	prune(menu_, true);
	if (menu_.empty()) {
		error = motif.path() + ": the title menu has nothing mugen360 can run";
		return false;
	}
	reset();
	return true;
}

void TitleScreen::reset()
{
	levels_.clear();
	Level top;
	top.items = &menu_;
	levels_.push_back(top);
	bg_.reset();
	fadeIn_.start(true);
	fadeOut_.stop();
	pending_ = result_ = TitleChoice::None;
	noticeText_.clear();
	noticeTime_ = 0;
	fadeAfterNotice_ = false;
	// A button still held from the last screen doesn't count as a press.
	for (bool &h : held_)
		h = true;
}

bool TitleScreen::pressed(bool now, bool &before) const
{
	const bool press = now && !before;
	before = now;
	return press;
}

// Ikemen's f_menuCommonCalc: the cursor moves within the visible window, wrapping at the ends.
void TitleScreen::move(Level &level, int32_t direction)
{
	const int32_t count = (int32_t)level.items->size();
	const int32_t visible = visibleItems_ > 0 ? visibleItems_ : count;
	if (direction > 0) {
		level.item++;
		if (level.cursorPosY < visible)
			level.cursorPosY++;
	} else {
		level.item--;
		if (level.cursorPosY > 1)
			level.cursorPosY--;
	}
	if (level.item > count) {
		level.item = 1;
		level.cursorPosY = 1;
	} else if (level.item < 1) {
		level.item = count;
		level.cursorPosY = std::min(count, visible);
	}
}

// The window's scroll: which item is on its first row, eased there (Ikemen's menuTweenData).
void TitleScreen::scroll(Level &level)
{
	const int32_t count = (int32_t)level.items->size();
	const int32_t visible = visibleItems_ > 0 ? visibleItems_ : count;
	const int32_t maxFirst = std::max(1, count - visible + 1);
	const int32_t first = std::clamp(level.item - level.cursorPosY + 1, 1, maxFirst);
	const float target = (float)(first - 1) * spacingY_;
	if (level.targetPos != target) {
		level.targetPos = target;
		level.slideOffset = level.currentPos - level.targetPos;
	}
	if (level.snap) {
		level.currentPos = level.targetPos;
		level.slideOffset = 0;
		level.snap = false;
		return;
	}
	level.slideOffset = tweenStep(level.slideOffset, 0, tweenFactor_);
	level.currentPos = level.targetPos + level.slideOffset;
}

void TitleScreen::notice(const std::string &text, int32_t ticks)
{
	noticeText_ = text;
	noticeTime_ = ticks;
}

void TitleScreen::choose(const Item &item, const std::string &path)
{
	logf("title: %s%s", path.c_str(), item.display.c_str());
	if (item.name == "exit") {
		result_ = TitleChoice::Exit;
		return;
	}
	motif_->play(doneSnd_);
	if (item.name == "options") {
		notice("OPTIONS COME IN STAGE 11", 120);
		return;
	}
	pending_ = item.name == "arcade" ? TitleChoice::Arcade
		: item.name == "teamarcade"    ? TitleChoice::TeamArcade
		: item.name == "teamversus"    ? TitleChoice::TeamVersus
		: item.name == "training"     ? TitleChoice::Training
		: item.name == "survival"     ? TitleChoice::Survival
		: item.name == "freebattle"   ? TitleChoice::QuickMatch
										: TitleChoice::Versus;
	fadeOut_.start(false);
}

void TitleScreen::update(const platform::Pad &pad1, const platform::Pad &pad2)
{
	bg_.update();
	const bool up = pressed(pad1.up || pad1.left || pad2.up || pad2.left, held_[0]);
	const bool down = pressed(pad1.down || pad1.right || pad2.down || pad2.right, held_[1]);
	const auto done = [](const platform::Pad &p) { return p.a || p.b || p.x || p.y || p.rb || p.rt || p.start; };
	const bool confirm = pressed(done(pad1) || done(pad2), held_[2]);
	const bool cancel = pressed(pad1.back || pad2.back, held_[3]);

	if (fadeIn_.isActive())
		fadeIn_.step();
	if (fadeOut_.isActive()) {
		fadeOut_.step();
		if (!fadeOut_.isActive())
			result_ = pending_;
	} else if (pending_ != TitleChoice::None && fadeOut_.duration() == 0 && !fadeAfterNotice_) {
		result_ = pending_;
	}
	if (noticeTime_ > 0 && --noticeTime_ == 0 && fadeAfterNotice_) {
		fadeAfterNotice_ = false;
		fadeOut_.start(false);
	}
	Level &level = levels_.back();
	// While fading or once a mode is picked, the menu just keeps easing.
	if (fadeIn_.isActive() || fadeOut_.isActive() || pending_ != TitleChoice::None || result_ != TitleChoice::None) {
		scroll(level);
		return;
	}
	if (down || up) {
		motif_->play(moveSnd_);
		move(level, down ? 1 : -1);
	}
	scroll(level);
	const std::vector<Item> &items = *level.items;
	if (cancel) {
		if (levels_.size() > 1) {
			motif_->play(cancelSnd_);
			levels_.pop_back();
			levels_.back().snap = true;
		} else {
			// At the top, cancel moves to Exit (Ikemen's menu key); Exit itself is confirmed with a button.
			for (int32_t i = 0; i < (int32_t)items.size(); i++) {
				if (items[(size_t)i].name == "exit" && level.item != i + 1) {
					motif_->play(cancelSnd_);
					level.item = i + 1;
					level.cursorPosY = std::min(level.item, visibleItems_ > 0 ? visibleItems_ : (int32_t)items.size());
				}
			}
		}
		return;
	}
	if (!confirm)
		return;
	const Item &item = items[(size_t)level.item - 1];
	if (item.name == "back") {
		motif_->play(cancelSnd_);
		levels_.pop_back();
		levels_.back().snap = true;
	} else if (!item.children.empty()) {
		const std::string path = level.path + item.display + " > ";
		if (const Item *only = singleAction(item)) {
			choose(*only, path);
		} else {
			motif_->play(doneSnd_);
			Level sub;
			sub.items = &item.children;
			sub.path = path;
			levels_.push_back(sub);
		}
	} else {
		choose(item, level.path);
	}
}

void TitleScreen::draw()
{
	Motif &m = *motif_;
	const ScreenSpace &space = m.space();
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, clearColour_);
	const View view = m.view();
	bg_.draw(0, view);

	// Ikemen's f_menuCommonDraw: the rows in the window, the picked one in the active font, clipped to the window.
	const Level &level = levels_.back();
	const std::vector<Item> &items = *level.items;
	const int32_t count = (int32_t)items.size();
	const int32_t visible = visibleItems_ > 0 ? visibleItems_ : count;
	const bool tweenDone = std::fabs(level.currentPos - level.targetPos) < 1;
	int32_t shown = level.item + visible - level.cursorPosY;
	if (shown > count || (shown < count && (marginTop_ != 0 || marginBottom_ != 0)))
		shown = count;
	const auto drawItems = [&](bool front) {
		if (marginTop_ != 0 || marginBottom_ != 0) {
			const float top = std::max(0.0f, posY_ - marginTop_);
			const float bottom = posY_ + (float)(visible - 1) * spacingY_ + marginBottom_;
			platform::setClip(0, (int)space.y(top), platform::SCREEN_WIDTH, (int)((bottom - top) * space.scale));
		}
		for (int pass = 0; pass < 2; pass++) {
			for (int32_t i = 1; i <= shown; i++) {
				const bool selected = i == level.item;
				if (selected != (pass == 1) || !(i > level.item - level.cursorPosY || !tweenDone))
					continue;
				const TextLayout &t = selected ? active_ : item_;
				if ((t.layout.layer >= 1) != front)
					continue;
				const float x = posX_ + (float)(i - 1) * spacingX_;
				const float y = posY_ + (float)(i - 1) * spacingY_ - level.currentPos;
				t.draw(items[(size_t)i - 1].display, x, y, t.layout.layer, m.fonts(), space);
			}
		}
		platform::clearClip();
	};
	drawItems(false);
	bg_.draw(1, view);
	drawItems(true);

	if (footer_) {
		const float x = space.x((float)footerWindow_[0]), y = space.y((float)footerWindow_[1]);
		platform::fillRect(x, y, (float)(footerWindow_[2] - footerWindow_[0]) * space.scale,
			(float)(footerWindow_[3] - footerWindow_[1]) * space.scale, footerColour_);
	}
	for (const TextLayout *t : { &footerTitle_, &footerInfo_, &footerVersion_ })
		t->draw(0, 0, t->layout.layer, m.fonts(), space);
	if (noticeTime_ > 0) {
		TextLayout centred = active_;
		centred.align = 0;
		centred.layout.offsetX = centred.layout.offsetY = 0;
		centred.draw(noticeText_, (float)m.localWidth() / 2, (float)m.localHeight() * 0.85f, centred.layout.layer,
			m.fonts(), space);
	}
	if (fadeOut_.isActive())
		fadeOut_.draw();
	else if (fadeIn_.isActive())
		fadeIn_.draw();
}

} // namespace m360
