#include "core/select.h"

#include <algorithm>
#include <cstdio>

#include "core/debug_text.h"
#include "core/file.h"
#include "core/ini.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

constexpr uint32_t TEXT = 0xFFFFFFFF, DIM = 0xFFA0A0B0, P1_COLOUR = 0xFFFF5040, P2_COLOUR = 0xFF40A0FF,
				   CELL_BG = 0xC0202028, SHADOW = 0xC0000000;
constexpr float CELL = 96, GAP = 8;
constexpr int COLUMNS = 8;
constexpr int32_t VERSUS_TIME = 120;

float textWidth(const std::string &s, float scale)
{
	return s.size() * 6 * scale - scale;
}

void centred(const std::string &s, float x, float y, float scale, uint32_t argb)
{
	const float left = x - textWidth(s, scale) / 2;
	drawText(s.c_str(), left + scale, y + scale, scale, SHADOW);
	drawText(s.c_str(), left, y, scale, argb);
}

// A .def's [Info] name (displayname first), unquoted.
std::string infoName(const std::string &defPath, bool display)
{
	std::vector<uint8_t> bytes;
	if (!readFile(defPath, bytes))
		return {};
	const std::vector<IniSection> def = parseIni(std::string(bytes.begin(), bytes.end()));
	const IniSection *info = findSection(def, "info");
	if (!info)
		return {};
	std::string name = display ? info->text("displayname") : std::string();
	return name.empty() ? info->text("name") : name;
}

} // namespace

bool SelectScreen::load(const GameConfig &config, std::string &error)
{
	base_ = config;
	roster_.clear();
	for (const std::string &def : config.characters) {
		Entry e;
		e.def = def;
		e.name = infoName(def, true);
		if (e.name.empty()) {
			logf("select: can't read %s; left out", def.c_str());
			continue;
		}
		// Only the portraits are kept: small 9000,0 for the grid, big 9000,1 for the player's side.
		std::vector<uint8_t> bytes;
		readFile(def, bytes);
		const IniSection *files = nullptr;
		const std::vector<IniSection> ini = parseIni(std::string(bytes.begin(), bytes.end()));
		files = findSection(ini, "files");
		if (const IniSection *info = findSection(ini, "info")) {
			int32_t width = 320, height = 240;
			if (info->ints("localcoord", { &width, &height }) && width > 0)
				e.scale = 320.0f / width;
		}
		if (files) {
			e.portraits = std::make_unique<Sff>();
			std::string sffError;
			if (e.portraits->load(directoryOf(def) + files->text("sprite"), sffError, true)) {
				e.portraits->keepOnly({ { 9000, 0 }, { 9000, 1 } });
				e.renderer = std::make_unique<SpriteRenderer>(*e.portraits);
			} else {
				logf("select: %s", sffError.c_str());
				e.portraits.reset();
			}
		}
		roster_.push_back(std::move(e));
	}
	stages_ = config.stages.empty() ? std::vector<std::string>{ config.stage } : config.stages;
	stageNames_.clear();
	for (const std::string &stage : stages_) {
		const std::string name = infoName(stage, false);
		stageNames_.push_back(name.empty() ? stage : name);
	}
	if (roster_.empty()) {
		error = "no characters to select";
		return false;
	}
	logf("select: %zu characters, %zu stages", roster_.size(), stages_.size());
	return true;
}

void SelectScreen::reset(bool p2Cpu)
{
	p2Cpu_ = p2Cpu;
	for (Player &p : players_)
		p = Player{};
	players_[1].cursor = std::min<int>(1, (int)roster_.size() - 1);
	players_[1].palette = 2;
	phase_ = Phase::Characters;
	timer_ = 0;
}

int32_t SelectScreen::random(int32_t n)
{
	random_ ^= random_ << 13;
	random_ ^= random_ >> 17;
	random_ ^= random_ << 5;
	return n > 0 ? (int32_t)(random_ % (uint32_t)n) : 0;
}

