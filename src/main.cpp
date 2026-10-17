// Milestones 1-3: Kung Fu Man's stage with two Kung Fu Men on it, his animations and his sprites, from the real
// MUGEN files. Tab (LB+Start on a pad) cycles stage -> animations -> sprites.
//
// Stage (starts here)
//   Left / right   walk player 1 (controller 2 walks player 2)
//   Up             jump: straight up, or forward / back with left / right held at takeoff, as in MUGEN
//   Down           crouch
//   X              back to the start positions
//   Y              stage info on / off
//
// Animation player                              Sprite viewer
//   Left / right   previous / next action        previous / next sprite (hold to scroll)
//   LB / RB        10 actions at a time          10 sprites at a time
//   Up / down      palette                       palette
//   A              pause / play
//   B              one tick, while paused
//   X              turn around
//   Y              collision boxes on / off
//
// Tab / LB+Start switches screen; Back (Esc) quits. Start remains available to the character as MUGEN's s button.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "core/ai.h"
#include "core/air.h"
#include "core/camera.h"
#include "core/character.h"
#include "core/debug_text.h"
#include "core/effects.h"
#include "core/file.h"
#include "core/game_config.h"
#include "core/hud.h"
#include "core/log.h"
#include "core/match.h"
#include "core/text.h"
#include "core/sff.h"
#include "core/sound.h"
#include "core/sprite_renderer.h"
#include "core/stage.h"
#include "platform/platform.h"

using namespace m360;

