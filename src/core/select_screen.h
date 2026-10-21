#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/bgdef.h"
#include "core/game_config.h"
#include "core/game_flow.h"
#include "core/layout.h"
#include "core/motif.h"
#include "core/sff.h"
#include "core/sprite_renderer.h"
#include "core/versus_screen.h"
#include "core/victory_screen.h"
#include "platform/platform.h"

namespace m360 {

// The game mode the select screen is for: it decides the title and who picks player 2.
enum class SelectMode : uint8_t { Versus, QuickMatch, Arcade, Training, TeamVersus, TeamArcade, Survival, Watch };

// Ikemen's select screen, from a screenpack's [Select Info] and SelectBG (Ikemen's start.lua: the start.t_grid
// build, getCellOverride, f_cellMovement, f_drawCursor, updateDrawList, f_selectMenu, f_palMenu, f_stageMenu,
// f_selectScreen; f_animGet's portrait scaling). One character a side: the grid of cells (with ikemen1's per-row 3D
// overrides), each side's cursor, its big portraits (the character's own stance animation and 9000,1), its name, the
// palette menu after a pick, then the stage line. Not yet: team modes, the select timer, hidden / unlock flags,
// several characters in one slot, per-character select sounds, the record text, loading portraits, stage portraits.
// After the picks and the select screen's fade out, Ikemen's VS screen (VersusScreen).
class MotifSelect {
public:
	bool load(Motif &motif, const GameConfig &config, std::string &error);
	// A new selection. secondController: a second controller is plugged in.
	// presetTeam: skip the team menus with this team mode (TEAM_SINGLE / TEAM_SIMUL / TEAM_TURNS) for both sides, as
	// a test run's --mode simul does; -1 shows them.
	void reset(SelectMode mode, bool secondController, int32_t presetTeam = -1);
	// The team modes of Ikemen's team menu that mugen360 runs (Tag isn't), and what each side chose.
	static constexpr int32_t TEAM_SINGLE = 0, TEAM_SIMUL = 1, TEAM_TURNS = 2;
	int32_t teamMode(int side) const { return sides_[side].teamMode; }
	void update(const platform::Pad &pad1, const platform::Pad &pad2);
	void draw();
	// Picks made and the VS screen shown: fight() has the match.
	bool done() const { return phase_ == Phase::Done; }
	// The VS screen is showing (its music plays).
	bool inVersus() const { return versusStarted_; }
	// Back pressed while choosing: back to the title.
	bool cancelled() const { return cancelled_; }
	GameConfig fight() const;
	// A won Arcade fight: pick the next route opponent and stage, and enter its VS screen. False completes the route.
	bool nextArcadeMatch();
	// A won Survival fight: the next opponent on the same stage, through its VS screen.
	bool nextSurvivalMatch();
	// After a match (13e): the victory screen with the winning side's character and quote, if the screenpack shows
	// one now. False when it doesn't.
	bool startVictory(int winnerSide, const std::string &quote, bool computerWon);
	void updateVictory(const platform::Pad &pad1, const platform::Pad &pad2) { victory_.update(pad1, pad2); }
	void drawVictory() { victory_.draw(); }
	bool victoryDone() const { return victory_.done(); }
	int32_t survivalWins() const { return survival_.wins(); }
	int32_t arcadeMatchNo() const { return arcade_.matchNo(); }
	int32_t arcadeMatchCount() const { return arcade_.matchCount(); }

