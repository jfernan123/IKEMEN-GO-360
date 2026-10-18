#include "core/select_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

// [Select Info] values for keys a screenpack leaves out (Ikemen's data/system.base.def).
const char *const SELECT_DEFAULTS[][2] = {
	{ "fadein.time", "10" },
	{ "fadeout.time", "10" },
	{ "rows", "2" },
	{ "columns", "5" },
	{ "pos", "90, 170" },
	{ "cell.size", "27, 27" },
	{ "cell.spacing", "2" },
	{ "cell.random.switchtime", "4" },
	{ "portrait.spr", "9000, 0" },
	{ "palmenu.random.switchtime", "4" },
	{ "stage.text", "Stage %i: %s" },
	{ "stage.random.text", "Stage: Random" },
	{ "stage.active.switchtime", "2" },
	{ "title.versus.text", "Versus Mode" },
	{ "title.freebattle.text", "Quick Match" },
	{ "title.arcade.text", "Arcade" },
	{ "title.training.text", "Training Mode" },
	{ "p1.cursor.tween.factor", "0.5, 0.5" },
	{ "p2.cursor.tween.factor", "0.5, 0.5" },
	{ "p1.palmenu.random.text", "?" },
	{ "p2.palmenu.random.text", "?" },
};

// A .def's [Info] name (displayname first), unquoted.
std::string infoName(const std::vector<IniSection> &def, bool display)
{
	const IniSection *info = findSection(def, "info");
	if (!info)
		return {};
	std::string name = display ? info->text("displayname") : std::string();
	return name.empty() ? info->text("name") : name;
}

std::vector<IniSection> readDef(const std::string &path)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes))
		return {};
	return parseIni(std::string(bytes.begin(), bytes.end()));
}

// Ikemen's f_tweenStep for the cursor: a factor of the way to the target each frame.
float tween(float value, float target, float factor)
{
	return value + (target - value) * std::min(std::fabs(factor), 1.0f);
}

std::string format(const std::string &text, int32_t number, const std::string &name)
{
	std::string out = text;
	size_t at = out.find("%i");
	if (at != std::string::npos)
		out.replace(at, 2, std::to_string(number));
	at = out.find("%s");
	if (at != std::string::npos)
		out.replace(at, 2, name);
	return out;
}

bool pressed(bool now, bool before)
{
	return now && !before;
}

bool doneButton(const platform::Pad &p)
{
	return p.a || p.b || p.x || p.y || p.rb || p.rt;
}

} // namespace