int SelectScreen::paletteButton(const Edges &e)
{
	if (e.pressed(&platform::Pad::a))
		return 1;
	if (e.pressed(&platform::Pad::b))
		return 2;
	if (e.pressed(&platform::Pad::rt))
		return 3;
	if (e.pressed(&platform::Pad::x))
		return 4;
	if (e.pressed(&platform::Pad::y))
		return 5;
	if (e.pressed(&platform::Pad::rb))
		return 6;
	return 0;
}

void SelectScreen::move(Player &p, const Edges &e)
{
	const int n = (int)roster_.size();
	const int columns = std::min(COLUMNS, n);
	if (e.pressed(&platform::Pad::left))
		p.cursor = (p.cursor + n - 1) % n;
	if (e.pressed(&platform::Pad::right))
		p.cursor = (p.cursor + 1) % n;
	if (e.pressed(&platform::Pad::up) && p.cursor - columns >= 0)
		p.cursor -= columns;
	if (e.pressed(&platform::Pad::down) && p.cursor + columns < n)
		p.cursor += columns;
}

void SelectScreen::update(const platform::Pad &p1, const platform::Pad &p2)
{
	pads_[0].before = pads_[0].now;
	pads_[0].now = p1;
	pads_[1].before = pads_[1].now;
	pads_[1].now = p2;
	timer_++;
	switch (phase_) {
	case Phase::Characters:
		for (int i = 0; i < 2; i++) {
			Player &p = players_[i];
			if (p.chosen || (i == 1 && p2Cpu_))
				continue;
			move(p, pads_[i]);
			if (const int pal = paletteButton(pads_[i])) {
				p.palette = pal;
				p.chosen = true;
			}
		}
		// The computer picks once player 1 has.
		if (p2Cpu_ && players_[0].chosen && !players_[1].chosen) {
			players_[1].cursor = random((int32_t)roster_.size());
			players_[1].palette = 1 + random(6);
			players_[1].chosen = true;
		}
		// The same character twice can't wear the same colours.
		if (players_[0].chosen && players_[1].chosen) {
			if (players_[0].cursor == players_[1].cursor && players_[0].palette == players_[1].palette)
				players_[1].palette = players_[1].palette % 6 + 1;
			phase_ = Phase::Stage;
			timer_ = 0;
		}
		break;
	case Phase::Stage: {
		const int n = (int)stages_.size();
		if (pads_[0].pressed(&platform::Pad::left))
			stage_ = (stage_ + n - 1) % n;
		if (pads_[0].pressed(&platform::Pad::right))
			stage_ = (stage_ + 1) % n;
		if (timer_ > 1 && (paletteButton(pads_[0]) || pads_[0].pressed(&platform::Pad::start))) {
			phase_ = Phase::Versus;
			timer_ = 0;
		}
		break;
	}
	case Phase::Versus:
		if (timer_ >= VERSUS_TIME)
			phase_ = Phase::Done;
		break;
	case Phase::Done: break;
	}
}

GameConfig SelectScreen::fight() const
{
	GameConfig c = base_;
	c.player1 = roster_[players_[0].cursor].def;
	c.player2 = roster_[players_[1].cursor].def;
	c.p1Palette = players_[0].palette;
	c.p2Palette = players_[1].palette;
	c.stage = stages_[stage_];
	return c;
}

void SelectScreen::drawPortrait(const Entry &entry, int32_t number, float x, float y, float scale, bool mirror,
	int palette) const
{
	if (!entry.renderer || !entry.portraits)
		return;
	const Sprite *s = entry.portraits->get(9000, (uint16_t)number);
	if (!s)
		return;
	// The chosen palette n replaces palette 1,1 (as Ikemen remaps 1,1 to 1,n); a portrait with a palette of its own
	// keeps it.
	const bool remap = palette > 0 && s->palette == entry.portraits->paletteIndex(1, 1);
	entry.renderer->setPalette(remap ? entry.portraits->paletteIndex(1, (uint16_t)palette) : -1);
	// Drawn from its top left corner, whatever its axis.
	SpriteDraw how;
	scale *= entry.scale;
	how.scaleX = how.scaleY = scale;
	how.flipH = mirror;
	const float ax = s->axisX * scale, ay = s->axisY * scale;
	entry.renderer->drawSprite(9000, (uint16_t)number, mirror ? x + s->width * scale - ax : x + ax, y + ay, how);
}

