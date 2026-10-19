#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bgdef.h"
#include "core/layout.h"
#include "core/motif.h"
#include "core/settings.h"
#include "platform/platform.h"

namespace m360 {

// The options screen, from a screenpack's [Option Info] and OptionBG (Ikemen's options.lua: its menu tree, the
// t_itemname handlers for the options mugen360 has, t_vardisplay; main.lua's f_menuCommonCalc / f_menuCommonDraw:
// the value column, the box behind the menu and the pulsing box cursor). Up / down move (spacers are skipped),
// left / right change the value, a button opens a submenu or runs an item, Back leaves a submenu, and at the top
// returns without saving. Changes apply at once; "Save and Return" also writes save/config.ini.
class OptionsScreen {
public:
	struct Item {
		std::string name, display;
		std::vector<Item> children;
	};

	bool load(Motif &motif, Settings &settings, std::string &error);
	// Back to the top with the fade in.
	void reset();
	void update(const platform::Pad &pad1, const platform::Pad &pad2);
	void draw();
	// Where "Save and Return" writes (save/config.ini; a test run's elsewhere).
	void setSavePath(const std::string &path) { savePath_ = path; }
	// Closed: back to the title.
	bool done() const { return done_; }

	// For the tests: the item under the cursor, an item's value as shown, and the top menu.
	const std::string &selected() const;
	std::string valueText(const std::string &name) const;
	const std::vector<Item> &menu() const { return menu_; }

private:
	struct Level {
		const std::vector<Item> *items = nullptr;
		int32_t item = 0;
		std::string title;
	};

	void move(Level &level, int32_t direction);
	// left / right on an item (Ikemen's t_itemname add / subtract).
	bool adjust(const std::string &name, int32_t direction);
	// A button on an item that does something (default, inputdefault, savereturn, return).
	void run(const std::string &name);
	void applyVolumes() const;
	bool pressed(bool now, bool &before) const;

	Motif *motif_ = nullptr;
	Settings *settings_ = nullptr;
	BgDef bg_;
	uint32_t clearColour_ = 0xFF000000;
	std::vector<Item> menu_;
	std::vector<Level> levels_;

	// Set by load() from [Option Info] over Ikemen's defaults. No default values here: GCC merged them into an
	// instruction Xenia skips (see xenia_scan).
	float posX_, posY_, spacingX_, spacingY_;
	TextLayout title_, item_, active_, value_, valueActive_;
	std::string titleText_ = "OPTIONS";
	bool titleUppercase_ = false;
	// The box behind the items and the cursor box: [x1, y1, x2, y2] from the menu's position.
	int32_t boxCoords_[4];
	bool boxBg_ = true, boxCursor_ = true;
	uint32_t boxBgColour_, boxCursorColour_;
	int32_t pulse_[3];
	float boxTween_, cursorY_, pulsePhase_;
	bool cursorPlaced_ = false;
	std::string none_ = "None", normal_ = "Normal", slow_ = "Slow %i", fast_ = "Fast %i";
	int32_t moveSnd_[2] = { -1, 0 }, doneSnd_[2] = { -1, 0 }, cancelSnd_[2] = { -1, 0 };
	ScreenFade fadeIn_, fadeOut_;
	bool closing_ = false, done_ = false;
	std::string savePath_ = SETTINGS_PATH;
	// Buttons last frame: up, down, left, right, done, cancel.
	bool held_[6] = { false, false, false, false, false, false };
};

} // namespace m360