bool MotifSelect::load(Motif &motif, const GameConfig &config, std::string &error)
{
	motif_ = &motif;
	base_ = config;
	const IniSection *own = motif.section("select info");
	if (!own) {
		error = motif.path() + " has no [Select Info]";
		return false;
	}
	motif.loadBg("selectbg", bg_, clearColour_);
	versus_.load(motif);
	IniSection s;
	for (const auto &pair : SELECT_DEFAULTS)
		s.values[pair[0]] = pair[1];
	for (const auto &[key, value] : own->values)
		s.values[key] = value;

	s.readInt("rows", rows_);
	s.readInt("columns", columns_);
	rows_ = std::max(1, rows_);
	columns_ = std::max(1, columns_);
	int32_t v = 0;
	wrapping_ = s.readInt("wrapping", v) && v != 0;
	showEmpty_ = s.readInt("showemptyboxes", v) && v != 0;
	moveOverEmpty_ = s.readInt("moveoveremptyboxes", v) && v != 0;
	s.floats("pos", { &posX_, &posY_ });
	s.floats("cell.size", { &cellW_, &cellH_ });
	// "cell.spacing = 4" is both ways.
	if (s.floats("cell.spacing", { &spacingX_, &spacingY_ }) && text::split(s.get("cell.spacing"), ',').size() < 2)
		spacingY_ = spacingX_;
	const AnimationTable &anims = motif.anims();
	cellBg_.read("cell.bg.", s, anims);
	cellRandom_.read("cell.random.", s, anims);
	s.readInt("cell.random.switchtime", randomSwitch_);
	portrait_.layout.read("portrait.", s);
	s.ints("portrait.spr", { &portraitSpr_[0], &portraitSpr_[1] });
	s.readInt("palmenu.random.switchtime", palRandomSwitch_);
	title_.read("title.", s);
	const char *modes[7] = { "versus", "freebattle", "arcade", "training", "versus", "arcade", "survival" };
	for (int i = 0; i < 7; i++)
		titles_[i] = s.text(std::string("title.") + modes[i] + ".text");
	// Ikemen's team menu leads to the same select screen, but these labels keep the chosen sub-mode visible.
	titles_[(int)SelectMode::TeamVersus] = "TEAM VERSUS";
	titles_[(int)SelectMode::TeamArcade] = "TEAM ARCADE";
	s.ints("cancel.snd", { &cancelSnd_[0], &cancelSnd_[1] });
	fadeIn_.read("fadein.", s);
	fadeOut_.read("fadeout.", s);
	s.floats("stage.pos", { &stagePos_[0], &stagePos_[1] });
	stageActive_.read("stage.active.", s);
	stageActive2_.read("stage.active2.", s);
	stageDone_.read("stage.done.", s);
	stageText_ = s.text("stage.text");
	stageRandomText_ = s.text("stage.random.text");
	s.ints("stage.move.snd", { &stageMoveSnd_[0], &stageMoveSnd_[1] });
	s.ints("stage.done.snd", { &stageDoneSnd_[0], &stageDoneSnd_[1] });
	s.readInt("stage.active.switchtime", stageSwitch_);
	stageBg_.read("stage.portrait.bg.", s, anims);
	stageRandom_.read("stage.portrait.random.", s, anims);
	stagePortrait_.read("stage.portrait.", s);
	s.ints("stage.portrait.spr", { &stagePortraitSpr_[0], &stagePortraitSpr_[1] });
	readSide(sides_[0], "p1.", s);
	readSide(sides_[1], "p2.", s);
	for (TextLayout *t : { &title_, &stageActive_, &stageActive2_, &stageDone_, &sides_[0].name, &sides_[1].name,
			 &sides_[0].palText, &sides_[1].palText, &sides_[0].palNumber, &sides_[1].palNumber })
		motif.resolveFont(*t);

	// "cell.<column>-<row>.<key>", either part "*" (Ikemen's getCellOverride).
	overrides_.clear();
	for (const std::string &key : own->keys) {
		if (!text::startsWith(key, "cell."))
			continue;
		const size_t dot = key.find('.', 5);
		const std::string cell = key.substr(5, dot == std::string::npos ? std::string::npos : dot - 5);
		if (cell.find('-') == std::string::npos || dot == std::string::npos)
			continue;
		auto found = std::find_if(overrides_.begin(), overrides_.end(), [&](const auto &p) { return p.first == cell; });
		if (found == overrides_.end()) {
			overrides_.emplace_back(cell, CellOverride{});
			found = overrides_.end() - 1;
		}
		CellOverride &o = found->second;
		const std::string prop = key.substr(dot + 1);
		const IniSection &raw = *own;
		if (prop == "spacing")
			o.hasSpacing = raw.floats(key, { &o.spacingX, &o.spacingY });
		else if (prop == "offset")
			o.hasOffset = raw.floats(key, { &o.offsetX, &o.offsetY });
		else if (prop == "scale")
			o.hasScale = raw.floats(key, { &o.scaleX, &o.scaleY });
		else if (prop == "facing")
			raw.readInt(key, o.facing);
		else if (prop == "skip")
			o.skip = raw.readInt(key, v) && v != 0;
		else if (prop == "xangle")
			o.hasXangle = raw.readFloat(key, o.xangle);
		else if (prop == "yangle")
			o.hasYangle = raw.readFloat(key, o.yangle);
		else if (prop == "focallength")
			o.hasFocal = raw.readFloat(key, o.focalLength);
		else if (prop == "projection") {
			o.hasProjection = true;
			o.perspective = text::startsWith(text::toLower(raw.text(key)), "perspective");
		}
	}

	// The roster: data/mugen360.ini's [Characters] (the screenpack's own select.def names characters not shipped).
	roster_.clear();
	for (const std::string &def : config.characters) {
		Entry e;
		e.def = def;
		if (def == "randomselect") {
			e.random = true;
			roster_.push_back(std::move(e));
			continue;
		}
		const std::vector<IniSection> ini = readDef(def);
		e.name = infoName(ini, true);
		const IniSection *files = findSection(ini, "files");
		if (e.name.empty() || !files) {
			logf("select: can't read %s; left out", def.c_str());
			continue;
		}
		int32_t localW = 320, localH = 240;
		if (const IniSection *info = findSection(ini, "info"))
			info->ints("localcoord", { &localW, &localH });
		float portraitScale = 1;
		if (const IniSection *info = findSection(ini, "info"))
			info->readFloat("portraitscale", portraitScale);
		e.resFix = portraitScale * (float)motif.localWidth() / (float)std::max(1, localW);
		// [Size] xscale / yscale from its constants, else 320 / localcoord (Ikemen's cns_scale).
		e.sizeScaleX = e.sizeScaleY = 320.0f / (float)std::max(1, localW);
		const std::string dir = directoryOf(def);
		const std::vector<IniSection> cns = readDef(dir + files->text("cns"));
		if (const IniSection *size = findSection(cns, "size")) {
			size->readFloat("xscale", e.sizeScaleX);
			size->readFloat("yscale", e.sizeScaleY);
		}
		// The animations the screenpack shows, and only the sprites they and the portraits use.
		AnimationTable charAnims;
		std::string animError;
		const uint16_t big = 9000;
		std::vector<std::pair<uint16_t, uint16_t>> keep = { { (uint16_t)portraitSpr_[0], (uint16_t)portraitSpr_[1] },
			{ big, (uint16_t)0 }, { big, (uint16_t)1 } };
		for (const Side &side : sides_) {
			if (side.face2Spr[0] >= 0)
				keep.emplace_back((uint16_t)side.face2Spr[0], (uint16_t)side.face2Spr[1]);
		}
		if (versus_.faceSprite(0) >= 0)
			keep.emplace_back((uint16_t)versus_.faceSprite(0), (uint16_t)versus_.faceSprite(1));
		if (charAnims.load(dir + files->text("anim"), animError)) {
			e.hasStance = sides_[0].faceAnim >= 0 && charAnims.get(sides_[0].faceAnim, e.stance);
			e.hasDone = sides_[0].faceDoneAnim >= 0 && charAnims.get(sides_[0].faceDoneAnim, e.done);
			e.hasVersus = versus_.faceAnim() >= 0 && charAnims.get(versus_.faceAnim(), e.versus);
			for (const Animation *a : { &e.stance, &e.done, &e.versus }) {
				for (const AnimFrame &f : a->frames) {
					if (f.group >= 0)
						keep.emplace_back((uint16_t)f.group, (uint16_t)f.number);
				}
			}
		}
		e.sff = std::make_unique<Sff>();
		std::string sffError;
		if (!e.sff->load(dir + files->text("sprite"), sffError, true)) {
			logf("select: %s; left out", sffError.c_str());
			continue;
		}
		for (int32_t n = 1; n <= 12; n++) {
			if (e.sff->paletteIndex(1, (uint16_t)n) >= 0)
				e.palettes.push_back(n);
		}
		if (e.palettes.empty())
			e.palettes.push_back(1);
		e.sff->keepOnly(keep);
		e.renderer = std::make_unique<SpriteRenderer>(*e.sff);
		roster_.push_back(std::move(e));
	}
	stages_ = config.stages.empty() ? std::vector<std::string>{ config.stage } : config.stages;
	stageNames_.clear();
	stagePortraits_.clear();
	for (const std::string &stage : stages_) {
		const std::vector<IniSection> def = readDef(stage);
		const std::string name = infoName(def, false);
		stageNames_.push_back(name.empty() ? stage : name);
		// Only the portrait is kept of the stage's sprites.
		StagePortrait portrait;
		const IniSection *bgdef = findSection(def, "bgdef");
		const std::string spr = bgdef ? bgdef->text("spr") : std::string();
		// [StageInfo] localcoord and portraitscale (Ikemen's select-screen stage loading).
		int32_t localW = 320, localH = 240;
		float portraitScale = 1;
		if (const IniSection *info = findSection(def, "stageinfo")) {
			info->ints("localcoord", { &localW, &localH });
			info->readFloat("portraitscale", portraitScale);
		}
		portrait.resFix = portraitScale * (float)motif.localWidth() / (float)std::max(1, localW);
		std::string sffError;
		portrait.sff = std::make_unique<Sff>();
		if (!spr.empty() && (portrait.sff->load(directoryOf(stage) + spr, sffError) || portrait.sff->load("data/" + spr, sffError))) {
			std::vector<std::pair<uint16_t, uint16_t>> keep = { { (uint16_t)stagePortraitSpr_[0], (uint16_t)stagePortraitSpr_[1] } };
			if (versus_.stageSprite(0) >= 0)
				keep.emplace_back((uint16_t)versus_.stageSprite(0), (uint16_t)versus_.stageSprite(1));
			portrait.sff->keepOnly(keep);
			portrait.renderer = std::make_unique<SpriteRenderer>(*portrait.sff);
		} else {
			portrait.sff.reset();
		}
		stagePortraits_.push_back(std::move(portrait));
	}
	if (std::none_of(roster_.begin(), roster_.end(), [](const Entry &e) { return !e.random; })) {
		error = "no characters to select";
		return false;
	}
	logf("select: %zu characters, %zu stages", roster_.size(), stages_.size());
	return true;
}

