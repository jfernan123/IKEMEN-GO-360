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
#include <functional>
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
#include "core/compat.h"
#include "core/file.h"
#include "core/game_config.h"
#include "core/fight_screen.h"
#include "core/hud.h"
#include "core/motif.h"
#include "core/options_screen.h"
#include "core/music.h"
#include "core/select_screen.h"
#include "core/settings.h"
#include "core/title_screen.h"
#include "core/log.h"
#include "core/match.h"
#include "core/select.h"
#include "core/text.h"
#include "core/sff.h"
#include "core/sound.h"
#include "core/sprite_renderer.h"
#include "core/stage.h"
#include "core/storyboard.h"
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
	// The player's options (main owns them; the options screen changes them between fights).
	const Settings *settings = nullptr;

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
		const Settings &settings = *assets_.settings;
		// Rounds to win, draws and the clock come from the options, not fight.def (Ikemen: "Replaced by ingame
		// options"); Turns bouts and Survival matches are one round each.
		RoundTimings timings = assets_.fightScreenLoaded ? fightScreen_.timings() : RoundTimings{};
		timings.matchWins = config.mode == "simul" ? settings.simulWins : settings.matchWins;
		if (config.mode == "turns" || config.mode == "survival")
			timings.matchWins = 1;
		timings.maxDraws = settings.maxDrawGames;
		match_.setTimings(timings);
		match_.setRoundTime(settings.roundTicks(assets_.fightScreenLoaded ? fightScreen_.framesPerCount() : 60));
		aiLevel_ = settings.difficulty;
		ticksPerSecond_ = settings.ticksPerSecond();
		if (assets_.fightScreenLoaded) {
			const View area = stage_.view(0, 0);
			fightScreen_.setViewport(area.originX, area.width);
			fightScreen_.setMatchInfo(config.mode, config.winCount);
		}
		else
			logf("fightscreen: %s; using the plain HUD", config.fight.empty() ? "none" : fightError.c_str());
		world_.effects = &effects_;
		world_.commonSound = &commonSound_;
		// Simul: every member of each team fights at once, the leaders first, then the partners (Ikemen's player
		// order: 1 and 3 on one side, 2 and 4 on the other). A side may have one fighter against two.
		std::string characterDefs[4] = { config.player1, config.player2 };
		int palettes[4] = { config.p1Palette, config.p2Palette, 1, 1 };
		count_ = 2;
		side_[0] = 0;
		side_[1] = 1;
		if (config.mode == "simul" && !config.team1.empty() && !config.team2.empty()) {
			const std::vector<GameConfig::Fighter> *teams[2] = { &config.team1, &config.team2 };
			count_ = 0;
			for (size_t member = 0; member < 2; member++) {
				for (int side = 0; side < 2; side++) {
					if (member >= teams[side]->size())
						continue;
					characterDefs[count_] = (*teams[side])[member].def;
					palettes[count_] = (*teams[side])[member].palette;
					side_[count_++] = side;
				}
			}
		}
		for (int i = 0; i < count_; i++) {
			if (trace)
				players_[i].traceLabel = "p" + std::to_string(i + 1);
			if (!players_[i].load(characterDefs[i], palettes[i], error))
				return false;
			players_[i].setGameWidth(stage_.gameWidth);
			players_[i].setLifeScale((float)settings.life / 100.0f);
			// Win, Lose and TeamSide go by side: a partner is on its leader's side.
			players_[i].playerNo = side_[i];
			players_[i].setWorld(&world_);
		}
		for (int i = 0; i < count_; i++) {
			if (config.mode != "simul") {
				players_[i].setOpponent(&players_[1 - i]);
				continue;
			}
			std::vector<Character *> enemies, partners;
			for (int j = 0; j < count_; j++) {
				if (side_[j] != side_[i])
					enemies.push_back(&players_[j]);
				else if (j != i)
					partners.push_back(&players_[j]);
			}
			players_[i].setTeam(enemies, partners);
		}
		// Each computer partner jams its own buttons.
		for (int i = 2; i < 4; i++)
			ai_[i].seed(0x9E3779B9u * (uint32_t)(i + 1));
		if (config.mode == "simul") {
			std::string teams[2];
			for (int i = 0; i < count_; i++)
				teams[side_[i]] += (teams[side_[i]].empty() ? "" : " and ") + characterDefs[i];
			logf("simul: %s vs %s", teams[0].c_str(), teams[1].c_str());
		}
		// A side with a partner shows Simul's lifebars, faces and names.
		const Character *partners[2] = { nullptr, nullptr };
		for (int i = 2; i < count_; i++)
			partners[side_[i]] = &players_[i];
		fightScreen_.setPartners(partners[0], partners[1]);
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
		for (int i = 0; i < count_; i++)
			players_[i].power = std::clamp(value, 0, 3000);
	}
	// A side's life for a test: its leader's, and in Simul its partner's too.
	void setTestLife(int side, int32_t value)
	{
		if (side < 0 || side >= 2)
			return;
		for (int i = 0; i < count_; i++)
			if (side_[i] == side)
				players_[i].life = std::clamp(value, 1, players_[i].lifeMax());
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
		for (int i = 0; i < count_; i++)
			players_[i].startRound((float)stage_.p[i].x * stage_.localScale, (float)stage_.p[i].y * stage_.localScale,
				stage_.p[i].facing, newMatch);
		if (quickStart_) {
			match_.skipIntro();
			fightScreen_.skipIntro();
			for (int i = 0; i < count_; i++)
				players_[i].roundStartControl();
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
		// Game speed: more or fewer ticks a frame (Ikemen's 60 + GameSpeed * GameSpeedStep ticks a second).
		speed_ += match_.speed() * (float)ticksPerSecond_ / 60.0f;
		while (speed_ >= 1) {
			speed_ -= 1;
			tick(pad1, pad2);
		}
	}

	// One game tick: the round, then each phase for both players before the next (Ikemen's charList order).
	void tick(const platform::Pad &pad1, const platform::Pad &pad2)
	{
		const bool globallyPaused = effects_.paused();
		Character *const fighters[4] = { &players_[0], &players_[1], &players_[2], &players_[3] };
		const Match::Event event =
			globallyPaused ? Match::Event::None : match_.step(fighters, count_, fightScreen_, side_);
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
		players_[1].aiLevel = cpu ? aiLevel_ : 0;
		ai_[1].update((float)aiLevel_, cpu && match_.roundState() == 2);
		// Simul partners are the computer's (Ikemen: a team member without a controller of its own).
		for (int i = 2; i < count_; i++) {
			players_[i].aiLevel = aiLevel_;
			ai_[i].update((float)aiLevel_, match_.roundState() == 2);
		}
		// Each one faces and targets its nearest enemy still in the fight.
		if (count_ > 2)
			for (int i = 0; i < count_; i++)
				players_[i].chooseP2();
		bool run[4] = { false, false, false, false };
		for (int i = 0; i < count_; i++)
			run[i] = !effects_.pausedFor(i);
		players_[0].aiLevel = watch_ ? aiLevel_ : 0;
		if (watch_)
			ai_[0].update((float)aiLevel_, match_.roundState() == 2);
		if (run[0]) {
			InputState input1;
			assets_.settings->toInput(0, pad1, input1);
			players_[0].turnAndInput(watch_ ? ai_[0].state() : input1);
			recordCommands(players_[0]);
		}
		if (run[1]) {
			InputState input2;
			assets_.settings->toInput(1, pad2, input2);
			players_[1].turnAndInput(cpu ? ai_[1].state() : input2);
		}
		for (int i = 2; i < count_; i++)
			if (run[i]) players_[i].turnAndInput(ai_[i].state());
		for (int i = 0; i < count_; i++)
			if (run[i]) players_[i].actionPrepare();
		for (int i = 0; i < count_; i++)
			if (run[i]) players_[i].actionRun();
		// The fight screen follows the characters' states, before hits land (Ikemen's FightScreen.step).
		if (!globallyPaused)
			fightScreen_.step(match_, players_[0], players_[1]);
		// Players stay on screen and in the stage, and push each other apart.
		for (int i = 0; i < count_; i++) {
			if (players_[i].screenBound())
				keepInBounds(i, edge);
		}
		// Where the players are for the camera (Ikemen's Char.track, after their update): their x, moving at their
		// velocity unless paused.
		for (int i = 0; i < count_; i++) {
			const Character &c = players_[i];
			const float velocity = run[i] && !c.hitPause() ? c.vx * c.localScale * (float)c.facing : 0.0f;
			camera_.track(c.worldX(), c.worldX(), velocity, c.worldY());
		}
		// Only where their hurt boxes touch, as MUGEN does: a thrown player (no hurt box) isn't pushed off. Each pair of
		// opponents pushes; teammates pass through each other.
		for (int i = 0; i < count_; i++) {
			for (int j = i + 1; j < count_; j++) {
				if (side_[i] != side_[j] && players_[i].playerPush() && players_[j].playerPush() &&
					players_[i].clsnTouch(players_[j], 2, 2))
					pushApart(i, j, edge);
			}
		}
		// Attacks land: each player's HitDef against the other side.
		for (int i = 0; i < count_; i++)
			if (run[i]) players_[i].beginHitDetection();
		for (int i = 0; i < count_; i++)
			if (run[i]) players_[i].hitDetection();
		for (int i = 0; i < count_; i++)
			if (run[i]) players_[i].tick();
		if (training_) {
			// Ikemen's default training values: both life bars recover and both power bars stay full. The dummy has no
			// AI unless --cpu explicitly asks for it.
			for (int i = 0; i < count_; i++)
				players_[i].trainingRestore();
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
		for (int i = count_ - 1; i >= 0; i--)
			players_[i].draw(view, (float)stage_.zoffset * stage_.localScale, groundFor(players_[i]));
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
			for (int i = 0; i < count_; i++) {
				const Character &c = players_[i];
				c.drawBoxes(view, (float)stage_.zoffset * stage_.localScale);
			}
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
		world_.autoTurn = stage_.autoTurn;
		world_.cameraZoom = camera_.scale;
		world_.stage = &stage_;
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
	void pushApart(int ai, int bi, float edge)
	{
		Character &a = players_[ai], &b = players_[bi];
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
		const int li = aLeft ? ai : bi, ri = aLeft ? bi : ai;
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

	// The stage's shadow and reflection for a character: the shadow faded as it rises through fade.range, offsets
	// in game units.
	GroundDraw groundFor(const Character &c) const
	{
		const float k = shadowFade(c);
		const float scale = stage_.localScale;
		const Stage::Shadow &s = stage_.shadow;
		const uint32_t faded = (uint32_t)std::lround((float)(s.colour >> 16 & 0xFF) * k) << 16 |
			(uint32_t)std::lround((float)(s.colour >> 8 & 0xFF) * k) << 8 | (uint32_t)std::lround((float)(s.colour & 0xFF) * k);
		GroundDraw g;
		g.shadowIntensity = (int)((float)s.intensity * k);
		g.shadowYScale = s.yscale;
		g.shadowColour = faded;
		g.shadowOffsetX = s.offsetX * scale;
		g.shadowOffsetY = s.offsetY * scale;
		g.reflection = stage_.reflection;
		g.reflectionColour = stage_.reflectionColour;
		g.reflectionYScale = stage_.reflectionYScale;
		g.reflectionOffsetX = stage_.reflectionOffsetX * scale;
		g.reflectionOffsetY = stage_.reflectionOffsetY * scale;
		return g;
	}

	// How much of the stage's shadow shows, faded out as the character rises through fade.range.
	float shadowFade(const Character &c) const
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
		return k;
	}

	Stage stage_;
	// Matches started in this fight: each new one restarts the stage's music.
	uint32_t matchCount_ = 0;
	// Players 1 and 2, and in Simul their partners 3 and 4.
	Character players_[4];
	int count_ = 2;
	// Each fighter's side: 0 with player 1, 1 with player 2.
	int side_[4] = { 0, 1, 0, 1 };
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
	// The computer players (MUGEN's button jamming), at the options' Difficulty.
	int aiLevel_ = 5;
	int32_t ticksPerSecond_ = 60;
	AiInput ai_[4];
	bool forceCpu_ = false;
	bool training_ = false;
	// Watch mode: the computer plays player 1 as well (Ikemen's cpuSide[1]).
	bool watch_ = false;
	bool selectAfterMatch_ = false, finished_ = false;

public:
	// After the match, the game goes back to the select screen (finished() turns true) instead of starting another.
	void returnToSelectAfterMatch() { selectAfterMatch_ = true; }
	// For the soaks (13d): a fighter, how many there are, and the round's state.
	const Character &fighter(int i) const { return players_[i]; }
	int fighterCount() const { return count_; }
	int32_t roundState() const { return match_.roundState(); }
	int32_t roundNo() const { return match_.roundNo(); }
	int matchesStarted() const { return (int)matchCount_; }
	void setWatch(bool watch)
	{
		watch_ = watch;
		ai_[0].seed(0x2545F491u);
	}
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

enum class Screen { Title, Options, Select, Stage, Animations, Sprites, Storyboard };

// A storyboard a .def names under [section] key (a character's [Arcade] intro.storyboard, a screenpack's [Files]
// logo.storyboard), found as Ikemen finds it (next to the .def, the game's root, data/); empty if there's none.
std::string storyboardFor(const std::string &defPath, const std::string &section, const std::string &key)
{
	std::vector<uint8_t> bytes;
	if (!readFile(defPath, bytes))
		return {};
	const std::vector<IniSection> ini = parseIni(std::string(bytes.begin(), bytes.end()));
	const IniSection *s = findSection(ini, section);
	const std::string name = s ? std::string(text::trim(s->text(key))) : std::string();
	if (name.empty())
		return {};
	const std::string found = searchFile(name, { defPath, "", "data/" });
	return fileExists(found) ? found : std::string();
}

} // namespace

// --soak [folder] (13d): each character of the roster against the next (the last against the first), the computer
// playing both, for `frames` ticks without intros, on the stages in turn. A pair's notes: states it holds too long
// in a fight (stuck), states and animations it lacks, controllers that run as no-ops. The report is written after
// every pair, so if one crashes the game, the report ends at the pair that did it.
std::string runSoak(const GameConfig &config, const Settings &settings, uint32_t frames, const std::string &path)
{
	std::vector<std::string> roster;
	for (const std::string &c : config.characters) {
		if (c != "randomselect" && c != "dummyslot" && c != "skipslot")
			roster.push_back(c);
	}
	if (roster.empty())
		roster.push_back(config.player1);
	std::vector<std::string> stages = config.stages.empty() ? std::vector<std::string>{ config.stage } : config.stages;
	std::string out = "mugen360 soak report: " + (config.game.empty() ? std::string("(mugen360's own content)") : config.game) +
		", " + std::to_string(frames) + " ticks a pair\n\n";
	FightAssets assets;
	assets.settings = &settings;
	compat::begin();
	int problems = 0;
	for (size_t i = 0; i < roster.size(); i++) {
		GameConfig fight = config;
		fight.mode = "versus";
		fight.player1 = roster[i];
		fight.player2 = roster[(i + 1) % roster.size()];
		fight.stage = stages[i % stages.size()];
		const std::string label = fight.player1 + " vs " + fight.player2 + " on " + fight.stage;
		out += label + ": ";
		platform::writeWholeFile(path.c_str(), out + "(running; if the report ends here, this pair crashed)\n");
		compat::setItem(label);
		StageDemo demo(assets);
		std::string error;
		if (!demo.load(fight, error, false, true, true)) {
			out += "FAILS to load: " + error + "\n";
			problems++;
			continue;
		}
		demo.setWatch(true);
		// Held states: a fighter in one state for 20 seconds of fighting, other than standing, crouching or walking.
		constexpr uint32_t STUCK_TICKS = 1200;
		int32_t lastState[4] = { -1, -1, -1, -1 };
		uint32_t held[4] = { 0, 0, 0, 0 };
		std::vector<std::string> stuck;
		// Whether they really fight: rounds begun and life lost.
		int rounds = 1, lastRound = 1, lastMatch = demo.matchesStarted();
		int32_t lastLife[4] = { -1, -1, -1, -1 };
		int64_t lifeLost = 0;
		Buttons buttons;
		platform::Pad idle;
		for (uint32_t f = 0; f < frames; f++) {
			demo.update(buttons, idle, idle);
			// Drawn now and then, so the drawing code runs too.
			if (f % 60 == 0 && platform::beginFrame(BACKGROUND)) {
				demo.draw();
				platform::endFrame();
			}
			if (demo.roundNo() != lastRound || demo.matchesStarted() != lastMatch) {
				rounds++;
				lastRound = demo.roundNo();
				lastMatch = demo.matchesStarted();
			}
			for (int p = 0; p < demo.fighterCount(); p++) {
				const Character &c = demo.fighter(p);
				if (lastLife[p] > c.life)
					lifeLost += lastLife[p] - c.life;
				lastLife[p] = c.life;
				if (c.stateNo != lastState[p] || demo.roundState() != 2) {
					lastState[p] = c.stateNo;
					held[p] = 0;
					continue;
				}
				const bool idleState = c.stateNo == 0 || c.stateNo == 10 || c.stateNo == 11 || c.stateNo == 12 ||
					c.stateNo == 20;
				if (++held[p] == STUCK_TICKS && !idleState)
					stuck.push_back("p" + std::to_string(p + 1) + " stuck in state " + std::to_string(c.stateNo));
			}
		}
		const compat::Item *notes = nullptr;
		for (const compat::Item &item : compat::items()) {
			if (item.name == label)
				notes = &item;
		}
		const size_t missing = notes ? notes->missing.size() : 0;
		out += std::to_string(rounds) + " rounds, " + std::to_string(lifeLost) + " life lost; " +
			(stuck.empty() && missing == 0 ? std::string("ok") : std::to_string(stuck.size() + missing) + " notes") + "\n";
		for (const std::string &s : stuck)
			out += "    " + s + "\n";
		if (notes) {
			for (const std::string &m : notes->missing)
				out += "    " + m + "\n";
		}
		problems += (int)(stuck.size() + missing) > 0;
		platform::writeWholeFile(path.c_str(), out);
	}
	compat::end();
	out += "\n" + std::to_string(roster.size()) + " pairs, " + std::to_string(problems) + " with notes\n";
	logf("soak: %d pairs, %d with notes", (int)roster.size(), problems);
	return out;
}

int main(int argc, char **argv)
{
	GameConfig config;
	std::string configError;
	const bool configLoaded = config.load("data/mugen360.ini", configError);
	// Test-run options (see InputScript).
	std::string inputPath, input2Path, forcedMode, p1Arg, p2Arg, stageArg;
	bool checkRun = false, soakRun = false;
	// Test runs skip storyboards (logo, intro, arcade intros and endings) unless --storyboards.
	bool storyboards = false;
	uint32_t soakFrames = 3600;
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
		else if (arg == "--storyboards")
			storyboards = true;
		else if (arg == "--title")
			forceTitle = forceSelect = true;
		else if (arg == "--power" && i + 1 < argc)
			testPower = text::atoi(argv[++i]);
		else if (arg == "--p2life" && i + 1 < argc)
			testP2Life = text::atoi(argv[++i]);
		else if (arg == "--p1" && i + 1 < argc)
			p1Arg = argv[++i];
		else if (arg == "--p2" && i + 1 < argc)
			p2Arg = argv[++i];
		else if (arg == "--stage" && i + 1 < argc)
			stageArg = argv[++i];
		else if (arg == "--game" && i + 1 < argc)
			config.game = argv[++i];
		else if (arg == "--soak") {
			// --soak [folder]: every character against the next, computer against computer (13d).
			soakRun = true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
				config.game = argv[++i];
		} else if (arg == "--soakframes" && i + 1 < argc)
			soakFrames = (uint32_t)text::atoi(argv[++i]);
		else if (arg == "--check") {
			// --check [folder]: a game's compatibility report, without playing (13b).
			checkRun = true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
				config.game = argv[++i];
		}
	}
	const bool testRun = !inputPath.empty() || !shots.empty() || quitFrame != UINT32_MAX || checkRun || soakRun;
	if (!platform::init(testRun && !show))
		return 1;
	logf("mugen360: milestone 13d, soaks, build %s", buildVersion());
	// The options, saved in save/config.ini. Test runs keep Ikemen's defaults, so a player's saved options never
	// change a test's result.
	Settings settings;
	if (testRun)
		logf("settings: test run, defaults");
	else if (!settings.load(SETTINGS_PATH))
		logf("settings: no %s yet, defaults", SETTINGS_PATH);
	Music::setVolumes(settings.masterVolume, settings.bgmVolume);
	SoundBank::setVolume(settings.soundGain());
	if (!configLoaded)
		logf("mugen360: %s", configError.c_str());
	// A MUGEN game's own folder (13a): its system.def, fight.def and select.def replace mugen360's content. If it
	// can't be read, mugen360's own content plays instead.
	if (!config.game.empty()) {
		GameConfig fromGame = config;
		std::string gameError;
		if (fromGame.loadGame(config.game, gameError)) {
			config = fromGame;
		} else {
			logf("game: %s; playing mugen360's own content", gameError.c_str());
			setGameRoot("");
			config.game.clear();
		}
	}
	if (!p1Arg.empty())
		config.player1 = p1Arg;
	if (!p2Arg.empty())
		config.player2 = p2Arg;
	if (!stageArg.empty())
		config.stage = stageArg;
	logf("game: p1=%s p2=%s stage=%s", config.player1.c_str(), config.player2.c_str(), config.stage.c_str());
	// --soak: fight every pair, write soak_<game>.txt next to the log, and stop.
	if (soakRun) {
		std::string name = config.game.empty() ? std::string("mugen360") : config.game;
		name = name.substr(name.find_last_of('/') + 1);
		const std::string path = "soak_" + name + ".txt";
		const std::string report = runSoak(config, settings, soakFrames, path);
		logf("soak: report %s %s", platform::writeWholeFile(path.c_str(), report) ? "written to" : "couldn't be written to",
			path.c_str());
		platform::shutdown();
		return 0;
	}
	// --check: load everything the game names, write compat_<game>.txt next to the log, and stop.
	if (checkRun) {
		const std::string report = compat::checkGame(config);
		std::string name = config.game.empty() ? std::string("mugen360") : config.game;
		name = name.substr(name.find_last_of('/') + 1);
		const std::string path = "compat_" + name + ".txt";
		logf("check: report %s %s", platform::writeWholeFile(path.c_str(), report) ? "written to" : "couldn't be written to",
			path.c_str());
		platform::shutdown();
		return 0;
	}
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
		// The options screen from the title's OPTIONS (or straight away with --options, for tests).
		OptionsScreen options;
		const bool optionsLoaded = motifLoaded && options.load(motif, settings, error);
		if (testRun)
			options.setSavePath("tests/out/config.ini");
		if (motifLoaded && !optionsLoaded)
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
		// Team modes ask each side Single, Simul or Turns in the select screen's team menu; a test's --mode presets it.
		int32_t presetTeam = -1;
		if (forcedMode == "teamarcade" || forcedMode == "turnsarcade" || forcedMode == "teamversus" || forcedMode == "turns")
			presetTeam = MotifSelect::TEAM_TURNS;
		if (forcedMode == "simul")
			mode = SelectMode::TeamVersus, presetTeam = MotifSelect::TEAM_SIMUL;
		else if (forcedMode == "simularcade")
			mode = SelectMode::TeamArcade, modeCpu = true, presetTeam = MotifSelect::TEAM_SIMUL;
		else if (forcedMode == "watch")
			mode = SelectMode::Watch, modeCpu = true, presetTeam = MotifSelect::TEAM_SINGLE;
		// Simul when either side chose it (the other may be one fighter); otherwise Turns when either did; a Single
		// pair is a normal match.
		bool simul = false, turnsFight = false;
		const auto teamMode = [&] {
			return mode == SelectMode::TeamVersus || mode == SelectMode::TeamArcade || mode == SelectMode::Watch;
		};
		// A new character selection, on whichever select screen there is.
		auto resetSelect = [&](bool secondController) {
			if (motifSelectLoaded)
				motifSelect.reset(mode, secondController && !cpu, presetTeam);
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
		fightAssets.settings = &settings;
		auto startFight = [&](const GameConfig &picked) {
			GameConfig fight = picked;
			fight.mode = mode == SelectMode::QuickMatch ? "freebattle"
				: mode == SelectMode::Arcade            ? "arcade"
				: mode == SelectMode::Training          ? "training"
				: teamMode() && simul                    ? "simul"
				: teamMode() && turnsFight               ? "turns"
				: mode == SelectMode::TeamArcade         ? "arcade"
				: mode == SelectMode::Survival           ? "survival"
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
			if (stageLoaded && mode == SelectMode::Watch && !noAi)
				stage->setWatch(true);
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

		// Storyboards (13e): the screenpack's logo and intro before the title, a character's arcade intro before its
		// first match and ending after its last. They play one after another from storyQueue, then afterStory runs.
		std::unique_ptr<Storyboard> story;
		std::vector<std::string> storyQueue;
		std::function<void()> afterStory;
		Press storySkip, storyCancel;
		std::function<void()> nextStory = [&]() {
			while (!storyQueue.empty()) {
				const std::string path = storyQueue.front();
				storyQueue.erase(storyQueue.begin());
				story = std::make_unique<Storyboard>();
				std::string storyError;
				if (story->load(path, storyError)) {
					story->start();
					screen = gameScreen = Screen::Storyboard;
					return;
				}
				logf("storyboard: %s", storyError.c_str());
			}
			story.reset();
			if (afterStory) {
				const std::function<void()> then = afterStory;
				afterStory = nullptr;
				then();
			}
		};
		const bool showStoryboards = !testRun || storyboards;
		auto playStories = [&](std::vector<std::string> paths, std::function<void()> then) {
			paths.erase(std::remove(paths.begin(), paths.end(), std::string()), paths.end());
			storyQueue = showStoryboards ? paths : std::vector<std::string>();
			afterStory = std::move(then);
			nextStory();
		};
		// The screenpack's logo and intro, then the title (Ikemen's main.lua start: logo, intro, title).
		if (titleLoaded && showStoryboards)
			playStories({ storyboardFor(motif.path(), "files", "logo.storyboard"),
							storyboardFor(motif.path(), "files", "intro.storyboard") },
				[&]() {
					title.reset();
					screen = gameScreen = Screen::Title;
				});
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
			const bool selectHandlesBack = (screen == Screen::Select && motifSelectLoaded) || screen == Screen::Storyboard;
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
			else if (gameScreen == Screen::Options)
				wantedMusic = "option";
			else if (gameScreen == Screen::Select)
				wantedMusic = motifSelectLoaded && motifSelect.inVersus() ? "vs" : "select";
			else if (gameScreen == Screen::Stage && stage)
				wantedMusic = "match " + std::to_string(stage->matchCount());
			else if (gameScreen == Screen::Storyboard && story)
				wantedMusic = "story " + story->path() + " " + std::to_string(story->sceneIndex());
			if (wantedMusic != musicFor) {
				musicFor = wantedMusic;
				if (wantedMusic == "title" || wantedMusic == "select")
					music.request(motifLoaded ? motif.music(wantedMusic) : MusicTrack{}, true);
				else if (wantedMusic == "vs" || wantedMusic == "option")
					music.request(motif.music(wantedMusic), false);
				else if (text::startsWith(wantedMusic, "story ")) {
					// A scene with its own bgm starts it; one without leaves the music playing.
					if (story && story->music())
						music.request(*story->music(), true);
				}
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
			if (screen == Screen::Storyboard) {
				// Start skips the scene; an attack button or Back cancels the storyboard (Ikemen's key.skip / cancel).
				const bool skip = storySkip.update(pad.start || pad2.start);
				const bool cancel = storyCancel.update(pad.a || pad.b || pad.x || pad.y || pad.rb || pad.rt || pad2.a ||
										pad2.b || pad2.x || pad2.y || pad2.rb || pad2.rt) || back;
				if (story) {
					story->update(skip, cancel);
					story->draw();
				}
				finishFrame();
				if (!story || story->done()) {
					if (story && story->stopMusic())
						music.stop();
					musicFor.clear();
					nextStory();
				}
				continue;
			}
			if (screen == Screen::Title) {
				title.update(gameplayPad, pad2);
				title.draw();
				finishFrame();
				const TitleChoice choice = title.result();
				if (choice == TitleChoice::Exit)
					break;
				if (choice == TitleChoice::Options && optionsLoaded) {
					options.reset();
					screen = gameScreen = Screen::Options;
				} else if (choice != TitleChoice::None) {
					// Arcade and Quick Match force a CPU opponent. Training leaves player 2 as a stationary dummy.
					modeCpu = choice == TitleChoice::Arcade || choice == TitleChoice::TeamArcade ||
						choice == TitleChoice::QuickMatch || choice == TitleChoice::Survival || choice == TitleChoice::Watch;
					mode = choice == TitleChoice::QuickMatch ? SelectMode::QuickMatch
						: choice == TitleChoice::Arcade      ? SelectMode::Arcade
						: choice == TitleChoice::TeamArcade  ? SelectMode::TeamArcade
						: choice == TitleChoice::TeamVersus  ? SelectMode::TeamVersus
						: choice == TitleChoice::Training    ? SelectMode::Training
						: choice == TitleChoice::Survival    ? SelectMode::Survival
						: choice == TitleChoice::Watch       ? SelectMode::Watch
															 : SelectMode::Versus;
					resetSelect(secondController);
					screen = gameScreen = Screen::Select;
				}
				continue;
			}
			if (screen == Screen::Options) {
				options.update(gameplayPad, pad2);
				options.draw();
				finishFrame();
				if (options.done()) {
					title.reset();
					screen = gameScreen = Screen::Title;
				}
				continue;
			}
			if (screen == Screen::Select && motifSelectLoaded) {
				motifSelect.update(gameplayPad, pad2);
				motifSelect.draw();
				finishFrame();
				if (motifSelect.done()) {
					selectedFight = motifSelect.fight();
					simul = motifSelect.teamMode(0) == MotifSelect::TEAM_SIMUL ||
						motifSelect.teamMode(1) == MotifSelect::TEAM_SIMUL;
					turnsFight = !simul && (motifSelect.teamMode(0) == MotifSelect::TEAM_TURNS ||
											   motifSelect.teamMode(1) == MotifSelect::TEAM_TURNS);
					if (teamMode()) {
						turns.reset((int32_t)selectedFight.team1.size(), (int32_t)selectedFight.team2.size());
						turnsLife[0] = turnsLife[1] = -1;
						turnsPower[0] = turnsPower[1] = 0;
					}
					// Arcade: the player's character's intro before its first match (Ikemen's arcade intro.storyboard).
					const bool arcadeStart = (mode == SelectMode::Arcade || mode == SelectMode::TeamArcade) &&
						motifSelect.arcadeMatchNo() == 1;
					playStories({ arcadeStart ? storyboardFor(selectedFight.player1, "arcade", "intro.storyboard")
											  : std::string() },
						[&]() {
							startFight(selectedFight);
							screen = gameScreen = Screen::Stage;
						});
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
					// A Simul match is a normal best-of match; only Turns goes member by member.
					if (teamMode() && turnsFight) {
						turnsLife[0] = turnsLife[1] = -1;
						if (winner >= 0) {
							const int recovery = turnsRecovery(lifeMax[winner], timeLeft, settings.roundTicks(60));
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
						// A finished arcade route shows the character's ending first.
						const std::string ending = arcadeMode && winner == 0
							? storyboardFor(selectedFight.player1, "arcade", "ending.storyboard")
							: std::string();
						playStories({ ending }, [&, secondController]() {
							resetSelect(secondController);
							screen = gameScreen = Screen::Select;
						});
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