namespace {

const char *const FIGHTFX_SFF = "data/fightfx.sff";
const char *const FIGHTFX_AIR = "data/fightfx.air";
const char *const COMMON_SND = "data/common.snd";
constexpr float SCALE = 3.0f;
// Where the viewers stand the character: MUGEN puts a sprite's axis at the character's feet.
constexpr float GROUND_X = platform::SCREEN_WIDTH / 2.0f;
constexpr float GROUND_Y = 600.0f;
constexpr uint32_t BACKGROUND = 0xFF203048;
constexpr uint32_t TEXT = 0xFFFFFFFF;
constexpr uint32_t DIM = 0xFF90A0B8;
constexpr uint32_t ERROR_TEXT = 0xFFFF6060;

// A button that fires once when pressed, then repeats while held.
class Repeat {
public:
	bool update(bool down)
	{
		held_ = down ? held_ + 1 : 0;
		return held_ == 1 || (held_ > 20 && held_ % 3 == 0);
	}

private:
	int held_ = 0;
};

// A button that fires once per press.
class Press {
public:
	bool update(bool down)
	{
		const bool fired = down && !was_;
		was_ = down;
		return fired;
	}

private:
	bool was_ = false;
};

struct Buttons {
	Repeat left, right, lb, rb, up, down, b;
	Press a, x, y, menu, back;
};

// Test runs (PC): "--input script" plays player 1's controller from a file instead of the keyboard ("--input2 script"
// player 2's, else player 2 stands still), "--shot N path"
// saves frame N as a .bmp, "--quit N" stops after frame N, "--quickstart" skips the intros so the fight begins at
// frame 0, "--cpu" makes player 2 the computer, "--trace" logs every state change and hit. A script line is "frames keys...": hold those keys for that
// many frames, keys being U D L R (directions) x y z a b c s (buttons), or "-" for nothing; '#' starts a comment.
class InputScript {
public:
	bool load(const std::string &path)
	{
		std::vector<uint8_t> bytes;
		if (!readFile(path, bytes))
			return false;
		for (const std::string &raw : text::splitLines(std::string(bytes.begin(), bytes.end()))) {
			const std::string line(text::stripComment(std::string_view(raw).substr(0, raw.find('#'))));
			if (line.empty())
				continue;
			const size_t space = line.find(' ');
			const int frames = text::atoi(line.substr(0, space));
			uint16_t held = 0;
			for (char k : space == std::string::npos ? std::string() : line.substr(space)) {
				switch (k) {
				case 'U': held |= Up; break;
				case 'D': held |= Down; break;
				case 'L': held |= Left; break;
				case 'R': held |= Right; break;
				case 'x': held |= X; break;
				case 'y': held |= Y; break;
				case 'z': held |= Z; break;
				case 'a': held |= A; break;
				case 'b': held |= B; break;
				case 'c': held |= C; break;
				case 's': held |= Start; break;
				default: break;
				}
			}
			for (int i = 0; i < frames; i++)
				frames_.push_back(held);
		}
		return true;
	}
	// The pad for a frame; nothing held once the script has run out.
	platform::Pad at(uint32_t frame) const
	{
		const uint16_t held = frame < frames_.size() ? frames_[frame] : 0;
		platform::Pad pad;
		pad.connected = true;
		pad.up = (held & Up) != 0;
		pad.down = (held & Down) != 0;
		pad.left = (held & Left) != 0;
		pad.right = (held & Right) != 0;
		pad.x = (held & X) != 0;
		pad.y = (held & Y) != 0;
		pad.rb = (held & Z) != 0;
		pad.a = (held & A) != 0;
		pad.b = (held & B) != 0;
		pad.rt = (held & C) != 0;
		pad.start = (held & Start) != 0;
		return pad;
	}

private:
	enum : uint16_t {
		Up = 1 << 0, Down = 1 << 1, Left = 1 << 2, Right = 1 << 3, X = 1 << 4, Y = 1 << 5,
		Z = 1 << 6, A = 1 << 7, B = 1 << 8, C = 1 << 9, Start = 1 << 10
	};
	// A compact mask avoids GCC combining the Pad's individual bytes with instructions Xenia does not implement.
	std::vector<uint16_t> frames_;
};

// A controller as MUGEN's buttons: punches x y z on X, Y and RB, kicks a b c on A, B and RT, start on Start.
void toInput(const platform::Pad &pad, InputState &in)
{
	in.up = pad.up;
	in.down = pad.down;
	in.left = pad.left;
	in.right = pad.right;
	in.x = pad.x;
	in.y = pad.y;
	in.z = pad.rb;
	in.a = pad.a;
	in.b = pad.b;
	in.c = pad.rt;
	in.s = pad.start;
}

// Changes a renderer's palette with up / down: -1 (each sprite's own) then the SFF's palettes in turn.
void updatePalette(Buttons &b, const platform::Pad &pad, SpriteRenderer &renderer)
{
	const int palettes = (int)renderer.sff().palettes().size();
	int palette = renderer.palette();
	if (b.up.update(pad.up))
		palette = palette + 1 >= palettes ? -1 : palette + 1;
	if (b.down.update(pad.down))
		palette = palette - 1 < -1 ? palettes - 1 : palette - 1;
	if (palette != renderer.palette()) {
		renderer.setPalette(palette);
		renderer.clear();
	}
}

void drawGround()
{
	platform::fillRect(0, GROUND_Y, platform::SCREEN_WIDTH, 1, 0x60FFFFFF);
	platform::fillRect(GROUND_X, 150, 1, GROUND_Y - 150 + 40, 0x60FFFFFF);
}

// A box's outline and a faint fill, in MUGEN coordinates around the character's position.
void drawClsn(const Clsn &box, int facing, uint32_t rgb)
{
	float l = box[0] * facing, r = box[2] * facing;
	if (l > r)
		std::swap(l, r);
	const float x = GROUND_X + l * SCALE, y = GROUND_Y + box[1] * SCALE;
	const float w = (r - l) * SCALE, h = (box[3] - box[1]) * SCALE;
	platform::fillRect(x, y, w, h, 0x30000000 | rgb);
	platform::fillRect(x, y, w, 2, 0xE0000000 | rgb);
	platform::fillRect(x, y + h - 2, w, 2, 0xE0000000 | rgb);
	platform::fillRect(x, y, 2, h, 0xE0000000 | rgb);
	platform::fillRect(x + w - 2, y, 2, h, 0xE0000000 | rgb);
}

const char *transName(Trans t)
{
	switch (t) {
	case Trans::Add: return "ADD";
	case Trans::Sub: return "SUB";
	case Trans::SubAdd: return "SUBADD";
	default: return "NONE";
	}
}

// The stage with two Kung Fu Men on it, each run by his own state files (milestone 5): everything he does comes from
// kfm.cns, kfm.cmd and common1.cns.
class StageDemo {
public:
	// quickStart: no intros or announcement, the fight starts at once (for scripted tests).
	// cpu: player 2 is the computer even with a controller (or a test script) for it.
	bool load(const GameConfig &config, std::string &error, bool trace, bool quickStart, bool cpu)
	{
		if (!stage_.load(config.stage, error))
			return false;
		if (!effects_.load(FIGHTFX_SFF, FIGHTFX_AIR, error))
			return false;
		if (!commonSound_.load(COMMON_SND, error))
			return false;
		world_.effects = &effects_;
		world_.commonSound = &commonSound_;
		if (trace) {
			players_[0].traceLabel = "p1";
			players_[1].traceLabel = "p2";
		}
		const std::string characterDefs[2] = { config.player1, config.player2 };
		for (int i = 0; i < 2; i++) {
			if (!players_[i].load(characterDefs[i], i + 1, error))
				return false;
			players_[i].playerNo = i;
			players_[i].setOpponent(&players_[1 - i]);
			players_[i].setWorld(&world_);
		}
		quickStart_ = quickStart;
		forceCpu_ = cpu;
		newMatch();
		return true;
	}