void MotifSelect::readSide(Side &side, const std::string &p, const IniSection &s)
{
	const AnimationTable &anims = motif_->anims();
	s.ints(p + "cursor.startcell", { &side.startY, &side.startX });
	side.cursorActive.read(p + "cursor.active.", s, anims);
	side.cursorDone.read(p + "cursor.done.", s, anims);
	s.floats(p + "cursor.tween.factor", { &side.tweenX, &side.tweenY });
	s.ints(p + "cursor.move.snd", { &side.moveSnd[0], &side.moveSnd[1] });
	s.ints(p + "cursor.done.snd", { &side.doneSnd[0], &side.doneSnd[1] });
	side.face.read(p + "face.", s);
	side.face2.read(p + "face2.", s);
	s.readInt(p + "face.anim", side.faceAnim);
	s.readInt(p + "face.done.anim", side.faceDoneAnim);
	s.ints(p + "face2.spr", { &side.face2Spr[0], &side.face2Spr[1] });
	s.floats(p + "face.pos", { &side.facePos[0], &side.facePos[1] });
	side.name.read(p + "name.", s);
	s.floats(p + "palmenu.pos", { &side.palPos[0], &side.palPos[1] });
	side.palBg.read(p + "palmenu.bg.", s, anims);
	side.palText.read(p + "palmenu.text.", s);
	side.palNumber.read(p + "palmenu.number.", s);
	side.palRandomText = s.text(p + "palmenu.random.text");
	s.ints(p + "palmenu.value.snd", { &side.palValueSnd[0], &side.palValueSnd[1] });
	s.ints(p + "palmenu.done.snd", { &side.palDoneSnd[0], &side.palDoneSnd[1] });
	s.ints(p + "palmenu.cancel.snd", { &side.palCancelSnd[0], &side.palCancelSnd[1] });
}

const MotifSelect::CellOverride *MotifSelect::override(int32_t column, int32_t row) const
{
	const std::string c = std::to_string(column), r = std::to_string(row);
	for (const std::string &key : { c + "-" + r, c + "-*", "*-" + r, std::string("*-*") }) {
		for (const auto &[name, o] : overrides_) {
			if (name == key)
				return &o;
		}
	}
	return nullptr;
}

MotifSelect::Point MotifSelect::cellPosition(int32_t column, int32_t row) const
{
	float sx = spacingX_, sy = spacingY_, ox = 0, oy = 0;
	if (const CellOverride *o = override(column, row)) {
		// Ikemen's getCellSpacing: one value means both ways.
		if (o->hasSpacing && (o->spacingX != 0 || o->spacingY != 0)) {
			sx = o->spacingX;
			sy = o->spacingY != 0 || o->spacingX == 0 ? o->spacingY : o->spacingX;
		}
		if (o->hasOffset) {
			ox = o->offsetX;
			oy = o->offsetY;
		}
	}
	Point p;
	p.x = (float)column * (cellW_ + sx) + ox;
	p.y = (float)row * (cellH_ + sy) + oy;
	return p;
}

float MotifSelect::cellScale(int32_t column, int32_t row) const
{
	const CellOverride *o = override(column, row);
	return o && o->hasScale && (o->scaleX != 0 || o->scaleY != 0) ? o->scaleX : 0;
}

const MotifSelect::Entry *MotifSelect::entryAt(int32_t x, int32_t y) const
{
	const int32_t i = cellIndex(x, y);
	return i >= 0 && i < (int32_t)roster_.size() ? &roster_[(size_t)i] : nullptr;
}

// Ikemen's f_cellMovement, one press: along the row or column, wrapping if the screenpack says so, over empty cells
// if it allows, never onto a skipped cell.
void MotifSelect::cellMovement(int32_t &x, int32_t &y, int32_t dx, int32_t dy) const
{
	const int32_t startX = x, startY = y;
	const auto usable = [&](int32_t cx, int32_t cy) {
		const CellOverride *o = override(cx, cy);
		return (entryAt(cx, cy) || moveOverEmpty_) && !(o && o->skip);
	};
	if (dy != 0) {
		for (int32_t i = 0; i < rows_; i++) {
			y += dy;
			if (y < 0 || y >= rows_) {
				if (!wrapping_) {
					y = startY;
					break;
				}
				y = y < 0 ? rows_ - 1 : 0;
			}
			if (usable(x, y))
				break;
		}
	} else if (dx != 0) {
		for (int32_t i = 0; i < columns_; i++) {
			x += dx;
			if (x < 0 || x >= columns_) {
				if (!wrapping_) {
					x = startX;
					break;
				}
				x = x < 0 ? columns_ - 1 : 0;
			}
			if (usable(x, y))
				break;
		}
	}
}