	// For the tests.
	struct Point {
		float x = 0, y = 0;
	};
	// A cell's position relative to [Select Info] pos, and its scale (Ikemen's start.t_grid and getCellTransform).
	Point cellPosition(int32_t column, int32_t row) const;
	float cellScale(int32_t column, int32_t row) const;
	// One step of the cursor (Ikemen's f_cellMovement): dx / dy of -1, 0 or 1.
	void cellMovement(int32_t &x, int32_t &y, int32_t dx, int32_t dy) const;
	// The palette menu's next / previous: indexes 1..count are palettes, count + 1 is random ("?").
	static int32_t cyclePalette(int32_t index, int32_t count, int32_t direction);
	int32_t rosterSize() const { return (int32_t)roster_.size(); }
	bool isRandom(int32_t cell) const { return cell < (int32_t)roster_.size() && roster_[(size_t)cell].random; }
	// A cell that holds a character (not the random cell, not an empty dummyslot / skipslot one).
	bool isPlayable(int32_t cell) const
	{
		return cell >= 0 && cell < (int32_t)roster_.size() && !roster_[(size_t)cell].random &&
			!roster_[(size_t)cell].empty;
	}

private:
	struct Entry {
		std::string def, name;
		bool random = false;
		// select.def's dummyslot / skipslot: a cell with no character in it.
		bool empty = false;
		// Portrait sprites are scaled by the screenpack's width over the character's localcoord (Ikemen's resolution
		// fix), animations also by its [Size] scale.
		float resFix = 1, sizeScaleX = 1, sizeScaleY = 1;
		std::unique_ptr<Sff> sff;
		std::unique_ptr<SpriteRenderer> renderer;
		Animation stance, done, versus;
		bool hasStance = false, hasDone = false, hasVersus = false;
		// The palettes 1,n it has.
		std::vector<int32_t> palettes;
	};
	// One cell's override ("cell.*-9."): the keys it sets.
	struct CellOverride {
		bool hasSpacing = false, hasOffset = false, hasScale = false, skip = false;
		float spacingX = 0, spacingY = 0, offsetX = 0, offsetY = 0, scaleX = 0, scaleY = 0;
		int32_t facing = 0;
		bool hasXangle = false, hasYangle = false, hasFocal = false, hasProjection = false, perspective = false;
		float xangle = 0, yangle = 0, focalLength = 0;
	};
	// A drawable piece of the screen ("p1.cursor.done.", "cell.bg."): a sprite or animation from system.sff.
	struct Piece {
		AnimLayout layout;
	};
	struct Side {
		// What's set in the def for this side.
		int32_t startX = 0, startY = 0;
		AnimLayout cursorActive, cursorDone;
		float tweenX = 0.5f, tweenY = 0.5f;
		int32_t moveSnd[2] = { -1, 0 }, doneSnd[2] = { -1, 0 };
		Layout face, face2;
		int32_t faceAnim = -1, faceDoneAnim = -1, face2Spr[2] = { -1, 0 };
		float facePos[2] = { 0, 0 };
		TextLayout name;
		// The palette menu.
		float palPos[2] = { 0, 0 };
		AnimLayout palBg;
		TextLayout palText, palNumber;
		std::string palRandomText = "?";
		int32_t palValueSnd[2] = { -1, 0 }, palDoneSnd[2] = { -1, 0 }, palCancelSnd[2] = { -1, 0 };
		// The team menu ("pN.teammenu."): its title, items, blinking active item, cursor and member icons.
		float teamPos[2] = { 0, 0 }, teamItemSpacing[2] = { 0, 0 }, teamValueSpacing[2] = { 0, 0 };
		TextLayout teamSelfTitle, teamEnemyTitle, teamItem, teamItemActive, teamItemActive2;
		AnimLayout teamCursor, teamValueIcon, teamValueEmpty;
		int32_t teamMoveSnd[2] = { -1, 0 }, teamDoneSnd[2] = { -1, 0 };
		int32_t teamSwitch = 2;

		// The selection.
		int32_t x = 0, y = 0;
		float drawX = 0, drawY = 0;
		bool cursorPlaced = false;
		// -1 team menu, 0 moving, 1 palette menu, 3 chosen (Ikemen's selectState).
		int32_t state = 0;
		// The team menu's cursor, the mode chosen and how many members that picks.
		int32_t teamMenu = 0, teamMode = 0, numChars = 1, teamBlinkCount = 0;
		bool teamBlink = false;
		bool active = false;
		// Drawn (cursor, portraits, name): both sides from the start, as Ikemen shows them, except an Arcade
		// opponent, which appears once picked.
		bool shown = false;
		int32_t chosen = -1, palette = 1;
		std::vector<int32_t> teamChosen;
		std::vector<int32_t> teamPalettes;
		int32_t palIndex = 1, randomPreview = 1, randomPalCount = 0;
		// The random cell's character on show, and its switch timer.
		int32_t randomShown = -1, randomCount = 0;
		Animation faceShown;
		int32_t faceShownFor = -2;
		bool faceDone = false;
		int32_t screenDelay = 0;
	};
	enum class Phase { Characters, Stage, Versus, Done };