	void newMatch()
	{
		match_.newMatch();
		startRound(true);
	}
	void setTestPower(int32_t value)
	{
		for (Character &c : players_)
			c.power = std::clamp(value, 0, 3000);
	}

	// A round begins: camera, stage and characters back to the start (Ikemen's resetRound).
	void startRound(bool newMatch)
	{
		match_.newRound();
		hud_.reset();
		effects_.clear();
		camera_.reset(stage_.camera);
		updateWorld();
		for (int i = 0; i < 2; i++)
			players_[i].startRound((float)stage_.p[i].x, (float)stage_.p[i].y, stage_.p[i].facing, newMatch);
		if (quickStart_) {
			match_.skipIntro();
			for (Character &c : players_)
				c.roundStartControl();
		}
		match_.fillWorld(world_);
		logf("match: round %d", (int)match_.roundNo());
	}

	void update(Buttons &, const platform::Pad &pad1, const platform::Pad &pad2)
	{
		// LB and LT, the buttons MUGEN leaves free: the six attack buttons belong to the characters.
		if (lbPress_.update(pad1.lb))
			showInfo_ = !showInfo_;
		if (ltPress_.update(pad1.lt))
			newMatch();
		// After a KO the game slows down: some frames run no game tick.
		speed_ += match_.speed();
		while (speed_ >= 1) {
			speed_ -= 1;
			tick(pad1, pad2);
		}
	}

	// One game tick: the round, then each phase for both players before the next (Ikemen's charList order).
	void tick(const platform::Pad &pad1, const platform::Pad &pad2)
	{
		const bool globallyPaused = effects_.paused();
		const Match::Event event = globallyPaused ? Match::Event::None : match_.step(players_[0], players_[1]);
		if (event == Match::Event::NextRound) {
			startRound(false);
			return;
		}
		if (event == Match::Event::MatchOver) {
			newMatch();
			return;
		}
		match_.fillWorld(world_);
		updateWorld();
		const float edge = world_.screenEdge;
		// Player 2 is the computer unless a second controller is plugged in.
		const bool cpu = forceCpu_ || !pad2.connected;
		players_[1].aiLevel = cpu ? AI_LEVEL : 0;
		ai_.update((float)AI_LEVEL, cpu && match_.roundState() == 2);
		const bool run[2] = { !effects_.pausedFor(0), !effects_.pausedFor(1) };
		if (run[0]) {
			InputState input1;
			toInput(pad1, input1);
			players_[0].turnAndInput(input1);
			recordCommands(players_[0]);
		}
		if (run[1]) {
			InputState input2;
			toInput(pad2, input2);
			players_[1].turnAndInput(cpu ? ai_.state() : input2);
		}
		for (int i = 0; i < 2; i++)
			if (run[i]) players_[i].actionPrepare();
		for (int i = 0; i < 2; i++)
			if (run[i]) players_[i].actionRun();
		// Players stay on screen and in the stage, and push each other apart.
		for (int i = 0; i < 2; i++) {
			if (players_[i].screenBound())
				keepInBounds(i, edge);
		}
		// Only where their hurt boxes touch, as MUGEN does: a thrown player (no hurt box) isn't pushed off.
		if (players_[0].playerPush() && players_[1].playerPush() && players_[0].clsnTouch(players_[1], 2, 2))
			pushApart(edge);
		// Attacks land: each player's HitDef against the other.
		for (int i = 0; i < 2; i++)
			if (run[i]) players_[i].beginHitDetection();
		for (int i = 0; i < 2; i++)
			if (run[i]) players_[i].hitDetection();
		const float leftest = std::min(players_[0].worldX(), players_[1].worldX());
		const float rightest = std::max(players_[0].worldX(), players_[1].worldX());
		if (!globallyPaused)
			camera_.update(leftest, rightest, std::min(players_[0].worldY(), players_[1].worldY()), world_.halfWidth, edge);
		for (int i = 0; i < 2; i++)
			if (run[i]) players_[i].tick();
		effects_.tick();
		effects_.tickPause();
		if (!globallyPaused) {
			hud_.update(players_[0], players_[1]);
			stage_.update();
		}
		world_.gameTime++;
	}