void SelectScreen::draw() const
{
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, 0xFF101018);
	const float w = (float)platform::SCREEN_WIDTH;

	if (phase_ == Phase::Versus || phase_ == Phase::Done) {
		// Big portraits facing each other, the names, "VS".
		for (int i = 0; i < 2; i++) {
			const Entry &e = roster_[players_[i].cursor];
			drawPortrait(e, 1, i == 0 ? 120 : w - 120 - 360, 160, 3, i == 1, players_[i].palette);
			centred(e.name, i == 0 ? 300 : w - 300, 600, 4, i == 0 ? P1_COLOUR : P2_COLOUR);
		}
		centred("VS", w / 2, 300, 14, 0xFFFFD040);
		centred(stageNames_[stage_], w / 2, 668, 3, DIM);
		return;
	}

	centred(phase_ == Phase::Stage ? "SELECT STAGE" : "SELECT YOUR FIGHTER", w / 2, 30, 5, TEXT);
	// Each player's choice so far: big portrait and name on their side.
	for (int i = 0; i < 2; i++) {
		const Player &p = players_[i];
		const Entry &e = roster_[p.cursor];
		const float x = i == 0 ? 40 : w - 40 - 300;
		drawPortrait(e, 1, x, 100, 2.5f, i == 1, p.palette);
		char line[64];
		std::snprintf(line, sizeof line, "%s  PAL %d", e.name.c_str(), p.palette);
		const uint32_t colour = i == 0 ? P1_COLOUR : P2_COLOUR;
		drawText(line, x, 470, 2, colour);
		const char *state = p.chosen ? "READY" : i == 1 && p2Cpu_ ? "CPU" : "CHOOSING";
		drawText(state, x, 492, 2, p.chosen ? TEXT : DIM);
	}

	// The grid of small portraits, each player's cursor a coloured frame.
	const int n = (int)roster_.size();
	const int columns = std::min(COLUMNS, n);
	const float gridW = columns * CELL + (columns - 1) * GAP;
	const float left = (w - gridW) / 2, top = 150;
	for (int i = 0; i < n; i++) {
		const float cx = left + (i % columns) * (CELL + GAP), cy = top + (i / columns) * (CELL + GAP);
		platform::fillRect(cx, cy, CELL, CELL, CELL_BG);
		drawPortrait(roster_[i], 0, cx + 6, cy + 6, (CELL - 12) / 25, false, 0);
		for (int pl = 0; pl < 2; pl++) {
			if (players_[pl].cursor != i || (pl == 1 && p2Cpu_ && !players_[1].chosen))
				continue;
			const uint32_t c = pl == 0 ? P1_COLOUR : P2_COLOUR;
			const float inset = pl * 4.0f;
			platform::fillRect(cx + inset, cy + inset, CELL - 2 * inset, 3, c);
			platform::fillRect(cx + inset, cy + CELL - 3 - inset, CELL - 2 * inset, 3, c);
			platform::fillRect(cx + inset, cy + inset, 3, CELL - 2 * inset, c);
			platform::fillRect(cx + CELL - 3 - inset, cy + inset, 3, CELL - 2 * inset, c);
		}
	}

	if (phase_ == Phase::Stage) {
		char line[128];
		std::snprintf(line, sizeof line, "< %s >", stageNames_[stage_].c_str());
		centred(line, w / 2, 560, 4, 0xFFFFD040);
		centred("LEFT / RIGHT: STAGE    ANY ATTACK BUTTON: FIGHT", w / 2, 640, 2, DIM);
	} else {
		centred("ARROWS: CHOOSE    J K L U I O (A B RT X Y RB): PICK WITH PALETTE 1-6", w / 2, 640, 2, DIM);
	}
}

} // namespace m360
