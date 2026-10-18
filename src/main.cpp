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
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
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
#include "core/fight_screen.h"
#include "core/hud.h"
#include "core/motif.h"
#include "core/music.h"
#include "core/select_screen.h"
#include "core/title_screen.h"
#include "core/log.h"
#include "core/match.h"
#include "core/select.h"
#include "core/text.h"
#include "core/sff.h"
#include "core/sound.h"
#include "core/sprite_renderer.h"
#include "core/stage.h"
#include "core/version.h"
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
// frame 0, "--cpu" makes player 2 the computer, "--trace" logs every state change and hit, "--show" keeps the window on
// screen during a test run so it can be watched. A script line is "frames keys...": hold those keys for that
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

// What every fight uses and no fight changes: the shared effects (fightfx), the shared sounds (common.snd) and the
// screenpack's fight screen. Loaded with the first fight and kept, so later fights don't read them again (the 360's
// drives are slow); each fight resets their state.
struct FightAssets {
	FightEffects effects;
	SoundBank commonSound;
	FightScreen fightScreen;
	bool loaded = false, fightScreenLoaded = false;
	std::string fightError;

	bool load(const GameConfig &config, std::string &error)
	{
		if (loaded)
			return true;
		if (!effects.load(FIGHTFX_SFF, FIGHTFX_AIR, error) || !commonSound.load(COMMON_SND, error))
			return false;
		fightScreenLoaded = !config.fight.empty() && fightScreen.load(config.fight, fightError);
		if (config.fight.empty())
			fightError = "none";
		loaded = true;
		return true;
	}
};

// The stage with two Kung Fu Men on it, each run by his own state files (milestone 5): everything he does comes from
// kfm.cns, kfm.cmd and common1.cns.
class StageDemo {
public:
	// quickStart: no intros or announcement, the fight starts at once (for scripted tests).
	// cpu: player 2 is the computer even with a controller (or a test script) for it.
	explicit StageDemo(FightAssets &assets)
		: fightScreen_(assets.fightScreen), effects_(assets.effects), commonSound_(assets.commonSound), assets_(assets)
	{
	}

	bool load(const GameConfig &config, std::string &error, bool trace, bool quickStart, bool cpu)
	{
		if (!stage_.load(config.stage, error))
			return false;
		if (!assets_.load(config, error))
			return false;
		effects_.clear();
		// The screenpack's fight screen sets the round's timings; without one, the plain HUD and default timings.
		const std::string &fightError = assets_.fightError;
		if (assets_.fightScreenLoaded) {
			RoundTimings timings = fightScreen_.timings();
			// Turns bouts and Survival matches are one round each (Ikemen's matchWins.single = 1 for them).
			if (config.mode == "turns" || config.mode == "survival")
				timings.matchWins = 1;
			match_.setTimings(timings);
			const View area = stage_.view(0, 0);
			fightScreen_.setViewport(area.originX, area.width);
			fightScreen_.setMatchInfo(config.mode, config.winCount);
		}
		else
			logf("fightscreen: %s; using the plain HUD", config.fight.empty() ? "none" : fightError.c_str());
		world_.effects = &effects_;
		world_.commonSound = &commonSound_;
		if (trace) {
			players_[0].traceLabel = "p1";
			players_[1].traceLabel = "p2";
		}
		const std::string characterDefs[2] = { config.player1, config.player2 };
		const int palettes[2] = { config.p1Palette, config.p2Palette };
		for (int i = 0; i < 2; i++) {
			if (!players_[i].load(characterDefs[i], palettes[i], error))
				return false;
			players_[i].setGameWidth(stage_.gameWidth);
			players_[i].playerNo = i;
			players_[i].setOpponent(&players_[1 - i]);
			players_[i].setWorld(&world_);
		}
		quickStart_ = quickStart;
		forceCpu_ = cpu;
		training_ = config.mode == "training";
		match_.setTraining(training_);
		newMatch();
		return true;
	}