	void draw()
	{
		const View view = stage_.view(camera_.x - effects_.shakeX(), camera_.y - effects_.shakeY());
		stage_.draw(0, view);
		effects_.draw(view, (float)stage_.zoffset, -1);
		// Player 1 in front.
		for (int i = 1; i >= 0; i--)
			players_[i].draw(view, (float)stage_.zoffset, shadowFor(players_[i]), stage_.shadow.yscale);
		effects_.draw(view, (float)stage_.zoffset, 0);
		stage_.draw(1, view);
		effects_.draw(view, (float)stage_.zoffset, 1);
		if (effects_.superPaused())
			platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, 0x70000000);
		hud_.draw(match_, players_[0], players_[1]);
		if (players_[1].aiLevel > 0)
			drawTextf(platform::SCREEN_WIDTH - 40 - 6 * 2 * 9, 82, 2, 0xFFFFD040, "CPU LV %d", players_[1].aiLevel);
		if (showInfo_) {
			for (const Character &c : players_)
				c.drawBoxes(view, (float)stage_.zoffset);
			drawTextf(30, 100, 2, DIM, "%s   CAMERA %.1f,%.1f   FRAME %u   ROUNDSTATE %d", stage_.name.c_str(), camera_.x,
				camera_.y, world_.gameTime, (int)match_.roundState());
			drawStateInfo(players_[0], "P1", 30, 124);
			drawStateInfo(players_[1], "P2", 560, 124);
			drawCommands();
			drawText("PUNCH U I O  KICK J K L  (PAD X Y RB / A B RT)   Q/LB: INFO  R/LT: NEW MATCH  TAB/LB+START: NEXT", 30, 690, 2,
				TEXT);
		}
	}

private:
	// The camera and screen as the characters' triggers see them.
	void updateWorld()
	{
		world_.camX = camera_.x;
		world_.camY = camera_.y;
		world_.halfWidth = stage_.view(camera_.x, camera_.y).halfWidth();
		world_.screenEdge = world_.halfWidth - (float)std::max(stage_.screenleft, stage_.screenright);
		world_.leftBound = stage_.leftbound;
		world_.rightBound = stage_.rightbound;
	}

	// A player's state, like Ikemen's debug display (Ctrl+D): state, animation, control, time, types, motion.
	void drawStateInfo(const Character &c, const char *who, float x, float y)
	{
		drawTextf(x, y, 2, TEXT, "%s STATE %d (FROM %d)  TIME %d", who, c.stateNo, c.prevStateNo, c.stateTime);
		drawTextf(x, y + 18, 2, TEXT, "ANIM %d ELEM %d  CTRL %d  TYPE %c %c %c", c.animNo, c.anim.curElem() + 1,
			c.ctrl ? 1 : 0, c.stateType, c.moveType, c.physics);
		drawTextf(x, y + 36, 2, DIM, "POS %.1f,%.1f  VEL %.2f,%.2f  FACING %d", c.x, c.y, c.vx, c.vy, c.facing);
		const GetHitVars &v = c.getHitVars();
		drawTextf(x, y + 54, 2, TEXT, "LIFE %d  POWER %d  CONTACT %d  HITS %d", c.life, c.power, c.moveContact(),
			(int)v.hitcount);
		drawTextf(x, y + 72, 2, DIM, "HITPAUSE %d  SHAKE %d  HITTIME %d", c.hitPauseTime(), v.hitshaketime, v.hittime);
		// What it tried that isn't built yet.
		std::string missing;
		for (const auto &[type, count] : c.unsupportedRuns())
			missing += type + " ";
		if (!missing.empty())
			drawText(("NOT YET: " + missing).c_str(), x, y + 90, 2, 0xFFFF9060);
	}