int32_t MotifSelect::cyclePalette(int32_t index, int32_t count, int32_t direction)
{
	const int32_t last = count + 1;
	if (direction > 0)
		return index >= last ? 1 : index + 1;
	return index <= 1 ? last : index - 1;
}

int32_t MotifSelect::random(int32_t n)
{
	random_ ^= random_ << 13;
	random_ ^= random_ >> 17;
	random_ ^= random_ << 5;
	return n > 0 ? (int32_t)(random_ % (uint32_t)n) : 0;
}

void MotifSelect::reset(SelectMode mode, bool secondController)
{
	mode_ = mode;
	secondController_ = secondController;
	phase_ = Phase::Characters;
	cancelled_ = false;
	timer_ = 0;
	versusStarted_ = false;
	stage_ = 0;
	stageCount_ = 0;
	stageBlink_ = true;
	for (int i = 0; i < 2; i++) {
		Side &side = sides_[i];
		side.x = std::clamp(side.startX, 0, columns_ - 1);
		side.y = std::clamp(side.startY, 0, rows_ - 1);
		side.cursorPlaced = false;
		side.state = 0;
		side.chosen = -1;
		side.palette = 1;
		side.faceShownFor = -2;
		side.faceDone = false;
		side.screenDelay = 0;
		side.randomShown = -1;
		// Player 2 picks alongside player 1 only in 1P vs 2P with a second controller; otherwise it waits.
		side.teamChosen.clear();
		side.teamPalettes.clear();
		const bool humanTeams = mode == SelectMode::Versus || mode == SelectMode::TeamVersus;
		side.active = i == 0 || (humanTeams && secondController);
		side.shown = i == 0 || (mode != SelectMode::Arcade && mode != SelectMode::TeamArcade && mode != SelectMode::Survival);
	}
	// A button still held from the last screen doesn't count as a press.
	for (platform::Pad &p : before_) {
		p = platform::Pad{};
		p.up = p.down = p.left = p.right = true;
		p.a = p.b = p.x = p.y = p.rb = p.rt = p.start = p.back = true;
	}
	bg_.reset();
	fadeIn_.start(true);
	fadeOut_.stop();
	const char *title = titles_[(int)mode].c_str();
	logf("select: %s", title);
}

int32_t MotifSelect::shownEntry(const Side &side) const
{
	if (side.chosen >= 0)
		return side.chosen;
	const int32_t i = cellIndex(side.x, side.y);
	if (i < 0 || i >= (int32_t)roster_.size())
		return -1;
	return roster_[(size_t)i].random ? side.randomShown : i;
}

void MotifSelect::setFace(Side &side, int32_t entry, bool done)
{
	if (side.faceShownFor == entry && side.faceDone == done)
		return;
	side.faceShownFor = entry;
	side.faceDone = done;
	if (entry < 0 || roster_[(size_t)entry].random)
		return;
	const Entry &e = roster_[(size_t)entry];
	side.faceShown = done && e.hasDone ? e.done : e.stance;
	side.faceShown.reset();
	if (done && e.hasDone) {
		// The screen waits for the "done" animation, at most 2 seconds (Ikemen's screenDelay).
		side.screenDelay = std::min(120, side.faceShown.totaltime > 0 ? side.faceShown.totaltime : 0);
	}
}

void MotifSelect::confirmPick(int index)
{
	Side &side = sides_[index];
	const bool teamMode = mode_ == SelectMode::TeamVersus || mode_ == SelectMode::TeamArcade;
	side.teamChosen.push_back(side.chosen);
	side.teamPalettes.push_back(side.palette);
	logf("select: p%d member %zu %s palette %d", index + 1, side.teamChosen.size(),
		roster_[(size_t)side.chosen].name.c_str(), (int)side.palette);
	if (teamMode && side.teamChosen.size() < 2) {
		side.state = 0;
		side.chosen = -1;
		side.faceDone = false;
		side.screenDelay = 0;
		return;
	}
	side.state = 3;
	setFace(side, side.chosen, true);
	logf("select: p%d %s palette %d", index + 1, roster_[(size_t)side.chosen].name.c_str(), (int)side.palette);
	if (index == 0 && !sides_[1].active && sides_[1].chosen < 0) {
		if (mode_ == SelectMode::Arcade || mode_ == SelectMode::TeamArcade || mode_ == SelectMode::Survival) {
			// Arcade: make one shuffled route through the real roster, then take its first opponent.
			std::vector<int32_t> real;
			for (int32_t i = 0; i < (int32_t)roster_.size(); i++) {
				if (!roster_[(size_t)i].random)
					real.push_back(i);
			}
			const auto player = std::find(real.begin(), real.end(), side.chosen);
			const int32_t playerIndex = player == real.end() ? -1 : (int32_t)(player - real.begin());
			if (mode_ == SelectMode::Survival) {
				survival_.reset((int32_t)real.size(), playerIndex, random_);
				Side &cpu = sides_[1];
				cpu.state = 0;
				cpu.chosen = real[(size_t)survival_.opponent()];
				const Entry &entry = roster_[(size_t)cpu.chosen];
				cpu.palette = entry.palettes[(size_t)random((int32_t)entry.palettes.size())];
				cpu.shown = true;
				confirmPick(1);
			} else {
				arcade_.reset((int32_t)real.size(), playerIndex, random_);
				setArcadeOpponent(real);
			}
		} else {
			// Against the computer, player 1 picks its character too, starting where player 1's cursor is.
			sides_[1].active = true;
			sides_[1].x = side.x;
			sides_[1].y = side.y;
			sides_[1].cursorPlaced = false;
		}
	}
}