	const CellOverride *override(int32_t column, int32_t row) const;
	void readSide(Side &side, const std::string &prefix, const IniSection &s);
	int32_t cellIndex(int32_t x, int32_t y) const { return y * columns_ + x; }
	const Entry *entryAt(int32_t x, int32_t y) const;
	// The roster entry a side's face shows: its pick, or the one under its cursor (the random cell's preview).
	int32_t shownEntry(const Side &side) const;
	void updateSide(int index, const platform::Pad &now, const platform::Pad &before);
	void confirmPick(int index);
	void setFace(Side &side, int32_t entry, bool done);
	void drawCell(const AnimLayout &piece, int32_t column, int32_t row, float x, float y, SpriteRenderer &renderer,
		const Entry *character);
	void drawFaces(int index);
	void drawPalMenu(int index);
	void drawTeamMenu(int index);
	bool teamModes() const
	{
		return mode_ == SelectMode::TeamVersus || mode_ == SelectMode::TeamArcade || mode_ == SelectMode::Watch;
	}
	void drawStage();
	void startVersus();
	void setArcadeOpponent(const std::vector<int32_t> &real);
	int32_t random(int32_t n);
	// Elements on layerno 0 draw behind SelectBG's layer 1, those on 1 and 2 in front (as Ikemen layers its sprites).
	bool inPass(int32_t layer) const { return (layer >= 1) == front_; }
	void drawElements();
	bool front_ = false;

	Motif *motif_ = nullptr;
	GameConfig base_;
	std::vector<Entry> roster_;
	std::vector<std::string> stages_, stageNames_;
	BgDef bg_;
	uint32_t clearColour_ = 0xFF000000;

	// [Select Info]
	int32_t rows_ = 2, columns_ = 5;
	bool wrapping_ = false, showEmpty_ = false, moveOverEmpty_ = false;
	float posX_ = 0, posY_ = 0, cellW_ = 27, cellH_ = 27, spacingX_ = 2, spacingY_ = 2;
	AnimLayout cellBg_, cellRandom_, portrait_;
	int32_t portraitSpr_[2] = { 9000, 0 };
	int32_t randomSwitch_ = 4, palRandomSwitch_ = 4;
	std::vector<std::pair<std::string, CellOverride>> overrides_;
	TextLayout title_;
	// The team menu's items ("teammenu.itemname.single" ...) and whether moving wraps.
	std::string teamNames_[3] = { "Single", "Simul", "Turns" };
	bool teamWrap_ = true;
	std::string titles_[8];
	int32_t cancelSnd_[2] = { -1, 0 };
	ScreenFade fadeIn_, fadeOut_;
	// The stage line.
	float stagePos_[2] = { 0, 0 };
	TextLayout stageActive_, stageActive2_, stageDone_;
	// The select timer (Ikemen's timer.* and main.f_drawTimer): count seconds of framespercount ticks, shown after
	// displaytime; -1 count is off. selectTimer_ is Ikemen's timerSelect (-1 once it has run out), timerShown_ the
	// number drawn. Set by load(), no default values (see xenia_scan).
	TextLayout timerText_;
	std::string timerFormat_;
	int32_t timerCount_, timerFrames_, timerDisplay_, selectTimer_, timerShown_;
	// The timer ran out this tick: the pick, the palette and the stage are taken as they stand.
	bool timerExpired() const { return timerCount_ >= 0 && selectTimer_ == -1; }
	void restartTimer() { selectTimer_ = timerDisplay_; }

public:
	// For the tests: a select timer of this many counts, as if the screenpack set timer.count.
	void setTimerCount(int32_t count) { timerCount_ = count; }

private:
	std::string stageText_ = "Stage %i: %s", stageRandomText_ = "Stage: Random";
	int32_t stageMoveSnd_[2] = { -1, 0 }, stageDoneSnd_[2] = { -1, 0 }, stageSwitch_ = 2;
	AnimLayout stageBg_, stageRandom_;
	// Each stage's portrait (stage.portrait.spr from its own sprites), scaled from its localcoord like a character's.
	struct StagePortrait {
		std::unique_ptr<Sff> sff;
		std::unique_ptr<SpriteRenderer> renderer;
		float resFix = 1;
	};
	std::vector<StagePortrait> stagePortraits_;
	Layout stagePortrait_;
	int32_t stagePortraitSpr_[2] = { 9000, 1 };
	Side sides_[2];
	VersusScreen versus_;
	VictoryScreen victory_;
	bool versusStarted_ = false;

	SelectMode mode_ = SelectMode::Versus;
	bool secondController_ = false;
	Phase phase_ = Phase::Characters;
	int32_t stage_ = 0, stageCount_ = 0;
	bool stageBlink_ = true;
	bool cancelled_ = false;
	int32_t timer_ = 0;
	platform::Pad before_[2];
	uint32_t random_ = 0x9E3779B9u;
	ArcadeLadder arcade_;
	SurvivalLadder survival_;
};

} // namespace m360