	// Player 1's commands as they complete (one entry each time a command turns on), for the debug list.
	void recordCommands(const Character &c)
	{
		const std::vector<std::string> now = c.commands.activeNames();
		for (const std::string &name : now) {
			if (std::find(lastActive_.begin(), lastActive_.end(), name) == lastActive_.end()) {
				history_.insert(history_.begin(), { name, 0 });
				logf("p1 command: %s", name.c_str());
			}
		}
		lastActive_ = now;
		for (auto &entry : history_)
			entry.second++;
		if (history_.size() > 12)
			history_.resize(12);
	}

	// Player 1's held buttons and recent commands, down the right side.
	void drawCommands()
	{
		const InputBuffer &b = players_[0].commands.buffer();
		std::string held;
		const std::pair<Key, const char *> keys[] = { { Key::U, "U" }, { Key::D, "D" }, { Key::B, "B" }, { Key::F, "F" },
			{ Key::x, "x" }, { Key::y, "y" }, { Key::z, "z" }, { Key::a, "a" }, { Key::b, "b" }, { Key::c, "c" } };
		for (const auto &[k, name] : keys) {
			if (b.state(k, false, false, true) > 0)
				held += std::string(name) + " ";
		}
		const float x = 1110;
		drawText("P1 INPUT", x, 90, 2, DIM);
		drawText(held.empty() ? "-" : held.c_str(), x, 110, 2, TEXT);
		drawText("P1 COMMANDS", x, 140, 2, DIM);
		float y = 160;
		for (const auto &[name, age] : history_) {
			// Fresh ones bright, fading over a second.
			const uint32_t alpha = (uint32_t)std::max(0x60, 0xFF - age * 3);
			drawText(name.c_str(), x, y, 2, alpha << 24 | 0xFFE060);
			y += 18;
		}
	}

	// Where player i may stand: inside the stage's bounds and the screen's edges. The camera follows, but only as far
	// as keeps the other player on screen, so the screen's edge is what stops a player walking away.
	void limits(int i, float edge, float &lo, float &hi) const
	{
		const float other = players_[1 - i].worldX();
		const float camMin = std::max((float)stage_.camera.boundleft, other - edge);
		const float camMax = std::min((float)stage_.camera.boundright, other + edge);
		lo = std::max(stage_.leftbound, camMin - edge);
		hi = std::min(stage_.rightbound, camMax + edge);
	}

	void keepInBounds(int i, float edge)
	{
		float lo, hi;
		limits(i, edge, lo, hi);
		players_[i].setWorldX(std::clamp(players_[i].worldX(), lo, hi));
	}

	// Players' bodies can't overlap: where they do, each moves back half the overlap. One stopped by a wall or the
	// screen's edge leaves the rest to the other, so walking into a cornered player pushes against them.
	// One above the other's head (a jump over) passes freely.
	void pushApart(float edge)
	{
		Character &a = players_[0], &b = players_[1];
		float al, ar, at, bl, br, bt;
		a.body(al, ar, at);
		b.body(bl, br, bt);
		if (a.worldY() <= bt || b.worldY() <= at)
			return;
		// Who is on the left: by position, or by facing when exactly level.
		const bool aLeft = a.worldX() != b.worldX() ? a.worldX() < b.worldX() : a.facing > 0;
		const float overlap = aLeft ? ar - bl : br - al;
		if (overlap <= 0)
			return;
		Character &left = aLeft ? a : b, &right = aLeft ? b : a;
		const int li = aLeft ? 0 : 1, ri = 1 - li;
		left.addWorldX(-overlap / 2);
		right.addWorldX(overlap / 2);
		// Whatever a wall or the screen's edge took back from one goes to the other.
		const float lx = left.worldX(), rx = right.worldX();
		keepInBounds(li, edge);
		keepInBounds(ri, edge);
		right.addWorldX(left.worldX() - lx);
		left.addWorldX(-(rx - right.worldX()));
		keepInBounds(li, edge);
		keepInBounds(ri, edge);
	}