void MotifSelect::updateSide(int index, const platform::Pad &now, const platform::Pad &before)
{
	Side &side = sides_[index];
	const bool up = pressed(now.up, before.up), down = pressed(now.down, before.down);
	const bool left = pressed(now.left, before.left), right = pressed(now.right, before.right);
	const bool done = pressed(doneButton(now), doneButton(before));
	const bool start = pressed(now.start, before.start), cancel = pressed(now.back, before.back);
	if (side.state == 0) {
		const int32_t oldX = side.x, oldY = side.y;
		cellMovement(side.x, side.y, right ? 1 : left ? -1 : 0, down ? 1 : up ? -1 : 0);
		if (side.x != oldX || side.y != oldY)
			motif_->play(side.moveSnd);
		if (cancel) {
			motif_->play(cancelSnd_);
			cancelled_ = true;
			return;
		}
		const int32_t cell = cellIndex(side.x, side.y);
		if (done && cell >= 0 && cell < (int32_t)roster_.size()) {
			// The random cell picks whoever it's showing.
			side.chosen = roster_[(size_t)cell].random ? std::max(0, side.randomShown) : cell;
			if (side.chosen < 0 || roster_[(size_t)side.chosen].random)
				return;
			motif_->play(side.doneSnd);
			const Entry &e = roster_[(size_t)side.chosen];
			// Straight to the palette menu, on palette 1 (Ikemen's paletteselect 3); one palette needs no menu.
			side.palIndex = 1;
			side.palette = e.palettes[0];
			side.state = 1;
			if (e.palettes.size() <= 1)
				confirmPick(index);
		}
		return;
	}
	if (side.state != 1)
		return;
	// The palette menu (Ikemen's f_palMenu): next / previous through the palettes and "?", done, cancel, Start for "?".
	const Entry &e = roster_[(size_t)side.chosen];
	const int32_t count = (int32_t)e.palettes.size();
	if (done) {
		side.palette = side.palIndex > count ? side.randomPreview : e.palettes[(size_t)side.palIndex - 1];
		motif_->play(side.palDoneSnd);
		confirmPick(index);
	} else if (up || right || down || left) {
		side.palIndex = cyclePalette(side.palIndex, count, up || right ? 1 : -1);
		if (side.palIndex <= count)
			side.palette = e.palettes[(size_t)side.palIndex - 1];
		motif_->play(side.palValueSnd);
	} else if (cancel) {
		side.state = 0;
		side.chosen = -1;
		motif_->play(side.palCancelSnd);
	}
	if (start)
		side.palIndex = count + 1;
	// On "?", a random palette is shown, changing every palmenu.random.switchtime.
	if (side.palIndex > count) {
		if (side.randomPalCount <= 0) {
			side.randomPalCount = palRandomSwitch_;
			side.randomPreview = e.palettes[(size_t)random(count)];
			side.palette = side.randomPreview;
			motif_->play(side.palValueSnd);
		} else {
			side.randomPalCount--;
		}
	}
}

void MotifSelect::update(const platform::Pad &pad1, const platform::Pad &pad2)
{
	timer_++;
	bg_.update();
	if (fadeIn_.isActive())
		fadeIn_.step();
	for (Side &side : sides_) {
		side.cursorActive.action();
		side.cursorDone.action();
		side.palBg.action();
		side.faceShown.action();
		// The random cell switches its character while a cursor is on it.
		if (side.chosen < 0 && entryAt(side.x, side.y) && entryAt(side.x, side.y)->random && --side.randomCount <= 0) {
			side.randomCount = randomSwitch_;
			std::vector<int32_t> real;
			for (int32_t i = 0; i < (int32_t)roster_.size(); i++) {
				if (!roster_[(size_t)i].random)
					real.push_back(i);
			}
			side.randomShown = real[(size_t)random((int32_t)real.size())];
		}
		if (side.chosen < 0 || side.state < 3)
			setFace(side, shownEntry(side), false);
		if (side.screenDelay > 0 && side.state == 3)
			side.screenDelay--;
	}
	cellBg_.action();
	cellRandom_.action();
	stageBg_.action();

	const platform::Pad now[2] = { pad1, pad2 };
	switch (phase_) {
	case Phase::Characters:
		if (fadeIn_.isActive())
			break;
		if (sides_[0].state != 3)
			updateSide(0, pad1, before_[0]);
		else if (sides_[1].active && sides_[1].state != 3 &&
			!((mode_ == SelectMode::Versus || mode_ == SelectMode::TeamVersus) && secondController_))
			updateSide(1, pad1, before_[0]); // player 1 picks for the computer
		if ((mode_ == SelectMode::Versus || mode_ == SelectMode::TeamVersus) && secondController_ && sides_[1].state != 3)
			updateSide(1, pad2, before_[1]);
		if (cancelled_)
			break;
		if (sides_[0].state == 3 && sides_[1].state == 3) {
			// Arcade picks its stage; the other modes, Survival included (Ikemen's stageMenu), choose one.
			if (mode_ == SelectMode::Arcade || mode_ == SelectMode::TeamArcade) {
				stage_ = 1 + random((int32_t)stages_.size());
				logf("select: stage %s", stageNames_[(size_t)stage_ - 1].c_str());
				logf("arcade: match %d of %d", arcade_.matchNo(), arcade_.matchCount());
				phase_ = Phase::Versus;
			} else {
				phase_ = Phase::Stage;
			}
			timer_ = 0;
		}
		break;
	case Phase::Stage: {
		// Ikemen's f_stageMenu: 0 is random, then the stages; up / down move by 10.
		const int32_t count = (int32_t)stages_.size();
		const platform::Pad &p = pad1, &b = before_[0];
		int32_t step = pressed(p.left, b.left) ? -1 : pressed(p.right, b.right) ? 1 : pressed(p.up, b.up) ? -10
			: pressed(p.down, b.down)																	   ? 10
																										   : 0;
		if (step != 0) {
			motif_->play(stageMoveSnd_);
			const int32_t direction = step > 0 ? 1 : -1;
			for (int32_t i = 0; i < std::abs(step); i++) {
				stage_ += direction;
				if (stage_ < 0)
					stage_ = count;
				if (stage_ > count)
					stage_ = 0;
			}
		}
		if (pressed(doneButton(p), doneButton(b))) {
			motif_->play(stageDoneSnd_);
			if (stage_ == 0)
				stage_ = 1 + random(count);
			logf("select: stage %s", stageNames_[(size_t)stage_ - 1].c_str());
			if (mode_ == SelectMode::Survival)
				logf("survival: match %d", survival_.matchNo());
			phase_ = Phase::Versus;
			timer_ = 0;
		} else if (pressed(p.back, b.back)) {
			// Back to player 2's (or player 1's) palette choice.
			Side &last = sides_[1].active ? sides_[1] : sides_[0];
			last.state = 1;
			motif_->play(cancelSnd_);
			phase_ = Phase::Characters;
		} else if (++stageCount_ > stageSwitch_) {
			stageCount_ = 0;
			stageBlink_ = !stageBlink_;
		}
		break;
	}
	case Phase::Versus:
		// Once the "done" animations have had their time, fade out; then the VS screen.
		if (versusStarted_) {
			const bool humans[2] = { true,
				(mode_ == SelectMode::Versus || mode_ == SelectMode::TeamVersus) && secondController_ };
			versus_.update(pad1, pad2, humans);
			if (versus_.done())
				phase_ = Phase::Done;
		} else if (!fadeOut_.isActive() && sides_[0].screenDelay <= 0 && sides_[1].screenDelay <= 0 && timer_ < 1000) {
			fadeOut_.start(false);
			timer_ = 1000;
		} else if (fadeOut_.isActive()) {
			fadeOut_.step();
		} else if (timer_ >= 1000) {
			startVersus();
		}
		break;
	case Phase::Done: break;
	}
	for (int i = 0; i < 2; i++)
		before_[i] = now[i];
}