	void newMatch()
	{
		matchCount_++;
		match_.newMatch();
		fightScreen_.newMatch();
		startRound(true);
	}
	uint32_t matchCount() const { return matchCount_; }
	const MusicTrack &music() const { return stage_.music; }
	void setTestPower(int32_t value)
	{
		for (Character &c : players_)
			c.power = std::clamp(value, 0, 3000);
	}
	void setTestLife(int side, int32_t value)
	{
		if (side >= 0 && side < 2)
			players_[side].life = std::clamp(value, 1, players_[side].lifeMax());
	}
	void setFighterStats(int side, int32_t life, int32_t power)
	{
		if (side < 0 || side >= 2)
			return;
		players_[side].life = std::clamp(life, 1, players_[side].lifeMax());
		players_[side].power = std::clamp(power, 0, players_[side].powerMax());
	}
	int32_t fighterLife(int side) const { return players_[side].life; }
	int32_t fighterLifeMax(int side) const { return players_[side].lifeMax(); }
	int32_t fighterPower(int side) const { return players_[side].power; }
	int32_t timeLeft() const { return match_.timeLeft(); }

	// A round begins: camera, stage and characters back to the start (Ikemen's resetRound).
	void startRound(bool newMatch)
	{
		match_.newRound();
		stage_.reset();
		hud_.reset();
		fightScreen_.reset();
		effects_.clear();
		CameraSpace space;
		space.localScale = stage_.localScale;
		space.gameWidth = stage_.gameWidth;
		space.gameHeight = 240;
		space.screenLeft = (float)stage_.screenleft * stage_.localScale;
		space.screenRight = (float)stage_.screenright * stage_.localScale;
		space.zoffset = (float)stage_.zoffset * stage_.localScale;
		space.localHeight = stage_.localHeight;
		space.heightScale = stage_.view(0, 0).scale;
		camera_.reset(stage_.camera, space);
		updateWorld();
		for (int i = 0; i < 2; i++)
			players_[i].startRound((float)stage_.p[i].x * stage_.localScale, (float)stage_.p[i].y * stage_.localScale,
				stage_.p[i].facing, newMatch);
		if (quickStart_) {
			match_.skipIntro();
			fightScreen_.skipIntro();
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
		// The KO and winner calls play at full speed through the KO slowdown.
		if (!effects_.paused())
			fightScreen_.frame(match_);
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
		const Match::Event event =
			globallyPaused ? Match::Event::None : match_.step(players_[0], players_[1], fightScreen_);
		if (event == Match::Event::NextRound) {
			startRound(false);
			return;
		}
		if (event == Match::Event::MatchOver) {
			// Back to the select screen when there is one; else the next match.
			if (selectAfterMatch_)
				finished_ = true;
			else
				newMatch();
			return;
		}
		match_.fillWorld(world_);
		updateWorld();
		camera_.resetTracking();
		const float edge = world_.screenEdge;
		// Player 2 is the computer unless a second controller is plugged in.
		const bool cpu = forceCpu_ || (!training_ && !pad2.connected);
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
		// The fight screen follows the characters' states, before hits land (Ikemen's FightScreen.step).
		if (!globallyPaused)
			fightScreen_.step(match_, players_[0], players_[1]);
		// Players stay on screen and in the stage, and push each other apart.
		for (int i = 0; i < 2; i++) {
			if (players_[i].screenBound())
				keepInBounds(i, edge);
		}
		// Where the players are for the camera (Ikemen's Char.track, after their update): their x, moving at their
		// velocity unless paused.
		for (int i = 0; i < 2; i++) {
			const Character &c = players_[i];
			const float velocity = run[i] && !c.hitPause() ? c.vx * c.localScale * (float)c.facing : 0.0f;
			camera_.track(c.worldX(), c.worldX(), velocity, c.worldY());
		}
		// Only where their hurt boxes touch, as MUGEN does: a thrown player (no hurt box) isn't pushed off.
		if (players_[0].playerPush() && players_[1].playerPush() && players_[0].clsnTouch(players_[1], 2, 2))
			pushApart(edge);
		// Attacks land: each player's HitDef against the other.
		for (int i = 0; i < 2; i++)
			if (run[i]) players_[i].beginHitDetection();
		for (int i = 0; i < 2; i++)
			if (run[i]) players_[i].hitDetection();
		for (int i = 0; i < 2; i++)
			if (run[i]) players_[i].tick();
		if (training_) {
			// Ikemen's default training values: both life bars recover and both power bars stay full. The dummy has no
			// AI unless --cpu explicitly asks for it.
			for (Character &player : players_)
				player.trainingRestore();
		}
		// The camera runs after the characters, pauses included (Ikemen's System.action).
		camera_.action();
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
		const View view = fightView(effects_.shakeX(), effects_.shakeY());
		const PalFX *stagePalFX = effects_.stagePalFX();
		const float stageBrightness = effects_.superBrightness();
		// Ikemen's order: the fight screen's layer -1 and 0 behind the characters, 1 and 2 in front.
		if (stage_.ikemen)
			stage_.draw(-1, view, stagePalFX, stageBrightness);
		stage_.draw(0, view, stagePalFX, stageBrightness);
		fightScreen_.draw(-1, match_, players_[0], players_[1]);
		effects_.draw(view, (float)stage_.zoffset * stage_.localScale, -1);
		fightScreen_.draw(0, match_, players_[0], players_[1]);
		// Player 1 in front.
		for (int i = 1; i >= 0; i--)
			players_[i].draw(view, (float)stage_.zoffset * stage_.localScale, shadowFor(players_[i]), stage_.shadow.yscale,
				stage_.reflection);
		effects_.draw(view, (float)stage_.zoffset * stage_.localScale, 0);
		stage_.draw(1, view, stagePalFX, stageBrightness);
		fightScreen_.draw(1, match_, players_[0], players_[1]);
		effects_.draw(view, (float)stage_.zoffset * stage_.localScale, 1);
		// A pillarboxed stage: black at the sides, over whatever of the stage and the fighters reached past it.
		if (view.originX > 0) {
			platform::fillRect(0, 0, view.originX, platform::SCREEN_HEIGHT, 0xFF000000);
			platform::fillRect(view.originX + view.width, 0, view.originX, platform::SCREEN_HEIGHT, 0xFF000000);
		}
		fightScreen_.draw(2, match_, players_[0], players_[1]);
		if (fightScreen_.loaded())
			fightScreen_.drawFade();
		else
			hud_.draw(match_, players_[0], players_[1], fightScreen_.roundCalled(), fightScreen_.fightCalled());
		if (players_[1].aiLevel > 0)
			drawTextf(platform::SCREEN_WIDTH - 40 - 6 * 2 * 9, 176, 2, 0xFFFFD040, "CPU LV %d", players_[1].aiLevel);
		// Which build this is, faint in the bottom-right corner (inside a TV's overscan), so a console's build can be
		// told apart.
		const char *version = buildVersion();
		drawText(version, platform::SCREEN_WIDTH - 48 - 6 * 2 * (float)strlen(version), platform::SCREEN_HEIGHT - 40, 2,
			0x90FFFFFF);
		if (showInfo_) {
			for (const Character &c : players_)
				c.drawBoxes(view, (float)stage_.zoffset * stage_.localScale);
			drawTextf(30, 200, 2, DIM, "%s   CAMERA %.1f,%.1f   FRAME %u   ROUNDSTATE %d", stage_.name.c_str(), camera_.x,
				camera_.y, world_.gameTime, (int)match_.roundState());
			drawStateInfo(players_[0], "P1", 30, 224);
			drawStateInfo(players_[1], "P2", 560, 224);
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
		world_.halfWidth = fightView(0, 0).halfWidth();
		world_.screenEdge = world_.halfWidth - (float)std::max(stage_.screenleft, stage_.screenright) * stage_.localScale;
		world_.leftBound = stage_.leftbound * stage_.localScale;
		world_.rightBound = stage_.rightbound * stage_.localScale;
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
		drawText("P1 INPUT", x, 200, 2, DIM);
		drawText(held.empty() ? "-" : held.c_str(), x, 220, 2, TEXT);
		drawText("P1 COMMANDS", x, 250, 2, DIM);
		float y = 270;
		for (const auto &[name, age] : history_) {
			// Fresh ones bright, fading over a second.
			const uint32_t alpha = (uint32_t)std::max(0x60, 0xFF - age * 3);
			drawText(name.c_str(), x, y, 2, alpha << 24 | 0xFFE060);
			y += 18;
		}
	}

	// Where player i may stand (Ikemen's xScreenBound): inside the camera's screen less the stage's screenleft /
	// screenright (xmin / xmax), and inside the stage's leftbound / rightbound.
	void limits(int, float, float &lo, float &hi) const
	{
		const float scale = stage_.localScale;
		lo = std::max(camera_.xmin(), stage_.leftbound * scale);
		hi = std::min(camera_.xmax(), stage_.rightbound * scale);
		// Ikemen's Clamp keeps the minimum when the two cross.
		hi = std::max(hi, lo);
	}

	// The fight's view: the camera's position (less any shake) and zoom, about the ground line.
	View fightView(float shakeX, float shakeY) const
	{
		View view = stage_.view(camera_.x - shakeX, camera_.y - shakeY);
		view.zoomX = view.zoomY = camera_.scale;
		view.ground = (float)stage_.zoffset * stage_.localScale;
		return view;
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
		const float fadeEnd = (float)s.fadeEnd * stage_.localScale, fadeBegin = (float)s.fadeBegin * stage_.localScale;
		if (fadeBegin != fadeEnd) {
			if (c.worldY() <= fadeEnd)
				k = 0;
			else if (c.worldY() < fadeBegin)
				k = (c.worldY() - fadeEnd) / (fadeBegin - fadeEnd);
		}
		return (int)(s.intensity * k);
	}

	Stage stage_;
	// Matches started in this fight: each new one restarts the stage's music.
	uint32_t matchCount_ = 0;
	Character players_[2];
	Camera camera_;
	FightWorld world_;
	Match match_;
	FightScreen &fightScreen_;
	FightHud hud_;
	FightEffects &effects_;
	SoundBank &commonSound_;
	FightAssets &assets_;
	float speed_ = 0;
	bool quickStart_ = false;
	// The computer player for player 2 (MUGEN's button jamming), at the hardest level.
	static constexpr int AI_LEVEL = 8;
	AiInput ai_;
	bool forceCpu_ = false;
	bool training_ = false;
	bool selectAfterMatch_ = false, finished_ = false;

public:
	// After the match, the game goes back to the select screen (finished() turns true) instead of starting another.
	void returnToSelectAfterMatch() { selectAfterMatch_ = true; }
	bool finished() const { return finished_; }
	// Each side's matches won in a row, to carry into the next fight.
	int winCount(int side) const { return fightScreen_.winCount(side); }
	int winner() const { return match_.winner(); }
	// The debug text and hitboxes (Q / LB); off, as Ikemen shows none.
	bool showInfo_ = false;
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

enum class Screen { Title, Select, Stage, Animations, Sprites };

} // namespace

int main(int argc, char **argv)
{
	GameConfig config;
	std::string configError;
	const bool configLoaded = config.load("data/mugen360.ini", configError);
	// Test-run options (see InputScript).
	std::string inputPath, input2Path, forcedMode;
	std::map<uint32_t, std::string> shots;
	uint32_t quitFrame = UINT32_MAX;
	int32_t testPower = -1, testP2Life = -1;
	// --noai: player 2 stays idle even in modes against the computer, so a test script's hits land the same way.
	bool noAi = false;
	bool trace = false, quickStart = false, cpu = false, show = false, forceSelect = false, forceTitle = false;
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
		else if (arg == "--noai")
			noAi = true;
		else if (arg == "--mode" && i + 1 < argc)
			forcedMode = text::toLower(argv[++i]);
		else if (arg == "--show")
			show = true;
		else if (arg == "--select")
			forceSelect = true;
		else if (arg == "--title")
			forceTitle = forceSelect = true;
		else if (arg == "--power" && i + 1 < argc)
			testPower = text::atoi(argv[++i]);
		else if (arg == "--p2life" && i + 1 < argc)
			testP2Life = text::atoi(argv[++i]);
		else if (arg == "--p1" && i + 1 < argc)
			config.player1 = argv[++i];
		else if (arg == "--p2" && i + 1 < argc)
			config.player2 = argv[++i];
		else if (arg == "--stage" && i + 1 < argc)
			config.stage = argv[++i];
	}
	const bool testRun = !inputPath.empty() || !shots.empty() || quitFrame != UINT32_MAX;
	if (!platform::init(testRun && !show))
		return 1;
	logf("mugen360: milestone 10d, Survival, build %s", buildVersion());
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
		// With a roster in data/mugen360.ini the game starts at the select screen (test runs skip it unless --select);
		// else straight into the configured fight.
		SelectScreen select;
		const bool useSelect = (!config.characters.empty() && !testRun) || forceSelect;
		const bool selectLoaded = useSelect && select.load(config, error);
		if (useSelect && !selectLoaded)
			logf("mugen360: %s", error.c_str());
		// The screenpack: Ikemen's select screen, and the title screen and its menu first (test runs only with --title).
		Motif motif;
		const bool motifLoaded = selectLoaded && !config.motif.empty() && motif.load(config.motif, error);
		if (selectLoaded && !config.motif.empty() && !motifLoaded)
			logf("mugen360: %s", error.c_str());
		MotifSelect motifSelect;
		const bool motifSelectLoaded = motifLoaded && motifSelect.load(motif, config, error);
		if (motifLoaded && !motifSelectLoaded)
			logf("mugen360: %s; using the plain select screen", error.c_str());
		TitleScreen title;
		const bool useTitle = motifLoaded && (!testRun || forceTitle);
		const bool titleLoaded = useTitle && title.load(motif, error);
		if (useTitle && !titleLoaded)
			logf("mugen360: %s", error.c_str());
		// The mode picked on the title and whether player 2 is forced to use computer input.
		bool modeCpu = false;
		SelectMode mode = SelectMode::Versus;
		if (forcedMode == "arcade")
			mode = SelectMode::Arcade, modeCpu = true;
		else if (forcedMode == "training")
			mode = SelectMode::Training;
		else if (forcedMode == "quick" || forcedMode == "freebattle")
			mode = SelectMode::QuickMatch, modeCpu = true;
		else if (forcedMode == "teamarcade" || forcedMode == "turnsarcade")
			mode = SelectMode::TeamArcade, modeCpu = true;
		else if (forcedMode == "teamversus" || forcedMode == "turns")
			mode = SelectMode::TeamVersus;
		else if (forcedMode == "survival")
			mode = SelectMode::Survival, modeCpu = true;
		// A new character selection, on whichever select screen there is.
		auto resetSelect = [&](bool secondController) {
			if (motifSelectLoaded)
				motifSelect.reset(mode, secondController && !cpu);
			else
				select.reset(cpu || modeCpu || !secondController);
		};
		std::unique_ptr<StageDemo> stage;
		bool stageLoaded = false;
		// A fight with these characters and stage.
		// Matches won in a row on each side, kept from one fight to the next.
		int winCounts[2] = { 0, 0 };
		GameConfig selectedFight;
		TurnsSeries turns;
		int32_t turnsLife[2] = { -1, -1 }, turnsPower[2] = { 0, 0 };
		// Loaded with the first fight, then reused.
		FightAssets fightAssets;
		auto startFight = [&](const GameConfig &picked) {
			GameConfig fight = picked;
			fight.mode = mode == SelectMode::QuickMatch ? "freebattle"
				: mode == SelectMode::Arcade            ? "arcade"
				: mode == SelectMode::Training          ? "training"
				: mode == SelectMode::TeamVersus || mode == SelectMode::TeamArcade ? "turns"
				: mode == SelectMode::Survival                                      ? "survival"
														: "versus";
			if (fight.mode == "turns" && !fight.team1.empty() && !fight.team2.empty()) {
				const GameConfig::Fighter &p1 = fight.team1[(size_t)turns.active(0)];
				const GameConfig::Fighter &p2 = fight.team2[(size_t)turns.active(1)];
				fight.player1 = p1.def;
				fight.p1Palette = p1.palette;
				fight.player2 = p2.def;
				fight.p2Palette = p2.palette;
				logf("turns: p1 member %d of %d vs p2 member %d of %d", turns.active(0) + 1, turns.size(0),
					turns.active(1) + 1, turns.size(1));
			}
			fight.winCount[0] = winCounts[0];
			fight.winCount[1] = winCounts[1];
			stage = std::make_unique<StageDemo>(fightAssets);
			stageLoaded = stage->load(fight, error, trace, quickStart, (cpu || modeCpu) && !noAi);
			// Turns winners and the Survival player keep their life and power (Ikemen's persistLife).
			if (stageLoaded && (fight.mode == "turns" || fight.mode == "survival"))
				for (int side = 0; side < 2; side++)
					if (turnsLife[side] >= 0)
						stage->setFighterStats(side, turnsLife[side], turnsPower[side]);
			if (stageLoaded && testPower >= 0)
				stage->setTestPower(testPower);
			if (stageLoaded && testP2Life >= 0)
				stage->setTestLife(1, testP2Life);
			if (!stageLoaded)
				logf("mugen360: %s", error.c_str());
			if (selectLoaded)
				stage->returnToSelectAfterMatch();
		};
		Screen screen = Screen::Stage;
		if (titleLoaded) {
			screen = Screen::Title;
		} else if (selectLoaded) {
			resetSelect(inputPath.empty() ? platform::pad(1).connected : !input2Path.empty());
			screen = Screen::Select;
		} else {
			startFight(config);
		}
		// The viewers get a Kung Fu Man of their own.
		Character kfm;
		const bool kfmLoaded = kfm.load(config.player1, 1, error);
		if (!kfmLoaded)
			logf("mugen360: %s", error.c_str());
		SpriteRenderer renderer(kfm.sff());
		AnimationViewer animViewer(kfm.anims(), renderer);
		SpriteViewer spriteViewer(kfm.sff(), renderer);
		Buttons buttons;
		// Where the debug screens go back to.
		Screen gameScreen = screen;
		Music music;
		// What the music was last chosen for ("title", "select", "vs", "match N").
		std::string musicFor;

		// Every 600 frames (10 seconds), a line in the log on how long frames took to update and draw (not counting the
		// wait for the screen), and the textures made meanwhile: the console's log shows where time goes.
		using Clock = std::chrono::steady_clock;
		Clock::time_point frameStart = Clock::now();
		uint64_t frameSum = 0, frameWorst = 0;
		uint32_t frameCount = 0, frameSlow = 0;
		auto finishFrame = [&]() {
			const uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - frameStart).count();
			frameSum += us;
			frameWorst = std::max(frameWorst, us);
			frameSlow += us > 16000 ? 1 : 0;
			if (++frameCount == 600) {
				size_t made = 0;
				uint64_t madeUs = 0;
				SpriteRenderer::takeCreationStats(made, madeUs);
				logf("perf: 600 frames, avg %.1f ms, worst %.1f ms, %u over 16 ms; %zu textures made in %.1f ms; %zu MB "
					 "of textures",
					frameSum / 600 / 1000.0, frameWorst / 1000.0, frameSlow, made, madeUs / 1000.0,
					SpriteRenderer::totalTextureBytes() >> 20);
				frameSum = frameWorst = 0;
				frameCount = frameSlow = 0;
			}
			platform::endFrame();
			frameStart = Clock::now();
		};
		for (uint32_t frame = 0; frame <= quitFrame && platform::beginFrame(BACKGROUND); frame++) {
			const platform::Pad pad = inputPath.empty() ? platform::pad(0) : script.at(frame);
			// A missing --input2 is a connected, idle dummy during scripted fights. Selection separately uses the path's
			// presence so player 1 still picks the opponent, matching the documented headless-test behavior.
			const platform::Pad pad2 = inputPath.empty() ? platform::pad(1) : script2.at(frame);
			const bool secondController = inputPath.empty() ? pad2.connected : !input2Path.empty();
			// Back: out of a fight to the select screen, out of the select screen to the title, else quit. The title
			// handles it itself (it moves to Exit).
			const bool back = buttons.back.update(pad.back);
			// The screenpack select screen handles Back itself (out of the palette menu, else back to the title).
			const bool selectHandlesBack = screen == Screen::Select && motifSelectLoaded;
			if (back && screen == Screen::Stage && selectLoaded) {
				stage.reset();
				resetSelect(secondController);
				screen = gameScreen = Screen::Select;
			} else if (back && screen == Screen::Select && titleLoaded && !selectHandlesBack) {
				title.reset();
				screen = gameScreen = Screen::Title;
			} else if (back && screen != Screen::Title && !selectHandlesBack) {
				break;
			}
			// The music for where the game is, as Ikemen's playBgm calls go: the title's and the select screen's
			// replace whatever plays, the VS screen's only if the screenpack has one, and the stage's starts over with
			// each match (Ikemen's Music.act at round 1).
			std::string wantedMusic;
			if (gameScreen == Screen::Title)
				wantedMusic = "title";
			else if (gameScreen == Screen::Select)
				wantedMusic = motifSelectLoaded && motifSelect.inVersus() ? "vs" : "select";
			else if (gameScreen == Screen::Stage && stage)
				wantedMusic = "match " + std::to_string(stage->matchCount());
			if (wantedMusic != musicFor) {
				musicFor = wantedMusic;
				if (wantedMusic == "title" || wantedMusic == "select")
					music.request(motifLoaded ? motif.music(wantedMusic) : MusicTrack{}, true);
				else if (wantedMusic == "vs")
					music.request(motif.music("vs"), false);
				else if (stage)
					music.restart(stage->music());
			}
			music.update();
			if (const auto shot = shots.find(frame); shot != shots.end())
				platform::requestScreenshot(shot->second.c_str());
			// The debug screens cycle; from the last one, back to the fight (or the select screen before one).
			if (buttons.menu.update(pad.menu)) {
				if (screen == Screen::Animations)
					screen = Screen::Sprites;
				else if (screen == Screen::Sprites)
					screen = gameScreen;
				else
					screen = Screen::Animations;
			}
			platform::Pad gameplayPad = pad;
			if (pad.menu) {
				gameplayPad.start = false;
				gameplayPad.lb = false;
			}
			if (screen == Screen::Title) {
				title.update(gameplayPad, pad2);
				title.draw();
				finishFrame();
				const TitleChoice choice = title.result();
				if (choice == TitleChoice::Exit)
					break;
				if (choice != TitleChoice::None) {
					// Arcade and Quick Match force a CPU opponent. Training leaves player 2 as a stationary dummy.
					modeCpu = choice == TitleChoice::Arcade || choice == TitleChoice::TeamArcade ||
						choice == TitleChoice::QuickMatch || choice == TitleChoice::Survival;
					mode = choice == TitleChoice::QuickMatch ? SelectMode::QuickMatch
						: choice == TitleChoice::Arcade      ? SelectMode::Arcade
						: choice == TitleChoice::TeamArcade  ? SelectMode::TeamArcade
						: choice == TitleChoice::TeamVersus  ? SelectMode::TeamVersus
						: choice == TitleChoice::Training    ? SelectMode::Training
						: choice == TitleChoice::Survival    ? SelectMode::Survival
															 : SelectMode::Versus;
					resetSelect(secondController);
					screen = gameScreen = Screen::Select;
				}
				continue;
			}
			if (screen == Screen::Select && motifSelectLoaded) {
				motifSelect.update(gameplayPad, pad2);
				motifSelect.draw();
				finishFrame();
				if (motifSelect.done()) {
					selectedFight = motifSelect.fight();
					if (mode == SelectMode::TeamVersus || mode == SelectMode::TeamArcade) {
						turns.reset((int32_t)selectedFight.team1.size(), (int32_t)selectedFight.team2.size());
						turnsLife[0] = turnsLife[1] = -1;
						turnsPower[0] = turnsPower[1] = 0;
					}
					startFight(selectedFight);
					screen = gameScreen = Screen::Stage;
				} else if (motifSelect.cancelled()) {
					if (!titleLoaded)
						break;
					title.reset();
					screen = gameScreen = Screen::Title;
				}
				continue;
			}
			if (screen == Screen::Select) {
				select.update(gameplayPad, pad2);
				select.draw();
				if (select.done()) {
					startFight(select.fight());
					screen = gameScreen = Screen::Stage;
				}
				finishFrame();
				continue;
			}
			const bool loaded = screen == Screen::Stage ? stageLoaded : kfmLoaded;
			if (!loaded) {
				drawText("COULDN'T LOAD:", 40, 40, 4, ERROR_TEXT);
				drawText(error.c_str(), 40, 90, 2, TEXT);
				drawText("TAB / LB+START: NEXT SCREEN   BACK: QUIT", 40, 680, 2, DIM);
			} else if (screen == Screen::Stage) {
				stage->update(buttons, gameplayPad, pad2);
				stage->draw();
				// Match over: back to pick again.
				if (stage->finished()) {
					int winner = stage->winner();
					winCounts[0] = stage->winCount(0);
					winCounts[1] = stage->winCount(1);
					int32_t life[2] = { stage->fighterLife(0), stage->fighterLife(1) };
					const int32_t lifeMax[2] = { stage->fighterLifeMax(0), stage->fighterLifeMax(1) };
					const int32_t power[2] = { stage->fighterPower(0), stage->fighterPower(1) };
					const int32_t timeLeft = stage->timeLeft();
					stage.reset();
					bool nextTurn = false;
					if (mode == SelectMode::TeamVersus || mode == SelectMode::TeamArcade) {
						turnsLife[0] = turnsLife[1] = -1;
						if (winner >= 0) {
							const int recovery = turnsRecovery(lifeMax[winner], timeLeft, Match::ROUND_TIME);
							turnsLife[winner] = std::min(lifeMax[winner], life[winner] + recovery);
							turnsPower[winner] = power[winner];
							logf("turns: p%d recovers %d life to %d", winner + 1, turnsLife[winner] - life[winner],
								turnsLife[winner]);
						}
						nextTurn = turns.result(winner);
						if (nextTurn) {
							startFight(selectedFight);
							musicFor.clear();
						} else {
							winner = turns.winner();
							if (winner >= 0)
								logf("turns: team %d wins", winner + 1);
							else
								logf("turns: draw");
						}
					}
					const bool arcadeMode = mode == SelectMode::Arcade || mode == SelectMode::TeamArcade;
					bool nextSurvival = false;
					if (mode == SelectMode::Survival) {
						// The player carries their life and power into the next opponent; the first loss ends the run.
						turnsLife[0] = turnsLife[1] = -1;
						if (winner == 0 && life[0] > 0) {
							turnsLife[0] = life[0];
							turnsPower[0] = power[0];
							nextSurvival = motifSelectLoaded && motifSelect.nextSurvivalMatch();
						}
						if (!nextSurvival)
							logf("survival: run over after %d wins", motifSelectLoaded ? motifSelect.survivalWins() : 0);
					}
					if (nextTurn) {
						screen = gameScreen = Screen::Stage;
					} else if (nextSurvival) {
						logf("survival: player advances");
						screen = gameScreen = Screen::Select;
					} else if (arcadeMode && winner == 0 && motifSelectLoaded && motifSelect.nextArcadeMatch()) {
						logf("arcade: player advances");
						screen = gameScreen = Screen::Select;
					} else {
						if (arcadeMode)
							logf("arcade: %s", winner == 0 ? "route complete" : "route lost");
						resetSelect(secondController);
						screen = gameScreen = Screen::Select;
					}
				}
			} else if (screen == Screen::Animations) {
				animViewer.update(buttons, gameplayPad);
				animViewer.draw();
			} else {
				spriteViewer.update(buttons, gameplayPad);
				spriteViewer.draw();
			}
			finishFrame();
		}
	}

	logf("mugen360: bye (textures %zu MB on a 360)", SpriteRenderer::totalTextureBytes() >> 20);
	platform::shutdown();
	return 0;
}