	// The stage's shadow strength, faded out as the character rises through fade.range.
	int shadowFor(const Character &c) const
	{
		const Stage::Shadow &s = stage_.shadow;
		float k = 1;
		if (s.fadeBegin != s.fadeEnd) {
			if (c.worldY() <= (float)s.fadeEnd)
				k = 0;
			else if (c.worldY() < (float)s.fadeBegin)
				k = (c.worldY() - s.fadeEnd) / (float)(s.fadeBegin - s.fadeEnd);
		}
		return (int)(s.intensity * k);
	}

	Stage stage_;
	Character players_[2];
	Camera camera_;
	FightWorld world_;
	Match match_;
	FightHud hud_;
	FightEffects effects_;
	SoundBank commonSound_;
	float speed_ = 0;
	bool quickStart_ = false;
	// The computer player for player 2 (MUGEN's button jamming), at the hardest level.
	static constexpr int AI_LEVEL = 8;
	AiInput ai_;
	bool forceCpu_ = false;
	bool showInfo_ = true;
	Press lbPress_, ltPress_;
	std::vector<std::string> lastActive_;
	std::vector<std::pair<std::string, int>> history_;
};

class AnimationViewer {
public:
	AnimationViewer(const AnimationTable &table, SpriteRenderer &renderer) : table_(table), renderer_(renderer)
	{
		for (const auto &[no, anim] : table.all())
			actions_.push_back(no);
		// Start on action 0, standing.
		for (size_t i = 0; i < actions_.size(); i++) {
			if (actions_[i] == 0)
				current_ = i;
		}
		if (!actions_.empty())
			load();
	}

	void update(Buttons &b, const platform::Pad &pad)
	{
		updatePalette(b, pad, renderer_);
		if (actions_.empty())
			return;
		const size_t n = actions_.size();
		const size_t was = current_;
		if (b.right.update(pad.right))
			current_ = (current_ + 1) % n;
		if (b.left.update(pad.left))
			current_ = (current_ + n - 1) % n;
		if (b.rb.update(pad.rb))
			current_ = (current_ + 10) % n;
		if (b.lb.update(pad.lb))
			current_ = (current_ + n - 10 % n) % n;
		if (current_ != was)
			load();
		if (b.a.update(pad.a))
			paused_ = !paused_;
		if (b.x.update(pad.x))
			facing_ = -facing_;
		if (b.y.update(pad.y))
			showClsn_ = !showClsn_;
		const bool step = b.b.update(pad.b);
		if (!paused_ || step)
			anim_.action();
	}

	void draw()
	{
		drawGround();
		if (actions_.empty()) {
			drawText("NO ACTIONS LOADED", 40, 40, 4, ERROR_TEXT);
			return;
		}
		renderer_.drawAnimation(anim_, GROUND_X, GROUND_Y, SCALE, facing_);
		const AnimFrame *f = anim_.drawFrame();
		if (showClsn_ && f) {
			for (const Clsn &box : f->clsn2)
				drawClsn(box, facing_, 0x4080FF);
			for (const Clsn &box : f->clsn1)
				drawClsn(box, facing_, 0xFF4040);
		}

		drawTextf(40, 30, 4, TEXT, "ACTION %d%s", actions_[current_], paused_ ? "   (PAUSED)" : "");
		drawTextf(40, 75, 3, DIM, "%zu / %zu    ELEMENT %d / %zu    TIME %d / %d    ANIMTIME %d", current_ + 1,
			actions_.size(), anim_.curElem() + 1, anim_.frames.size(), anim_.curElemTime(), f ? f->time : 0,
			anim_.animTime());
		if (f) {
			drawTextf(40, 105, 2, DIM, "SPRITE %d,%d   OFFSET %d,%d   FLIP %s%s   TRANS %s   LOOPSTART %d   LOOPS %d",
				anim_.spriteGroup(), anim_.spriteNumber(), f->xoffset, f->yoffset, f->hflip < 0 ? "H" : "",
				f->vflip < 0 ? "V" : (f->hflip < 0 ? "" : "-"), transName(f->trans), anim_.loopstart + 1,
				anim_.loopCount());
			drawTextf(40, 125, 2, DIM, "BOXES: %zu ATTACK (RED), %zu HURT (BLUE)%s", f->clsn1.size(), f->clsn2.size(),
				showClsn_ ? "" : " - HIDDEN");
		}
		drawText("LEFT/RIGHT: ACTION  A: PAUSE  B: STEP  X: TURN  Y: BOXES  UP/DOWN: PALETTE  TAB/LB+START: NEXT", 40, 680,
			2, DIM);
	}

private:
	void load() { table_.get(actions_[current_], anim_); }