void MotifSelect::setArcadeOpponent(const std::vector<int32_t> &real)
{
	if (!arcade_.active() || arcade_.opponent() >= (int32_t)real.size())
		return;
	Side &cpu = sides_[1];
	const int members = mode_ == SelectMode::TeamArcade ? 2 : 1;
	cpu.teamChosen.clear();
	cpu.teamPalettes.clear();
	for (int member = 0; member < members; member++) {
		cpu.state = 0;
		cpu.chosen = member == 0 ? real[(size_t)arcade_.opponent()] : real[(size_t)random((int32_t)real.size())];
		const Entry &entry = roster_[(size_t)cpu.chosen];
		cpu.palette = entry.palettes[(size_t)random((int32_t)entry.palettes.size())];
		cpu.shown = true;
		confirmPick(1);
	}
}

bool MotifSelect::nextArcadeMatch()
{
	if ((mode_ != SelectMode::Arcade && mode_ != SelectMode::TeamArcade) || !arcade_.advance())
		return false;
	std::vector<int32_t> real;
	for (int32_t i = 0; i < (int32_t)roster_.size(); i++)
		if (!roster_[(size_t)i].random)
			real.push_back(i);
	setArcadeOpponent(real);
	stage_ = stages_.empty() ? 0 : 1 + random((int32_t)stages_.size());
	if (stage_ > 0)
		logf("select: stage %s", stageNames_[(size_t)stage_ - 1].c_str());
	logf("arcade: match %d of %d", arcade_.matchNo(), arcade_.matchCount());
	phase_ = Phase::Versus;
	versusStarted_ = false;
	timer_ = 1000;
	startVersus();
	return true;
}

bool MotifSelect::nextSurvivalMatch()
{
	if (mode_ != SelectMode::Survival || !survival_.active())
		return false;
	survival_.advance();
	std::vector<int32_t> real;
	for (int32_t i = 0; i < (int32_t)roster_.size(); i++)
		if (!roster_[(size_t)i].random)
			real.push_back(i);
	Side &cpu = sides_[1];
	cpu.teamChosen.clear();
	cpu.teamPalettes.clear();
	cpu.state = 0;
	cpu.chosen = real[(size_t)survival_.opponent()];
	const Entry &entry = roster_[(size_t)cpu.chosen];
	cpu.palette = entry.palettes[(size_t)random((int32_t)entry.palettes.size())];
	cpu.shown = true;
	confirmPick(1);
	// The stage chosen at the start stays for the whole run (Ikemen's survival keeps stageListNo).
	logf("survival: match %d", survival_.matchNo());
	phase_ = Phase::Versus;
	versusStarted_ = false;
	timer_ = 1000;
	startVersus();
	return true;
}

// Hands the picks to the VS screen: each side's character in its palette, the stage, Arcade's match number.
void MotifSelect::startVersus()
{
	VersusScreen::Fighter fighters[2];
	for (int i = 0; i < 2; i++) {
		const int32_t chosen = sides_[i].teamChosen.empty() ? sides_[i].chosen : sides_[i].teamChosen.front();
		const int32_t palette = sides_[i].teamPalettes.empty() ? sides_[i].palette : sides_[i].teamPalettes.front();
		const Entry &e = roster_[(size_t)chosen];
		VersusScreen::Fighter &f = fighters[i];
		f.name = e.name;
		f.sff = e.sff.get();
		f.renderer = e.renderer.get();
		f.anim = e.versus;
		f.hasAnim = e.hasVersus;
		f.resFix = e.resFix;
		f.sizeScaleX = e.sizeScaleX;
		f.sizeScaleY = e.sizeScaleY;
		f.palette = palette;
	}
	VersusScreen::StagePick stage;
	const size_t s = (size_t)std::max(0, stage_ - 1);
	stage.name = stageNames_[s];
	stage.number = stage_;
	stage.renderer = stagePortraits_[s].renderer.get();
	stage.resFix = stagePortraits_[s].resFix;
	const int32_t matchNo = mode_ == SelectMode::Survival ? survival_.matchNo()
		: mode_ == SelectMode::Arcade || mode_ == SelectMode::TeamArcade ? arcade_.matchNo()
																					 : 0;
	versus_.start(fighters, stage, matchNo);
	versusStarted_ = true;
}

