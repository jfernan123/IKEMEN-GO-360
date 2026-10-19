#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bgdef.h"
#include "core/layout.h"
#include "core/motif.h"
#include "platform/platform.h"

namespace m360 {

// What the title menu chose.
enum class TitleChoice : uint8_t { None, Arcade, TeamArcade, Versus, TeamVersus, QuickMatch, Training, Survival, Watch, Options, Exit };

// The title screen and main menu, from a screenpack's [Title Info] and TitleBG (Ikemen's main.lua: f_createMenu,
// f_menuCommonCalc, f_menuCommonDraw, f_menuWindow). The menu comes from the "menu.itemname." keys in file order;
// modes mugen360 can't play yet are left out, and a submenu with one mode left runs it directly (Ikemen's
// f_getSingleMenuAction). Not yet: the logo storyboard, demo and attract modes, the info box, item backgrounds,
// scroll arrows, music.
class TitleScreen {
public:
	struct Item {
		std::string name, display;
		std::vector<Item> children;
	};

	bool load(Motif &motif, std::string &error);
	// Back to the top menu with the fade in.
	void reset();
	void update(const platform::Pad &pad1, const platform::Pad &pad2);
	void draw();
	// The mode picked, once the fade-out has finished; None until then.
	TitleChoice result() const { return result_; }

	const std::vector<Item> &menu() const { return menu_; }
	// The item the cursor is on ("menuversus"), in the menu showing now.
	const std::string &selected() const { return (*levels_.back().items)[(size_t)levels_.back().item - 1].name; }
	// The one mode a submenu leads to, or null when it has a choice (Ikemen's f_getSingleMenuAction).
	static const Item *singleAction(const Item &item);

private:
	// A menu being shown: its items, which is picked, and where the cursor is in the visible window
	// (Ikemen's item / cursorPosY / menuTweenData).
	struct Level {
		const std::vector<Item> *items = nullptr;
		int32_t item = 1, cursorPosY = 1;
		float currentPos = 0, targetPos = 0, slideOffset = 0;
		bool snap = true;
		std::string path;
	};

	void move(Level &level, int32_t direction);
	void scroll(Level &level);
	void choose(const Item &item, const std::string &path);
	void notice(const std::string &text, int32_t ticks);
	bool pressed(bool now, bool &before) const;

	Motif *motif_ = nullptr;
	BgDef bg_;
	uint32_t clearColour_ = 0xFF000000;
	std::vector<Item> menu_;
	std::vector<Level> levels_;

	// [Title Info]
	float posX_ = 159, posY_ = 158, spacingX_ = 0, spacingY_ = 13;
	int32_t visibleItems_ = 5;
	float marginTop_ = 12, marginBottom_ = 8, tweenFactor_ = 0.5f;
	TextLayout item_, active_;
	TextLayout footerTitle_, footerInfo_, footerVersion_;
	uint32_t footerColour_ = 0;
	int32_t footerWindow_[4] = { 0, 0, 0, 0 };
	bool footer_ = false;
	int32_t moveSnd_[2] = { -1, 0 }, doneSnd_[2] = { -1, 0 }, cancelSnd_[2] = { -1, 0 };
	ScreenFade fadeIn_, fadeOut_;

	TitleChoice pending_ = TitleChoice::None, result_ = TitleChoice::None;
	std::string noticeText_;
	int32_t noticeTime_ = 0;
	// The choice waits for its notice before the fade-out starts.
	bool fadeAfterNotice_ = false;
	// Buttons last frame, for presses: up, down, done, cancel.
	bool held_[4] = { false, false, false, false };
};

} // namespace m360