	const AnimationTable &table_;
	SpriteRenderer &renderer_;
	std::vector<int32_t> actions_;
	size_t current_ = 0;
	Animation anim_;
	int facing_ = 1;
	bool paused_ = false;
	bool showClsn_ = true;
};

class SpriteViewer {
public:
	SpriteViewer(const Sff &sff, SpriteRenderer &renderer) : sff_(sff), renderer_(renderer)
	{
		// Start on 0,0, a character's standing pose, wherever it sits in the file.
		if (const Sprite *stand = sff.get(0, 0))
			current_ = (size_t)(stand - sff.sprites().data());
	}

	void update(Buttons &b, const platform::Pad &pad)
	{
		updatePalette(b, pad, renderer_);
		const size_t n = sff_.sprites().size();
		if (!n)
			return;
		if (b.right.update(pad.right))
			current_ = (current_ + 1) % n;
		if (b.left.update(pad.left))
			current_ = (current_ + n - 1) % n;
		if (b.rb.update(pad.rb))
			current_ = (current_ + 10) % n;
		if (b.lb.update(pad.lb))
			current_ = (current_ + n - 10 % n) % n;
	}

	void draw()
	{
		drawGround();
		const size_t n = sff_.sprites().size();
		if (!n)
			return;
		const Sprite &s = sff_.sprites()[current_];
		// Sprites whose axis would put them off screen (portraits, effects) are centred instead.
		float x = GROUND_X, y = GROUND_Y;
		const float w = s.width * SCALE, h = s.height * SCALE;
		const bool fits = x - s.axisX * SCALE >= 0 && y - s.axisY * SCALE >= 150 &&
			x - s.axisX * SCALE + w <= platform::SCREEN_WIDTH && y - s.axisY * SCALE + h <= 660;
		if (!fits) {
			x = (platform::SCREEN_WIDTH - w) / 2 + s.axisX * SCALE;
			y = 150 + (510 - h) / 2 + s.axisY * SCALE;
		}
		SpriteDraw how;
		how.scaleX = how.scaleY = SCALE;
		renderer_.drawSprite(s.group, s.number, x, y, how);

		const int palette = renderer_.palette() >= 0 ? renderer_.palette() : s.palette;
		drawTextf(40, 30, 4, TEXT, "SPRITE %u,%u", s.group, s.number);
		drawTextf(40, 75, 3, DIM, "%zu / %zu    %u X %u    AXIS %d,%d    PALETTE %d%s", current_ + 1, n, s.width,
			s.height, s.axisX, s.axisY, palette, renderer_.palette() < 0 ? " (OWN)" : "");
		if (!fits)
			drawText("(CENTRED: ITS AXIS PUTS IT OFF SCREEN)", 40, 110, 2, DIM);
		drawText("LEFT/RIGHT: SPRITE   LB/RB: 10 AT A TIME   UP/DOWN: PALETTE   TAB/LB+START: NEXT", 40, 680, 2, DIM);
	}

private:
	const Sff &sff_;
	SpriteRenderer &renderer_;
	size_t current_ = 0;
};

enum class Screen { Stage, Animations, Sprites };

} // namespace