GameConfig MotifSelect::fight() const
{
	GameConfig c = base_;
	c.player1 = roster_[(size_t)sides_[0].chosen].def;
	c.player2 = roster_[(size_t)sides_[1].chosen].def;
	c.p1Palette = sides_[0].palette;
	c.p2Palette = sides_[1].palette;
	for (size_t i = 0; i < sides_[0].teamChosen.size(); i++)
		c.team1.push_back({ roster_[(size_t)sides_[0].teamChosen[i]].def, sides_[0].teamPalettes[i] });
	for (size_t i = 0; i < sides_[1].teamChosen.size(); i++)
		c.team2.push_back({ roster_[(size_t)sides_[1].teamChosen[i]].def, sides_[1].teamPalettes[i] });
	if (!c.team1.empty()) {
		c.player1 = c.team1[0].def;
		c.p1Palette = c.team1[0].palette;
	}
	if (!c.team2.empty()) {
		c.player2 = c.team2[0].def;
		c.p2Palette = c.team2[0].palette;
	}
	c.stage = stages_[(size_t)std::max(0, stage_ - 1)];
	return c;
}

// One element in a grid cell, with that cell's overrides: scale, facing, 3D angles, projection.
void MotifSelect::drawCell(const AnimLayout &piece, int32_t column, int32_t row, float x, float y,
	SpriteRenderer &renderer, const Entry *character)
{
	if ((!piece.present && !character) || !inPass(piece.layout.layer))
		return;
	const ScreenSpace &space = motif_->space();
	SpriteDraw how;
	piece.layout.apply(how, space);
	float sx = piece.layout.scaleX, sy = piece.layout.scaleY;
	int32_t facing = piece.layout.facing;
	if (const CellOverride *o = override(column, row)) {
		if (o->hasScale && (o->scaleX != 0 || o->scaleY != 0)) {
			sx = o->scaleX;
			sy = o->scaleY;
		}
		if (o->facing != 0)
			facing = o->facing;
		if (o->hasXangle)
			how.xangle = o->xangle;
		if (o->hasYangle)
			how.yangle = o->yangle;
		if (o->hasProjection)
			how.perspective = o->perspective;
		if (o->hasFocal)
			how.focalLength = Layout::focalPixels(o->focalLength);
	}
	const float px = space.x(x + piece.layout.offsetX), py = space.y(y + piece.layout.offsetY);
	if (character) {
		// A character's small portrait, scaled from its resolution to the screenpack's.
		how.scaleX = sx * character->resFix * space.scale;
		how.scaleY = sy * character->resFix * space.scale;
		how.flipH = facing < 0;
		renderer.drawSprite(portraitSpr_[0], portraitSpr_[1], px, py, how);
		return;
	}
	how.scaleX = sx;
	how.scaleY = sy * piece.layout.vfacing;
	renderer.drawAnimation(piece.anim, px, py, space.scale, facing, how);
}

void MotifSelect::drawFaces(int index)
{
	Side &side = sides_[index];
	const int32_t shown = shownEntry(side);
	if (!side.shown || shown < 0 || roster_[(size_t)shown].random)
		return;
	const Entry &e = roster_[(size_t)shown];
	const ScreenSpace &space = motif_->space();
	// The chosen palette recolours only the sprites drawn with the character's palette 1,1; a portrait with its own
	// palette keeps it.
	const int base = e.sff->paletteIndex(1, 1), chosen = e.sff->paletteIndex(1, (uint16_t)side.palette);
	e.renderer->setPalette(-1);
	// face2 (9000,1, turned in 3D) behind face (the character's own animation), each clipped to its window.
	ClipRect window;
	if (side.face2Spr[0] >= 0 && inPass(side.face2.layer)) {
		SpriteDraw how;
		side.face2.apply(how, space);
		const Sprite *sprite = e.sff->get((uint16_t)side.face2Spr[0], (uint16_t)side.face2Spr[1]);
		if (sprite && sprite->palette == base)
			how.palette = chosen;
		how.scaleX = side.face2.scaleX * e.resFix * space.scale;
		how.scaleY = side.face2.scaleY * e.resFix * space.scale;
		how.flipH = side.face2.facing < 0;
		const bool clip = side.face2.windowRect(space, window);
		if (clip)
			platform::setClip(window.x, window.y, window.width, window.height);
		e.renderer->drawSprite(side.face2Spr[0], side.face2Spr[1], space.x(side.facePos[0] + side.face2.offsetX),
			space.y(side.facePos[1] + side.face2.offsetY), how);
		if (clip)
			platform::clearClip();
	}
	if (side.faceShownFor == shown && !side.faceShown.frames.empty() && inPass(side.face.layer)) {
		e.renderer->setPalette(chosen);
		SpriteDraw how;
		side.face.apply(how, space);
		how.scaleX = side.face.scaleX * e.resFix * e.sizeScaleX;
		how.scaleY = side.face.scaleY * e.resFix * e.sizeScaleY;
		const bool clip = side.face.windowRect(space, window);
		if (clip)
			platform::setClip(window.x, window.y, window.width, window.height);
		e.renderer->drawAnimation(side.faceShown, space.x(side.facePos[0] + side.face.offsetX),
			space.y(side.facePos[1] + side.face.offsetY), space.scale, side.face.facing, how);
		if (clip)
			platform::clearClip();
	}
	e.renderer->setPalette(-1);
}

