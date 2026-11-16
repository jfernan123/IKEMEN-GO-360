#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/screens/layout.h"
#include "core/screens/motif.h"
#include "core/formats/sff.h"
#include "core/render/sprite_renderer.h"
#include "platform/platform.h"

namespace m360 {

// What the pause menu chose, once it closes.
enum class PauseChoice : uint8_t { None, Continue, CharacterChange, Exit };

// The pause menu over a fight (Ikemen's external/script/menu.lua: f_init, f_run, the "back", "commandlist",
// "characterchange" and "exit" items, f_commandlistParse and f_commandlistRender), laid out from the screenpack's
// [Pause Menu] over Ikemen's defaultMotif.ini values. Back (Ikemen's menu key) opens and closes it. The command list
// shows each human player's movelist file (the character's [Files] movelist) with Ikemen's glyphs from
// data/glyphs.sff. Not yet: Button Config, the training pause menu, [PauseBgDef] backgrounds, scroll arrows.
class PauseMenu {
public:
	// A human player's name and movelist file ("" for none).
	struct Movelist {
		std::string name, path;
	};

	bool load(Motif &motif);
	bool loaded() const { return motif_ != nullptr; }
	void open(std::vector<Movelist> movelists);
	void update(const platform::Pad &pad1, const platform::Pad &pad2);
	void draw();
	// None while it's open.
	PauseChoice result() const { return result_; }
	// hidebars: the fight screen (lifebars, timer) hidden while it's open.
	bool hideBars() const { return hideBars_; }

private:
	struct Item {
		std::string name, display;
	};
	// A piece of a command list line: text in a colour, or a glyph, aligned left (1), centre (0) or right (-1).
	struct Piece {
		bool glyph = false;
		std::string text;
		int align = 1;
		uint32_t colour = 0;
	};
	using Line = std::vector<Piece>;

	bool pressed(bool now, bool &before) const;
	void parseMovelist(size_t which);
	void drawCommandList();

	Motif *motif_ = nullptr;
	ScreenSpace space_;
	std::vector<Item> items_;
	int32_t item_ = 0;
	float posX_ = 85, posY_ = 33, spacingY_ = 14;
	TextLayout title_, itemText_, activeText_, movelistTitle_, movelistText_;
	// Set by load() (no default values here, see xenia_scan).
	int32_t boxCoords_[4], boxPulse_[3];
	uint32_t boxColour_, boxBgColour_, overlayColour_, movelistOverlayColour_;
	bool boxCursor_, boxBg_, hideBars_;
	float boxPhase_;
	float movelistPosX_, movelistPosY_, movelistWidth_, textSpacingX_, textSpacingY_;
	float glyphOffsetX_, glyphOffsetY_, glyphScaleX_, glyphScaleY_, glyphSpacingX_;
	int32_t movelistVisible_;
	std::string notFound_;
	int32_t moveSnd_[2], doneSnd_[2], cancelSnd_[2], enterSnd_[2], exitSnd_[2];

	// The glyphs: token (longest first) and sprite.
	std::vector<std::pair<std::string, std::pair<uint16_t, uint16_t>>> glyphs_;
	Sff glyphSff_;
	std::unique_ptr<SpriteRenderer> glyphRenderer_;

	std::vector<Movelist> movelists_;
	std::vector<std::vector<Line>> parsed_;
	std::vector<int32_t> firstLine_;
	size_t shownList_ = 0;
	bool commandList_ = false;
	PauseChoice result_ = PauseChoice::None;
	// Buttons last frame, for presses: up, down, left, right, done, back.
	bool held_[6] = { true, true, true, true, true, true };
};

} // namespace m360
