#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/fight/bgdef.h"
#include "core/screens/layout.h"
#include "core/screens/motif.h"
#include "platform/platform.h"

namespace m360 {

// What the title menu chose.
enum class TitleChoice : uint8_t { None, Arcade, TeamArcade, Versus, TeamVersus, QuickMatch, Training, Survival, Watch, Options, NeuralVs, Exit };

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
		// A mode mugen360 can't run yet, kept as a row that says so: a screenpack that paints its labels needs
		// every row in place, or each painted label below starts the next one's mode.
		bool placeholder = false;
		// mugen360 draws this row's label itself, over the screenpack's painted one (claimPlaceholder).
		bool ownLabel = false;
	};

	bool load(Motif &motif, std::string &error);
	// Back to the top menu with the fade in.
	void reset();
	// One more top-level item, before the one named before (or last): mugen360's own modes, such as "neuralvs" (VS
	// NEURAL CAP), which no screenpack lists.
	void addItem(const std::string &name, const std::string &display, const std::string &before);
	// Whether the menu draws its items' text: some screenpacks paint the labels into their art (font -1 or missing),
	// so an added item would be invisible there and shift every painted label below it.
	bool itemsShowText() const;
	// On a screenpack whose labels are painted: takes the first top-level row of a mode mugen360 can't run (a
	// placeholder) for this item, with its own label drawn over the painted one. False if there's no such row.
	bool claimPlaceholder(const std::string &name, const std::string &display);
	// A mode on a button instead (LB): its hint is drawn at the top of the screen. For screenpacks whose menu is art.
	void addShortcut(const std::string &hint, TitleChoice choice);
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
	// The labels of rows mugen360 claimed in a painted menu (claimPlaceholder).
	void drawOwnLabels(const ScreenSpace &space) const;

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
	// menu.boxcursor.*: set by load() (no default values here, see xenia_scan).
	bool boxCursor_;
	int32_t boxCoords_[4], boxPulse_[3];
	uint32_t boxColour_;
	float boxPhase_;
	ScreenFade fadeIn_, fadeOut_;

	TitleChoice pending_ = TitleChoice::None, result_ = TitleChoice::None;
	std::string noticeText_;
	int32_t noticeTime_ = 0;
	// The choice waits for its notice before the fade-out starts.
	bool fadeAfterNotice_ = false;
	// Buttons last frame, for presses: up, down, done, cancel, the shortcut.
	bool held_[5] = { false, false, false, false, false };
	std::string shortcutHint_;
	TitleChoice shortcut_ = TitleChoice::None;
};

} // namespace m360