void MotifSelect::drawPalMenu(int index)
{
	Side &side = sides_[index];
	if (side.state != 1 || side.chosen < 0)
		return;
	const ScreenSpace &space = motif_->space();
	const int32_t count = (int32_t)roster_[(size_t)side.chosen].palettes.size();
	if (inPass(side.palBg.layout.layer))
		side.palBg.draw(side.palPos[0], side.palPos[1], side.palBg.layout.layer, motif_->renderer(), space);
	if (inPass(side.palText.layout.layer))
		side.palText.draw(side.palPos[0], side.palPos[1], side.palText.layout.layer, motif_->fonts(), space);
	const std::string number = side.palIndex > count ? side.palRandomText
													 : std::to_string(roster_[(size_t)side.chosen].palettes[(size_t)side.palIndex - 1]);
	if (inPass(side.palNumber.layout.layer))
		side.palNumber.draw(number, side.palPos[0], side.palPos[1], side.palNumber.layout.layer, motif_->fonts(), space);
}

void MotifSelect::drawStage()
{
	if (phase_ == Phase::Characters)
		return;
	const ScreenSpace &space = motif_->space();
	// The portrait frame sits relative to the stage line.
	if (inPass(stageBg_.layout.layer))
		stageBg_.draw(stagePos_[0], stagePos_[1], stageBg_.layout.layer, motif_->renderer(), space);
	if (stage_ == 0 && inPass(stageRandom_.layout.layer))
		stageRandom_.draw(stagePos_[0], stagePos_[1], stageRandom_.layout.layer, motif_->renderer(), space);
	// The stage's own portrait, clipped to stage.portrait.window.
	if (stage_ > 0 && inPass(stagePortrait_.layer)) {
		const StagePortrait &p = stagePortraits_[(size_t)stage_ - 1];
		if (p.renderer) {
			SpriteDraw how;
			stagePortrait_.apply(how, space);
			how.scaleX = stagePortrait_.scaleX * p.resFix * space.scale;
			how.scaleY = stagePortrait_.scaleY * p.resFix * space.scale;
			ClipRect window;
			const bool clip = stagePortrait_.windowRect(space, window);
			if (clip)
				platform::setClip(window.x, window.y, window.width, window.height);
			p.renderer->drawSprite(stagePortraitSpr_[0], stagePortraitSpr_[1], space.x(stagePos_[0] + stagePortrait_.offsetX),
				space.y(stagePos_[1] + stagePortrait_.offsetY), how);
			if (clip)
				platform::clearClip();
		}
	}
	const TextLayout &t = phase_ != Phase::Stage ? stageDone_ : stageBlink_ ? stageActive_ : stageActive2_;
	const std::string line = stage_ == 0 ? stageRandomText_ : format(stageText_, stage_, stageNames_[(size_t)stage_ - 1]);
	if (inPass(t.layout.layer))
		t.draw(line, stagePos_[0], stagePos_[1], t.layout.layer, motif_->fonts(), space);
}

void MotifSelect::draw()
{
	if (versusStarted_) {
		versus_.draw();
		return;
	}
	Motif &m = *motif_;
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, clearColour_);
	const View view = m.view();
	bg_.draw(0, view);
	front_ = false;
	drawElements();
	bg_.draw(1, view);
	front_ = true;
	drawElements();

	if (fadeOut_.isActive())
		fadeOut_.draw();
	else if (fadeIn_.isActive())
		fadeIn_.draw();
}

// One pass of Ikemen's f_selectScreen draw order: the title, the big portraits, the grid, the cursors, the names, the
// palette menus, the stage line; front_ picks the elements behind or in front of SelectBG's layer 1.
void MotifSelect::drawElements()
{
	Motif &m = *motif_;
	const ScreenSpace &space = m.space();
	if (inPass(title_.layout.layer))
		title_.draw(titles_[(int)mode_], 0, 0, title_.layout.layer, m.fonts(), space);
	for (int i = 0; i < 2; i++)
		drawFaces(i);

	// The grid (Ikemen's updateDrawList): every cell's background, the random icon, the characters' portraits.
	for (int32_t row = 0; row < rows_; row++) {
		for (int32_t column = 0; column < columns_; column++) {
			const CellOverride *o = override(column, row);
			if (o && o->skip)
				continue;
			const Point p = cellPosition(column, row);
			const float x = posX_ + p.x, y = posY_ + p.y;
			const Entry *e = entryAt(column, row);
			if (e || showEmpty_)
				drawCell(cellBg_, column, row, x, y, m.renderer(), nullptr);
			if (e && e->random)
				drawCell(cellRandom_, column, row, x + portrait_.layout.offsetX, y + portrait_.layout.offsetY,
					m.renderer(), nullptr);
			else if (e && e->renderer)
				drawCell(portrait_, column, row, x, y, *e->renderer, e);
		}
	}

	// Cursors, eased toward their cells (Ikemen's f_drawCursor); a side that has picked shows its "done" cursor.
	for (int i = 0; i < 2; i++) {
		Side &side = sides_[i];
		if (!side.shown)
			continue;
		const Point p = cellPosition(side.x, side.y);
		const float targetX = posX_ + p.x, targetY = posY_ + p.y;
		if (!side.cursorPlaced || side.state == 3) {
			side.drawX = targetX;
			side.drawY = targetY;
			side.cursorPlaced = true;
		} else {
			side.drawX = tween(side.drawX, targetX, side.tweenX);
			side.drawY = tween(side.drawY, targetY, side.tweenY);
		}
		drawCell(side.state == 3 ? side.cursorDone : side.cursorActive, side.x, side.y, side.drawX, side.drawY,
			m.renderer(), nullptr);
	}
	for (int i = 0; i < 2; i++) {
		const Side &side = sides_[i];
		const int32_t shown = shownEntry(side);
		if (side.shown && shown >= 0 && !roster_[(size_t)shown].random && inPass(side.name.layout.layer))
			side.name.draw(roster_[(size_t)shown].name, 0, 0, side.name.layout.layer, m.fonts(), space);
	}
	for (int i = 0; i < 2; i++)
		drawPalMenu(i);
	drawStage();
}

} // namespace m360