int main(int argc, char **argv)
{
	GameConfig config;
	std::string configError;
	const bool configLoaded = config.load("data/mugen360.ini", configError);
	// Test-run options (see InputScript).
	std::string inputPath, input2Path;
	std::map<uint32_t, std::string> shots;
	uint32_t quitFrame = UINT32_MAX;
	int32_t testPower = -1;
	bool trace = false, quickStart = false, cpu = false;
	for (int i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		if (arg == "--input" && i + 1 < argc)
			inputPath = argv[++i];
		else if (arg == "--input2" && i + 1 < argc)
			input2Path = argv[++i];
		else if (arg == "--shot" && i + 2 < argc) {
			const uint32_t frame = (uint32_t)text::atoi(argv[i + 1]);
			shots[frame] = argv[i + 2];
			i += 2;
		} else if (arg == "--quit" && i + 1 < argc)
			quitFrame = (uint32_t)text::atoi(argv[++i]);
		else if (arg == "--trace")
			trace = true;
		else if (arg == "--quickstart")
			quickStart = true;
		else if (arg == "--cpu")
			cpu = true;
		else if (arg == "--power" && i + 1 < argc)
			testPower = text::atoi(argv[++i]);
		else if (arg == "--p1" && i + 1 < argc)
			config.player1 = argv[++i];
		else if (arg == "--p2" && i + 1 < argc)
			config.player2 = argv[++i];
		else if (arg == "--stage" && i + 1 < argc)
			config.stage = argv[++i];
	}
	const bool testRun = !inputPath.empty() || !shots.empty() || quitFrame != UINT32_MAX;
	if (!platform::init(testRun))
		return 1;
	logf("mugen360: milestone 7a, configurable game content");
	if (!configLoaded)
		logf("mugen360: %s", configError.c_str());
	logf("game: p1=%s p2=%s stage=%s", config.player1.c_str(), config.player2.c_str(), config.stage.c_str());
	InputScript script, script2;
	if (!inputPath.empty() && !script.load(inputPath))
		logf("mugen360: can't read input script %s", inputPath.c_str());
	if (!input2Path.empty() && !script2.load(input2Path))
		logf("mugen360: can't read input script %s", input2Path.c_str());

	{
		std::string error;
		StageDemo stage;
		const bool stageLoaded = stage.load(config, error, trace, quickStart, cpu);
		if (stageLoaded && testPower >= 0)
			stage.setTestPower(testPower);
		if (!stageLoaded)
			logf("mugen360: %s", error.c_str());
		// The viewers get a Kung Fu Man of their own.
		Character kfm;
		const bool kfmLoaded = kfm.load(config.player1, 1, error);
		if (!kfmLoaded)
			logf("mugen360: %s", error.c_str());
		SpriteRenderer renderer(kfm.sff());
		AnimationViewer animViewer(kfm.anims(), renderer);
		SpriteViewer spriteViewer(kfm.sff(), renderer);
		Buttons buttons;
		Screen screen = Screen::Stage;

		for (uint32_t frame = 0; frame <= quitFrame && platform::beginFrame(BACKGROUND); frame++) {
			const platform::Pad pad = inputPath.empty() ? platform::pad(0) : script.at(frame);
			if (buttons.back.update(pad.back))
				break;
			if (const auto shot = shots.find(frame); shot != shots.end())
				platform::requestScreenshot(shot->second.c_str());
			if (buttons.menu.update(pad.menu))
				screen = screen == Screen::Stage ? Screen::Animations
					: screen == Screen::Animations ? Screen::Sprites
					: Screen::Stage;
			platform::Pad gameplayPad = pad;
			if (pad.menu) {
				gameplayPad.start = false;
				gameplayPad.lb = false;
			}
			const bool loaded = screen == Screen::Stage ? stageLoaded : kfmLoaded;
			if (!loaded) {
				drawText("COULDN'T LOAD:", 40, 40, 4, ERROR_TEXT);
				drawText(error.c_str(), 40, 90, 2, TEXT);
				drawText("TAB / LB+START: NEXT SCREEN   BACK: QUIT", 40, 680, 2, DIM);
			} else if (screen == Screen::Stage) {
				stage.update(buttons, gameplayPad, inputPath.empty() ? platform::pad(1) : script2.at(frame));
				stage.draw();
			} else if (screen == Screen::Animations) {
				animViewer.update(buttons, gameplayPad);
				animViewer.draw();
			} else {
				spriteViewer.update(buttons, gameplayPad);
				spriteViewer.draw();
			}
			platform::endFrame();
		}
	}

	logf("mugen360: bye");
	platform::shutdown();
	return 0;
}
