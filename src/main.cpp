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
#ifndef M360_X360
#include <condition_variable>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#endif

#include "core/ai/ai.h"
#include "core/ai/smart_ai.h"
#include "core/formats/air.h"
#include "core/fight/camera.h"
#include "core/fight/character.h"
#include "core/render/debug_text.h"
#include "core/fight/effects.h"
#include "core/game/compat.h"
#include "core/screens/continue_screen.h"
#include "core/util/file.h"
#include "core/game/game_config.h"
#include "core/screens/fight_screen.h"
#include "core/screens/hud.h"
#include "core/screens/motif.h"
#include "core/screens/options_screen.h"
#include "core/screens/pause_menu.h"
#include "core/game/music.h"
#include "core/screens/select_screen.h"
#include "core/game/settings.h"
#include "core/screens/title_screen.h"
#include "core/util/log.h"
#include "core/fight/match.h"
#include "core/screens/select.h"
#include "core/util/text.h"
#include "core/formats/sff.h"
#include "core/ai/neural_policy.h"
#include "core/screens/neural_select.h"
#include "core/ai/rl_observation.h"
#include "core/formats/sound.h"
#include "core/render/sprite_renderer.h"
#include "core/fight/stage.h"
#include "core/screens/storyboard.h"
#include "core/util/version.h"
#include "platform/platform.h"

using namespace m360;

namespace {

// Frames played since the program started, the seed for real matches: when a player reaches a fight varies. Test and
// training runs leave it at 0 unless --charseed sets it.
uint32_t playedFrames = 0;

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

// Put a frame on screen before the external game's large SFFs and screenpack start loading. Drawing it once for
// each 360 front buffer also makes the message visible immediately through Xenia's VdSwap path.
void showLoadingScreen(const std::string &game)
{
	std::string name = game.empty() ? std::string("MUGEN360") : game;
	const size_t slash = name.find_last_of("/\\");
	if (slash != std::string::npos)
		name = name.substr(slash + 1);
	const char *heading = "MUGEN360";
	const char *message = "LOADING GAME...";
	for (int frame = 0; frame < 3; frame++) {
		if (!platform::beginFrame(0xFF080C18))
			return;
		platform::fillRect(0, 0, platform::SCREEN_WIDTH, 10, 0xFF3058A0);
		platform::fillRect(0, platform::SCREEN_HEIGHT - 10, platform::SCREEN_WIDTH, 10, 0xFFC03038);
		const float headingScale = 9.0f, nameScale = 4.0f, messageScale = 3.0f;
		drawText(heading, (platform::SCREEN_WIDTH - std::strlen(heading) * 6.0f * headingScale) / 2.0f, 220,
			headingScale, 0xFFFFFFFF);
		drawText(name.c_str(),
			(platform::SCREEN_WIDTH - name.size() * 6.0f * nameScale) / 2.0f, 340, nameScale, 0xFFFFD050);
		drawText(message, (platform::SCREEN_WIDTH - std::strlen(message) * 6.0f * messageScale) / 2.0f, 430,
			messageScale, 0xFFB8C8E8);
		platform::fillRect(380, 500, 520, 6, 0xFF283850);
		platform::fillRect(380, 500, 130, 6, 0xFF60A0E8);
		platform::endFrame();
		SpriteRenderer::newFrame();
	}
}

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
// many frames, keys being U D L R (directions) x y z a b c s (buttons), m (Back, Ikemen's menu key), or "-" for
// nothing; '#' starts a comment.
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
				case 'm': held |= Back; break;
				case 'i': held |= Lb; break;
				case 't': held |= Menu; break;
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
		pad.back = (held & Back) != 0;
		pad.lb = (held & Lb) != 0;
		pad.menu = (held & Menu) != 0;
		return pad;
	}

private:
	enum : uint16_t {
		Up = 1 << 0, Down = 1 << 1, Left = 1 << 2, Right = 1 << 3, X = 1 << 4, Y = 1 << 5,
		Z = 1 << 6, A = 1 << 7, B = 1 << 8, C = 1 << 9, Start = 1 << 10, Back = 1 << 11,
		// i: LB (the info and hitbox view), t: the debug screen switch (Tab).
		Lb = 1 << 12, Menu = 1 << 13
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

	bool load(const GameConfig &config, std::string &error, bool simulationOnly = false)
	{
		if (loaded)
			return true;
		if (simulationOnly) {
			fightError = "simulation only";
			if (!effects.load(FIGHTFX_SFF, FIGHTFX_AIR, error, true))
				return false;
			loaded = true;
			return true;
		}
		// The screenpack's localcoord, for a fight.def that gives none.
		int32_t motifW = 320, motifH = 240;
		std::vector<uint8_t> motifBytes;
		if (!config.motif.empty() && readFile(config.motif, motifBytes)) {
			const std::vector<IniSection> motifIni = parseIni(std::string(motifBytes.begin(), motifBytes.end()));
			if (const IniSection *info = findSection(motifIni, "info"))
				info->ints("localcoord", { &motifW, &motifH });
		}
		fightScreenLoaded = !config.fight.empty() && fightScreen.load(config.fight, fightError, motifW, motifH);
		if (config.fight.empty())
			fightError = "none";
		// The fight screen's own fightfx and common.snd, else the shared ones (found in the game's folder first).
		const bool own = fightScreenLoaded && !fightScreen.fightfxSff().empty() && !fightScreen.fightfxAir().empty();
		if (!effects.load(own ? fightScreen.fightfxSff() : FIGHTFX_SFF, own ? fightScreen.fightfxAir() : FIGHTFX_AIR,
				error) ||
			!commonSound.load(fightScreenLoaded && !fightScreen.commonSnd().empty() ? fightScreen.commonSnd() : COMMON_SND,
				error))
			return false;
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

	bool load(const GameConfig &config, std::string &error, bool trace, bool quickStart, bool cpu,
		bool simulationOnly = false)
	{
		if (!stage_.load(config.stage, error, simulationOnly))
			return false;
		if (!assets_.load(config, error, simulationOnly))
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
		smartAi_ = settings.aiStyle != 0;
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
			if (const Character *shared = sharedCharacters_ ? sharedCharacters_(characterDefs[i]) : nullptr)
				players_[i].loadFrom(*shared, palettes[i]);
			else if (!players_[i].load(characterDefs[i], palettes[i], error, simulationOnly))
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
		for (int i = 0; i < 4; i++)
			smart_[i].seed(0x2545F491u * (uint32_t)(i + 1));
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
		// Each real match rolls differently (Ikemen seeds from the clock); test and training runs stay repeatable.
		if (playedFrames != 0) {
			const uint32_t seed = (playedFrames ^ matchCount_ * 0x9E3779B9u) * 2654435761u;
			seedAi(seed);
			for (int i = 0; i < count_; i++)
				players_[i].seedRandom(seed ^ (0x85EBCA6Bu * (uint32_t)(i + 1)));
		}
		neuralMemory_.clear();
		neuralFrame_ = neuralDirection_ = neuralButton_ = 0;
		match_.newMatch();
		fightScreen_.newMatch();
		startRound(true);
	}
	uint32_t matchCount() const { return matchCount_; }
	const MusicTrack &music() const { return stage_.music; }
	// RL training's handicap: a side's life bar at a fraction of its usual size, every round. Set it on a freshly
	// loaded fight before the match starts.
	void setLifeHandicap(int side, float fraction)
	{
		for (int i = 0; i < count_; i++) {
			if (side_[i] == side)
				players_[i].setLifeScale(players_[i].lifeScale() * fraction);
		}
	}
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
		// Plain comparisons: std::clamp here built an instruction Xenia skips (mcrf, see xenia_scan).
		for (int i = 0; i < count_; i++) {
			if (side_[i] != side)
				continue;
			int32_t life = value;
			if (life > players_[i].lifeMax())
				life = players_[i].lifeMax();
			if (life < 1)
				life = 1;
			players_[i].life = life;
		}
	}
	void setFighterStats(int side, int32_t life, int32_t power)
	{
		if (side < 0 || side >= 2)
			return;
		players_[side].life = std::clamp(life, 1, players_[side].lifeMax());
		players_[side].power = std::clamp(power, 0, players_[side].powerMax());
	}
	int32_t fighterLife(int side) const { return players_[side].life; }
	// The quote the victory screen shows for a side's leader.
	std::string victoryQuote(int side) { return players_[side].victoryQuote(); }
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
		if (neural_) {
			// VS NEURAL CAP: the policy plays player 1 as in training (a decision held for RL_FRAMES frames, whatever
			// the game speed), and the person's controller plays player 2.
			// Training skipped the intros and round calls (every round started at once), so the policy waits them out:
			// no decisions and no memory until the fight starts, then its first decision on the fight's first frame.
			if (match_.roundState() < 2) {
				neuralPad_ = platform::Pad{};
				neuralPad_.connected = true;
				neuralFrame_ = 0;
			} else if (neuralFrame_ % RL_FRAMES == 0) {
				if (neuralMemory_.empty())
					neuralMemory_ = neural_->freshMemory();
				const std::vector<float> seen = rlObservation(players_[0], players_[1], match_.roundState(), match_.timeLeft(),
					neuralDirection_, neuralButton_);
				neural_->decide(seen, neuralSelf_, neuralOpponent_, neuralMemory_, neuralRandom_, neuralDirection_,
					neuralButton_);
				neuralPad_ = rlPad(neuralDirection_, neuralButton_, players_[0].facing);
			}
			if (match_.roundState() >= 2)
				neuralFrame_++;
			while (speed_ >= 1) {
				speed_ -= 1;
				tick(neuralPad_, pad1);
			}
			return;
		}
		while (speed_ >= 1) {
			speed_ -= 1;
			tick(pad1, pad2);
		}
	}

	// One game tick: the round, then each phase for both players before the next (Ikemen's charList order).
	void tick(const platform::Pad &pad1, const platform::Pad &pad2)
	{
		world_.globalSpecial = 0;
		effects_.tickEnvColor();
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
		const bool cpu = neural_ ? neuralCpu_ : forceCpu_ || (!training_ && !pad2.connected);
		players_[1].aiLevel = cpu ? aiLevel_ : 0;
		updateAi(1, cpu && match_.roundState() == 2);
		// Simul partners are the computer's (Ikemen: a team member without a controller of its own).
		for (int i = 2; i < count_; i++) {
			players_[i].aiLevel = aiLevel_;
			updateAi(i, match_.roundState() == 2);
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
			updateAi(0, match_.roundState() == 2);
		if (run[0]) {
			InputState input1;
			// The policy's buttons as in training, with the default button config.
			static const Settings defaults;
			(neural_ ? defaults : *assets_.settings).toInput(0, pad1, input1);
			players_[0].turnAndInput(watch_ ? aiState(0) : input1);
			recordCommands(players_[0]);
		}
		if (run[1]) {
			InputState input2;
			// Against the neural CPU the person plays player 2 with player 1's controller and button config.
			assets_.settings->toInput(neural_ ? 0 : 1, pad2, input2);
			players_[1].turnAndInput(cpu ? aiState(1) : input2);
		}
		for (int i = 2; i < count_; i++)
			if (run[i]) players_[i].turnAndInput(aiState(i));
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
			if (c.moveCameraX()) {
				const float velocity = run[i] && !c.hitPause() ? c.vx * c.localScale * (float)c.facing : 0.0f;
				camera_.trackX(c.worldX(), c.worldX(), velocity);
			}
			if (c.moveCameraY() && std::isfinite(c.worldY()))
				camera_.trackY(c.worldY());
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

	// bars false leaves out the fight screen (lifebars, timer...): the pause menu's hidebars.
	void draw(bool bars = true)
	{
		const View view = fightView(effects_.shakeX(), effects_.shakeY());
		const PalFX *stagePalFX = effects_.stagePalFX();
		const float stageBrightness = effects_.superBrightness();
		// Ikemen's order: the fight screen's layer -1 and 0 behind the characters, 1 and 2 in front.
		// nobg: black behind the fighters instead of the stage; nofg: no stage in front (Ikemen's GSF_nobg / nofg).
		const bool noBg = (world_.globalSpecial & Special::NoBg) != 0, noFg = (world_.globalSpecial & Special::NoFg) != 0;
		// EnvColor replaces everything behind the characters, and them too unless it is under them (Ikemen's System.draw).
		const bool envColor = effects_.envColorOn(), showFighters = !envColor || effects_.envColorUnder();
		if (envColor) {
			effects_.drawEnvColor();
		} else {
			if (noBg)
				platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, 0xFF000000);
			if (stage_.ikemen && !noBg)
				stage_.draw(-1, view, stagePalFX, stageBrightness);
			// Effects with layerno -1, under the stage's layer 0 (Ikemen's spriteList.draw(-1)).
			effects_.draw(view, (float)stage_.zoffset * stage_.localScale, -2);
			if (!noBg)
				stage_.draw(0, view, stagePalFX, stageBrightness);
			if (bars)
				fightScreen_.draw(-1, match_, players_[0], players_[1]);
			{
				FightEffects::GroundShadow ground;
				ground.intensity = stage_.shadow.intensity;
				ground.yscale = stage_.shadow.yscale;
				ground.colour = stage_.shadow.colour;
				ground.offsetX = stage_.shadow.offsetX * stage_.localScale;
				ground.offsetY = stage_.shadow.offsetY * stage_.localScale;
				effects_.drawShadows(view, (float)stage_.zoffset * stage_.localScale, ground);
			}
			if (bars)
				fightScreen_.draw(0, match_, players_[0], players_[1]);
		}
		// Player 1 in front.
		if (showFighters) {
			drawFighters(view);
		}
		if (!noFg && !envColor)
			stage_.draw(1, view, stagePalFX, stageBrightness);
		if (bars)
			fightScreen_.draw(1, match_, players_[0], players_[1]);
		effects_.draw(view, (float)stage_.zoffset * stage_.localScale, 1);
		// A pillarboxed stage: black at the sides, over whatever of the stage and the fighters reached past it.
		if (view.originX > 0) {
			platform::fillRect(0, 0, view.originX, platform::SCREEN_HEIGHT, 0xFF000000);
			platform::fillRect(view.originX + view.width, 0, view.originX, platform::SCREEN_HEIGHT, 0xFF000000);
		}
		if (bars)
			fightScreen_.draw(2, match_, players_[0], players_[1]);
		if (fightScreen_.loaded())
			fightScreen_.drawFade();
		else if (bars)
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
	// Every fighter and helper by sprpriority, highest in front; equals keep the list's order, first in front
	// (Ikemen's DrawList.draw).
	void drawFighters(const View &view)
	{
		fighterDraws_.clear();
		for (int i = 0; i < count_; i++)
			fighterDraws_.push_back({ &players_[i], i });
		for (int i = 0; i < count_; i++) {
			helperDraws_.clear();
			players_[i].collectHelpers(helperDraws_);
			for (Character *helper : helperDraws_)
				fighterDraws_.push_back({ helper, i });
		}
		std::stable_sort(fighterDraws_.begin(), fighterDraws_.end(), [](const FighterDraw &a, const FighterDraw &b) {
			return a.fighter->sprPriority > b.fighter->sprPriority;
		});
		const float groundY = (float)stage_.zoffset * stage_.localScale;
		// An Explod draws in front of a fighter with the same sprpriority: Galactus's head rises behind his floor.
		int32_t effectsDrawn = INT32_MIN;
		for (int i = (int)fighterDraws_.size() - 1; i >= 0; i--) {
			const FighterDraw &entry = fighterDraws_[i];
			if (entry.fighter->sprPriority > effectsDrawn) {
				effects_.draw(view, groundY, 0, effectsDrawn, entry.fighter->sprPriority);
				effectsDrawn = entry.fighter->sprPriority;
			}
			entry.fighter->draw(view, groundY, groundFor(players_[entry.player]));
		}
		effects_.draw(view, groundY, 0, effectsDrawn);
	}

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
	// The fight's draw order, kept between frames.
	struct FighterDraw {
		Character *fighter;
		int player;
	};
	std::vector<FighterDraw> fighterDraws_;
	std::vector<Character *> helperDraws_;
	std::function<const Character *(const std::string &)> sharedCharacters_;
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
	// The computer players, at the options' Difficulty: SmartAi, or MUGEN's button jamming (AI Style Classic).
	int aiLevel_ = 5;
	int32_t ticksPerSecond_ = 60;
	AiInput ai_[4];
	SmartAi smart_[4];
	bool smartAi_ = true;
	// One tick of fighter i's computer player; active is false outside the fight, when it lets go of everything.
	void updateAi(int i, bool active)
	{
		if (smartAi_ && players_[i].p2()) {
			smart_[i].update(players_[i], *players_[i].p2(), aiLevel_, active);
			players_[i].aiCheat = smart_[i].cheatAny() ? Character::AiCheat::Any
				: smart_[i].roundStart()               ? Character::AiCheat::Switches
													   : Character::AiCheat::None;
		} else {
			ai_[i].update((float)aiLevel_, active);
			players_[i].aiCheat = Character::AiCheat::Any;
		}
	}
	InputState aiState(int i) const { return smartAi_ && players_[i].p2() ? smart_[i].state() : ai_[i].state(); }
	bool forceCpu_ = false;
	bool training_ = false;
	// Watch mode: the computer plays player 1 as well (Ikemen's cpuSide[1]).
	bool watch_ = false;
	// VS NEURAL CAP: a trained policy plays player 1 (setNeural).
	const NeuralPolicy *neural_ = nullptr;
	int neuralSelf_ = 0, neuralOpponent_ = 0, neuralFrame_ = 0, neuralDirection_ = 0, neuralButton_ = 0;
	uint32_t neuralRandom_ = 1;
	// Tests (--cpu): the stock AI plays player 2 instead of the person, as in training.
	bool neuralCpu_ = false;
	std::vector<float> neuralMemory_;
	platform::Pad neuralPad_;
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
	// The policy plays player 1 from now on, the person player 2. self and opponent are the characters' ids in its
	// roster; seed varies its sampled play from match to match.
	void setNeural(const NeuralPolicy *policy, int self, int opponent, uint32_t seed, bool cpuOpponent)
	{
		neural_ = policy;
		neuralCpu_ = cpuOpponent;
		neuralSelf_ = self;
		neuralOpponent_ = opponent;
		neuralRandom_ = seed ? seed : 1;
		neuralMemory_.clear();
		neuralFrame_ = neuralDirection_ = neuralButton_ = 0;
	}
	void setWatch(bool watch)
	{
		watch_ = watch;
		ai_[0].seed(0x2545F491u);
		smart_[0].seed(0x9E3779B9u);
	}
	bool finished() const { return finished_; }
	// Each side's matches won in a row, to carry into the next fight.
	int winCount(int side) const { return fightScreen_.winCount(side); }
	int winner() const { return match_.winner(); }
	bool matchOver() const { return match_.matchOver(); }
	// A training match ends on a drawn round too (a double KO, equal life at time over): the game would play
	// another round with life refilled, which a policy could stall its way into for free.
	bool trainingOver() const { return match_.matchOver() || match_.draws() > 0; }
	int32_t matchTimeLeft() const { return match_.timeLeft(); }
	void seedAi(uint32_t seed)
	{
		for (int i = 0; i < count_; i++) {
			ai_[i].update((float)aiLevel_, false);
			smart_[i].update(players_[i], players_[i == 0 ? 1 : 0], aiLevel_, false);
			ai_[i].seed(seed ^ (0x9E3779B9u * (uint32_t)(i + 1)));
			smart_[i].seed(seed ^ (0x2545F491u * (uint32_t)(i + 1)));
		}
	}
	// A training client reuses the loaded stage and characters for thousands of episodes.
	void resetTraining(uint32_t seed, bool cpuOpponent)
	{
		forceCpu_ = cpuOpponent;
		finished_ = false;
		speed_ = 0;
		lastActive_.clear();
		history_.clear();
		seedAi(seed);
		// The characters' own random numbers too, or a reused fight carries them over from its last match and the same
		// seed plays out differently than in a fresh process.
		for (int i = 0; i < count_; i++)
			players_[i].seedRandom(seed ^ (0x85EBCA6Bu * (uint32_t)(i + 1)));
		newMatch();
	}
	// An RL pool loads each character once; a fight given this takes its characters from there instead of the files.
	void useSharedCharacters(std::function<const Character *(const std::string &)> find) { sharedCharacters_ = find; }
	// The debug text and hitboxes (Q / LB); off, as Ikemen shows none.
	bool showInfo_ = false;
	Press lbPress_, ltPress_;
	std::vector<std::string> lastActive_;
	std::vector<std::pair<std::string, int>> history_;
};

#ifndef M360_X360
// The PC-only RL bridge deliberately exposes controller inputs rather than state changes. A learned policy must play
// through the same CMD parser as a person, and can therefore train on any mounted MUGEN game without character code.
// The observation and controller code is shared with the neural CPU (core/ai/rl_observation).
// Player 2's view is mirrored, so it looks like player 1's and one policy plays both sides alike.
std::vector<float> rlObservation(const StageDemo &demo, int side, int previousDirection, int previousButton)
{
	return m360::rlObservation(demo.fighter(side), demo.fighter(1 - side), demo.roundState(), demo.matchTimeLeft(),
		previousDirection, previousButton, side == 1);
}

void writeRlState(const StageDemo &demo, float reward, bool done, const int previousDirection[2], const int previousButton[2])
{
	const std::vector<float> observation1 = rlObservation(demo, 0, previousDirection[0], previousButton[0]);
	const std::vector<float> observation2 = rlObservation(demo, 1, previousDirection[1], previousButton[1]);
	std::cout << "RL_OBS " << std::setprecision(7) << reward << ' ' << (done ? 1 : 0) << ' ' << demo.winner();
	for (float value : observation1)
		std::cout << ' ' << value;
	for (float value : observation2)
		std::cout << ' ' << value;
	std::cout << std::endl;
}

// recordDir: when set, the fight loads its graphics and saves one .bmp there after each reset and step, so a clip
// shows exactly what the policy played (a scripted replay can drift from the training run).
// show: the fight plays in the window with its graphics and sound, every tick drawn at 60 a second, so a policy
// can be watched live (--show with --rl-server; closing the window ends the server).
int runRlServer(const GameConfig &config, Settings &settings, const std::string &recordDir, bool show, bool trace)
{
	// Each match plays in a fight built afresh, as in the RL pool: a reused one carries state from the last match
	// (animations, game time, input buffers), so a seed wouldn't replay the same match (BUGFIX_LIST 9b, 9).
	std::unique_ptr<FightAssets> assets;
	std::unique_ptr<StageDemo> demoHolder;
	std::string error;
	const auto build = [&]() {
		demoHolder.reset();
		assets = std::make_unique<FightAssets>();
		assets->settings = &settings;
		demoHolder = std::make_unique<StageDemo>(*assets);
		if (!demoHolder->load(config, error, trace, true, true, recordDir.empty() && !show))
			return false;
		demoHolder->returnToSelectAfterMatch();
		return true;
	};
	uint32_t recorded = 0;
	const auto record = [&]() {
		if (recordDir.empty() || !platform::beginFrame(BACKGROUND))
			return;
		demoHolder->draw();
		char path[32];
		std::snprintf(path, sizeof path, "/%06u.bmp", recorded++);
		platform::requestScreenshot((recordDir + path).c_str());
		platform::endFrame();
		SpriteRenderer::newFrame();
	};
	if (!build()) {
		std::cout << "RL_ERROR " << error << std::endl;
		return 1;
	}
	bool fresh = true;
	int previousDirection[2] = { 0, 0 }, previousButton[2] = { 0, 0 };
	std::cout << "RL_READY " << rlObservation(*demoHolder, 0, 0, 0).size() << ' ' << RL_DIRECTIONS << ' ' << RL_BUTTONS
		<< std::endl;
	std::string line;
	while (std::getline(std::cin, line)) {
		std::istringstream input(line);
		std::string command;
		input >> command;
		if (command == "quit")
			break;
		if (command == "reset") {
			uint32_t seed = 1;
			std::string opponent = "smart";
			// Optionally player 2's life bar as a fraction (a training curriculum's handicap, for clips of it).
			float opponentLife = 1;
			input >> seed >> opponent >> opponentLife;
			if (!fresh && !build()) {
				std::cout << "RL_ERROR " << error << std::endl;
				return 1;
			}
			fresh = false;
			if (opponentLife > 0 && opponentLife < 1)
				demoHolder->setLifeHandicap(1, std::max(opponentLife, 0.01f));
			demoHolder->resetTraining(seed, opponent != "external");
			previousDirection[0] = previousDirection[1] = previousButton[0] = previousButton[1] = 0;
			record();
			writeRlState(*demoHolder, 0, false, previousDirection, previousButton);
			continue;
		}
		if (command != "step") {
			std::cout << "RL_ERROR expected reset, step, or quit" << std::endl;
			continue;
		}
		int direction1 = 0, button1 = 0, direction2 = 0, button2 = 0, frames = 3;
		input >> direction1 >> button1 >> direction2 >> button2 >> frames;
		direction1 = std::clamp(direction1, 0, RL_DIRECTIONS - 1);
		button1 = std::clamp(button1, 0, RL_BUTTONS - 1);
		direction2 = std::clamp(direction2, 0, RL_DIRECTIONS - 1);
		button2 = std::clamp(button2, 0, RL_BUTTONS - 1);
		frames = std::clamp(frames, 1, 30);
		float reward = 0;
		for (int frame = 0; frame < frames && !demoHolder->trainingOver(); frame++) {
			const int32_t oldSelf = demoHolder->fighter(0).life, oldOther = demoHolder->fighter(1).life;
			Buttons buttons;
			const platform::Pad p1 = rlPad(direction1, button1, demoHolder->fighter(0).facing);
			const platform::Pad p2 = rlPad(direction2, button2, demoHolder->fighter(1).facing);
			demoHolder->update(buttons, p1, p2);
			if (show) {
				if (!platform::beginFrame(BACKGROUND))
					return 0;
				demoHolder->draw();
				platform::endFrame();
				SpriteRenderer::newFrame();
				// One tick per 60th of a second, however fast the drawing went.
				static auto nextFrame = std::chrono::steady_clock::now();
				nextFrame += std::chrono::microseconds(16667);
				const auto now = std::chrono::steady_clock::now();
				if (nextFrame > now)
					std::this_thread::sleep_until(nextFrame);
				else
					nextFrame = now;
			}
			reward += (float)std::max(0, oldOther - demoHolder->fighter(1).life) /
				(float)std::max(1, demoHolder->fighter(1).lifeMax());
			reward -= (float)std::max(0, oldSelf - demoHolder->fighter(0).life) /
				(float)std::max(1, demoHolder->fighter(0).lifeMax());
		}
		const bool done = demoHolder->trainingOver();
		if (done)
			reward += demoHolder->winner() == 0 ? 1.0f : demoHolder->winner() == 1 ? -1.0f : 0.0f;
		previousDirection[0] = direction1;
		previousDirection[1] = direction2;
		previousButton[0] = button1;
		previousButton[1] = button2;
		record();
		writeRlState(*demoHolder, reward, done, previousDirection, previousButton);
	}
	return 0;
}

// --rl-pool N: N headless fights of one matchup in one process, for RL training at scale. Each character loads once
// and every fight shares it (Character::loadFrom); each fight has its own FightAssets, since effects and the fight
// screen change as it plays. Steps run on a pool of threads, and the protocol is binary on the original stdout (log
// lines go to stderr instead), all fights at once:
//   start:  "RLPL", int32 fights, int32 observation size, int32 directions, int32 buttons
//   'R' uint32 seed, uint8 external   reset every fight (fight i uses seed + i); answers the observations
//   'S' uint8 frames, then per fight uint8 direction1, button1, direction2, button2
//       answers float32 reward[N], uint8 done[N], int8 winner[N], then the observations
//   'L' float32 fraction              player 2's life bar from each fight's next match on (no answer)
//   'Q'                               quit
// Observations are float32 [N][2][size]: each fight's player 1 view, then player 2's (mirrored). A fight whose match
// ended (won, or a drawn round: winner -1) reports it (done, winner, final reward) and is reset at once, so its
// observation is the next match's first one.
namespace {

struct RlFight {
	std::unique_ptr<FightAssets> assets;
	std::unique_ptr<StageDemo> demo;
	int previousDirection[2] = { 0, 0 }, previousButton[2] = { 0, 0 };
	uint32_t seed = 0, episodes = 0;
	bool external = false;
	// Player 2's life bar as a fraction of its usual size, from the next match on (a training curriculum's handicap).
	float opponentLife = 1;

	// A new fight from the shared characters: a match left behind more than its random numbers (animations, the
	// game time, input buffers), so each match starts from a fight built afresh. That makes a seed play out the same
	// in any fight, as in a fresh process, which replays rely on.
	bool build(const GameConfig &config, Settings &settings, const std::function<const Character *(const std::string &)> &find,
		std::string &error)
	{
		demo.reset();
		assets = std::make_unique<FightAssets>();
		assets->settings = &settings;
		demo = std::make_unique<StageDemo>(*assets);
		demo->useSharedCharacters(find);
		if (!demo->load(config, error, false, true, true, true))
			return false;
		demo->returnToSelectAfterMatch();
		return true;
	}

	void reset(uint32_t count, uint32_t index, const GameConfig &config, Settings &settings,
		const std::function<const Character *(const std::string &)> &find)
	{
		std::string error;
		{
			// Loading reads files through the platform's file cache, which isn't thread-safe: one build at a time.
			static std::mutex building;
			std::lock_guard<std::mutex> lock(building);
			if (!build(config, settings, find, error))
				logf("rl pool: %s", error.c_str());
		}
		if (opponentLife != 1)
			demo->setLifeHandicap(1, opponentLife);
		demo->resetTraining(seed + index + count * episodes, !external);
		episodes++;
		previousDirection[0] = previousDirection[1] = previousButton[0] = previousButton[1] = 0;
	}

	void observe(float *out, size_t size) const
	{
		for (int side = 0; side < 2; side++) {
			const std::vector<float> o = rlObservation(*demo, side, previousDirection[side], previousButton[side]);
			std::memcpy(out + side * size, o.data(), std::min(o.size(), size) * sizeof(float));
		}
	}

	// One decision: the same frames and reward as runRlServer's step.
	void step(const uint8_t action[4], int frames, float &reward, uint8_t &done, int8_t &winner)
	{
		const int direction1 = std::min<int>(action[0], RL_DIRECTIONS - 1);
		const int button1 = std::min<int>(action[1], RL_BUTTONS - 1);
		const int direction2 = std::min<int>(action[2], RL_DIRECTIONS - 1);
		const int button2 = std::min<int>(action[3], RL_BUTTONS - 1);
		reward = 0;
		for (int frame = 0; frame < frames && !demo->trainingOver(); frame++) {
			const int32_t oldSelf = demo->fighter(0).life, oldOther = demo->fighter(1).life;
			Buttons buttons;
			const platform::Pad p1 = rlPad(direction1, button1, demo->fighter(0).facing);
			const platform::Pad p2 = rlPad(direction2, button2, demo->fighter(1).facing);
			demo->update(buttons, p1, p2);
			reward += (float)std::max(0, oldOther - demo->fighter(1).life) / (float)std::max(1, demo->fighter(1).lifeMax());
			reward -= (float)std::max(0, oldSelf - demo->fighter(0).life) / (float)std::max(1, demo->fighter(0).lifeMax());
		}
		done = demo->trainingOver() ? 1 : 0;
		winner = (int8_t)(done ? demo->winner() : -1);
		if (done)
			reward += winner == 0 ? 1.0f : winner == 1 ? -1.0f : 0.0f;
		previousDirection[0] = direction1;
		previousDirection[1] = direction2;
		previousButton[0] = button1;
		previousButton[1] = button2;
	}
};

// Runs job(worker) on every worker thread and waits for all of them; the threads stay up between calls.
class RlWorkers {
public:
	explicit RlWorkers(int count) : count_(count)
	{
		for (int t = 0; t < count; t++)
			threads_.emplace_back([this, t]() { loop(t); });
	}
	~RlWorkers()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stop_ = true;
		}
		start_.notify_all();
		for (std::thread &thread : threads_)
			thread.join();
	}
	int count() const { return count_; }
	void run(const std::function<void(int)> &job)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		job_ = &job;
		remaining_ = count_;
		generation_++;
		start_.notify_all();
		finished_.wait(lock, [this]() { return remaining_ == 0; });
		job_ = nullptr;
	}

private:
	void loop(int t)
	{
		uint64_t seen = 0;
		for (;;) {
			const std::function<void(int)> *job;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				start_.wait(lock, [&]() { return stop_ || generation_ != seen; });
				if (stop_)
					return;
				seen = generation_;
				job = job_;
			}
			(*job)(t);
			std::lock_guard<std::mutex> lock(mutex_);
			if (--remaining_ == 0)
				finished_.notify_one();
		}
	}
	int count_;
	std::vector<std::thread> threads_;
	std::mutex mutex_;
	std::condition_variable start_, finished_;
	const std::function<void(int)> *job_ = nullptr;
	uint64_t generation_ = 0;
	int remaining_ = 0;
	bool stop_ = false;
};

bool readExact(void *data, size_t size)
{
	return std::fread(data, 1, size, stdin) == size;
}

} // namespace

int runRlPool(const GameConfig &config, Settings &settings, int count, int threads)
{
	// The protocol keeps the real stdout; everything printf'd (log lines) goes to stderr from here on.
	std::fflush(stdout);
	const int protocolFd = _dup(_fileno(stdout));
	_dup2(_fileno(stderr), _fileno(stdout));
	_setmode(protocolFd, _O_BINARY);
	_setmode(_fileno(stdin), _O_BINARY);
	std::FILE *out = _fdopen(protocolFd, "wb");
	if (!out)
		return 1;
	std::setvbuf(out, nullptr, _IOFBF, 1 << 20);

	// Each character's files load once, the first time a fight asks for them; fights share them after that. Once the
	// fights are built the map only gets read, so the worker threads can rebuild fights from it.
	std::map<std::string, std::unique_ptr<Character>> loaded;
	bool loading = true;
	std::function<const Character *(const std::string &)> find = [&loaded, &loading](const std::string &def) -> const Character * {
		auto it = loaded.find(def);
		if (it == loaded.end()) {
			if (!loading)
				return nullptr;
			auto character = std::make_unique<Character>();
			std::string error;
			if (!character->load(def, 1, error, true)) {
				logf("rl pool: %s", error.c_str());
				return nullptr;
			}
			it = loaded.emplace(def, std::move(character)).first;
		}
		return it->second.get();
	};
	std::vector<std::unique_ptr<RlFight>> fights;
	for (int i = 0; i < count; i++) {
		auto fight = std::make_unique<RlFight>();
		std::string error;
		if (!fight->build(config, settings, find, error)) {
			logf("rl pool: %s", error.c_str());
			return 1;
		}
		fights.push_back(std::move(fight));
	}
	loading = false;
	const size_t size = rlObservation(*fights[0]->demo, 0, 0, 0).size();
	const int32_t header[4] = { count, (int32_t)size, RL_DIRECTIONS, RL_BUTTONS };
	std::fwrite("RLPL", 1, 4, out);
	std::fwrite(header, sizeof header, 1, out);
	std::fflush(out);

	RlWorkers workers(std::max(1, std::min(threads, count)));
	std::vector<float> observations((size_t)count * 2 * size), rewards((size_t)count);
	std::vector<uint8_t> actions((size_t)count * 4), done((size_t)count);
	std::vector<int8_t> winners((size_t)count);
	auto writeObservations = [&]() {
		std::fwrite(observations.data(), sizeof(float), observations.size(), out);
		std::fflush(out);
	};
	for (;;) {
		char command = 0;
		if (!readExact(&command, 1) || command == 'Q')
			break;
		if (command == 'R') {
			uint32_t seed = 1;
			uint8_t external = 0;
			if (!readExact(&seed, 4) || !readExact(&external, 1))
				break;
			workers.run([&](int t) {
				for (int i = t; i < count; i += workers.count()) {
					RlFight &fight = *fights[(size_t)i];
					fight.seed = seed;
					fight.episodes = 0;
					fight.external = external != 0;
					fight.reset((uint32_t)count, (uint32_t)i, config, settings, find);
					fight.observe(&observations[(size_t)i * 2 * size], size);
				}
			});
			writeObservations();
		} else if (command == 'S') {
			uint8_t frames = 3;
			if (!readExact(&frames, 1) || !readExact(actions.data(), actions.size()))
				break;
			const int ticks = std::clamp<int>(frames, 1, 30);
			workers.run([&](int t) {
				for (int i = t; i < count; i += workers.count()) {
					RlFight &fight = *fights[(size_t)i];
					fight.step(&actions[(size_t)i * 4], ticks, rewards[(size_t)i], done[(size_t)i], winners[(size_t)i]);
					if (done[(size_t)i])
						fight.reset((uint32_t)count, (uint32_t)i, config, settings, find);
					fight.observe(&observations[(size_t)i * 2 * size], size);
				}
			});
			std::fwrite(rewards.data(), sizeof(float), rewards.size(), out);
			std::fwrite(done.data(), 1, done.size(), out);
			std::fwrite(winners.data(), 1, winners.size(), out);
			writeObservations();
		} else if (command == 'L') {
			float fraction = 1;
			if (!readExact(&fraction, 4))
				break;
			fraction = std::clamp(fraction, 0.01f, 1.0f);
			for (auto &fight : fights)
				fight->opponentLife = fraction;
		} else {
			logf("rl pool: unknown command %d", (int)command);
			break;
		}
	}
	return 0;
}
#endif

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

// The debug viewers and the character they show.
struct Viewers {
	Character kfm;
	std::string error;
	bool loaded;
	SpriteRenderer renderer;
	AnimationViewer animations;
	SpriteViewer sprites;

	explicit Viewers(const std::string &def)
		: loaded(kfm.load(def, 1, error)), renderer(kfm.sff()), animations(kfm.anims(), renderer),
		  sprites(kfm.sff(), renderer)
	{
		if (!loaded)
			logf("mugen360: %s", error.c_str());
	}
};

// What the 360 has free and what the textures hold, in the log: a big game ran a real console out of memory (13).
void logMemory(const char *where)
{
	size_t free = 0;
	if (platform::freeMemory(free))
		logf("memory: %s: %zu MB free, %zu MB of textures", where, free >> 20, SpriteRenderer::totalTextureBytes() >> 20);
}

enum class Screen { Title, Options, Select, NeuralSelect, Stage, Animations, Sprites, Storyboard, Victory, Continue };

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
std::string runSoak(const GameConfig &config, const Settings &settings, uint32_t frames, const std::string &path,
	size_t firstPair)
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
	for (size_t i = firstPair; i < roster.size(); i++) {
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
				SpriteRenderer::newFrame();
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
	// --p1pal N / --p2pal N: the fighters' palettes in a test run.
	int32_t p1Pal = 0, p2Pal = 0;
	bool checkRun = false, soakRun = false;
#ifndef M360_X360
	bool rlServer = false;
	std::string rlRecordDir;
	int rlPoolCount = 0, rlThreads = (int)std::max(1u, std::thread::hardware_concurrency());
#endif
	// Test runs skip storyboards (logo, intro, arcade intros and endings) unless --storyboards.
	bool storyboards = false;
	uint32_t soakFrames = 3600;
	// --soakstart N: begin at pair N (from 0), to go back to one that crashed.
	size_t soakStart = 0;
	std::map<uint32_t, std::string> shots;
	uint32_t quitFrame = UINT32_MAX;
	int32_t testPower = -1, testP1Life = -1, testP2Life = -1, testWins = -1, testSelectTimer = -1;
	uint32_t testAiSeed = 0;
	// --noai: player 2 stays idle even in modes against the computer, so a test script's hits land the same way.
	bool noAi = false;
	// --ai classic / smart: the computer's AI Style, whatever the options say.
	int32_t testAiStyle = -1;
	bool trace = false, quickStart = false, cpu = false, show = false, forceSelect = false, forceTitle = false;
	for (int i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		if (arg == "--ai" && i + 1 < argc)
			testAiStyle = std::string(argv[++i]) == "classic" ? 0 : 1;
		else if (arg == "--aiseed" && i + 1 < argc)
			testAiSeed = (uint32_t)text::atoi(argv[++i]);
		else if (arg == "--input" && i + 1 < argc)
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
		else if (arg == "--charseed" && i + 1 < argc)
			playedFrames = (uint32_t)text::atoi(argv[++i]);
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
		else if (arg == "--p1life" && i + 1 < argc)
			testP1Life = text::atoi(argv[++i]);
		else if (arg == "--selecttimer" && i + 1 < argc)
			testSelectTimer = text::atoi(argv[++i]);
		else if (arg == "--wins" && i + 1 < argc)
			testWins = text::atoi(argv[++i]);
		else if (arg == "--p1" && i + 1 < argc)
			p1Arg = argv[++i];
		else if (arg == "--p1pal" && i + 1 < argc)
			p1Pal = text::atoi(argv[++i]);
		else if (arg == "--p2pal" && i + 1 < argc)
			p2Pal = text::atoi(argv[++i]);
		else if (arg == "--p2" && i + 1 < argc)
			p2Arg = argv[++i];
		else if (arg == "--stage" && i + 1 < argc)
			stageArg = argv[++i];
		else if (arg == "--game" && i + 1 < argc)
			config.game = argv[++i];
#ifndef M360_X360
		else if (arg == "--rl-server")
			rlServer = true;
		else if (arg == "--rl-record" && i + 1 < argc)
			rlRecordDir = argv[++i];
		else if (arg == "--rl-pool" && i + 1 < argc) {
			rlServer = true;
			rlPoolCount = std::max(1, text::atoi(argv[++i]));
		} else if (arg == "--rl-threads" && i + 1 < argc)
			rlThreads = std::max(1, text::atoi(argv[++i]));
#endif
		else if (arg == "--soak") {
			// --soak [folder]: every character against the next, computer against computer (13d).
			soakRun = true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
				config.game = argv[++i];
		} else if (arg == "--soakframes" && i + 1 < argc)
			soakFrames = (uint32_t)text::atoi(argv[++i]);
		else if (arg == "--soakstart" && i + 1 < argc)
			soakStart = (size_t)std::max(0, text::atoi(argv[++i]));
		else if (arg == "--check") {
			// --check [folder]: a game's compatibility report, without playing (13b).
			checkRun = true;
			if (i + 1 < argc && argv[i + 1][0] != '-')
				config.game = argv[++i];
		}
	}
	const bool testRun = !inputPath.empty() || !shots.empty() || quitFrame != UINT32_MAX || checkRun || soakRun
#ifndef M360_X360
		|| rlServer
#endif
		;
	const char *logName = testRun ? "m360.log" : "m360_play.log";
#ifndef M360_X360
	if (rlServer)
		logName = nullptr;
#endif
	if (!platform::init(testRun && !show, logName))
		return 1;
	if (!testRun)
		showLoadingScreen(config.game);
	logf("mugen360: milestone 13e.3, effects, pause menu, 4 KB texture pages, build %s", buildVersion());
	// The options, saved in save/config.ini. Test runs keep Ikemen's defaults, so a player's saved options never
	// change a test's result.
	Settings settings;
	if (testRun)
		logf("settings: test run, defaults");
	// --wins N: rounds to win a match in a test run (one ends it after a single round).
	if (testRun) {
		if (testWins > 0)
			settings.matchWins = testWins;
	} else if (!settings.load(SETTINGS_PATH))
		logf("settings: no %s yet, defaults", SETTINGS_PATH);
	if (testAiStyle >= 0)
		settings.aiStyle = testAiStyle;
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
	if (p1Pal > 0)
		config.p1Palette = p1Pal;
	if (p2Pal > 0)
		config.p2Palette = p2Pal;
	if (!stageArg.empty())
		config.stage = stageArg;
	logf("game: p1=%s p2=%s stage=%s", config.player1.c_str(), config.player2.c_str(), config.stage.c_str());
#ifndef M360_X360
	if (rlServer) {
		const int result = rlPoolCount > 0 ? runRlPool(config, settings, rlPoolCount, rlThreads)
										   : runRlServer(config, settings, rlRecordDir, show, trace);
		platform::shutdown();
		return result;
	}
#endif
	// --soak: fight every pair, write soak_<game>.txt next to the log, and stop.
	if (soakRun) {
		std::string name = config.game.empty() ? std::string("mugen360") : config.game;
		name = name.substr(name.find_last_of('/') + 1);
		const std::string path = "soak_" + name + ".txt";
		const std::string report = runSoak(config, settings, soakFrames, path, soakStart);
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
		if (motifSelectLoaded && testSelectTimer >= 0)
			motifSelect.setTimerCount(testSelectTimer);
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
			// The screens' textures go first, so the fight's files have the most room (they're made again when drawn).
			SpriteRenderer::freeAll();
			logMemory("before the fight loads");
			stage = std::make_unique<StageDemo>(fightAssets);
			stageLoaded = stage->load(fight, error, trace, quickStart, (cpu || modeCpu) && !noAi);
			if (stageLoaded && testAiSeed != 0)
				stage->seedAi(testAiSeed);
			logMemory("fight loaded");
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
			if (stageLoaded && testP1Life >= 0)
				stage->setTestLife(0, testP1Life);
			if (!stageLoaded)
				logf("mugen360: %s", error.c_str());
			if (selectLoaded)
				stage->returnToSelectAfterMatch();
		};
		// VS NEURAL CAP (data/neural/neural.ini): a trained policy plays player 1, the person picks one of the opponents
		// it trained against and plays player 2. Only when the game has the policy's character, stage and opponents.
		NeuralPolicy neuralPolicy;
		NeuralSelect neuralSelect;
		std::string neuralCharacter, neuralStage;
		bool neuralLoaded = false, neuralFight = false;
		uint32_t neuralSeed = 0x2545F491u;
		{
			std::vector<uint8_t> bytes;
			const std::vector<IniSection> sections = readFile("data/neural/neural.ini", bytes)
				? parseIni(std::string(bytes.begin(), bytes.end()))
				: std::vector<IniSection>{};
			if (const IniSection *n = findSection(sections, "neural")) {
				neuralCharacter = text::trim(n->text("character"));
				neuralStage = text::trim(n->text("stage"));
				std::vector<NeuralSelect::Opponent> opponents;
				for (int i = 1; i <= 16; i++) {
					const std::vector<std::string> parts = text::split(n->text("opponent" + std::to_string(i)), ',');
					if (parts.empty() || text::trim(parts[0]).empty())
						break;
					NeuralSelect::Opponent o;
					o.def = text::trim(parts[0]);
					o.trainedWins = parts.size() > 1 ? (float)std::atof(std::string(text::trim(parts[1])).c_str()) : 0.0f;
					if (fileExists(o.def))
						opponents.push_back(o);
				}
				std::string why;
				if (!fileExists(neuralCharacter) || !fileExists(neuralStage))
					why = "this game hasn't got " + neuralCharacter + " and " + neuralStage;
				else if (neuralPolicy.load(std::string(text::trim(n->text("policy"))), why))
					neuralLoaded = neuralSelect.load(neuralCharacter, opponents, why);
				if (neuralLoaded)
					logf("neural: VS NEURAL CAP ready, %zu opponents", opponents.size());
				else
					logf("neural: no VS NEURAL CAP (%s)", why.c_str());
			}
		}
		if (neuralLoaded && titleLoaded) {
			// In the menu: a row of its own when the menu draws text; in a painted menu, the row of a mode mugen360
			// can't run yet, with its own label; on LB if there's no such row.
			if (title.itemsShowText())
				title.addItem("neuralvs", "VS NEURAL CAP", "options");
			else if (!title.claimPlaceholder("neuralvs", "NEURAL CAP"))
				title.addShortcut("LB (Q ON PC): VS NEURAL CAP", TitleChoice::NeuralVs);
		}
		auto startNeuralFight = [&]() {
			GameConfig fight = config;
			fight.player1 = neuralCharacter;
			fight.p1Palette = 1; // its palette in training (Cap's shield style goes by PalNo)
			fight.player2 = neuralSelect.picked();
			fight.p2Palette = 1;
			fight.stage = neuralStage;
			fight.team1.clear();
			fight.team2.clear();
			mode = SelectMode::Versus;
			modeCpu = simul = turnsFight = false;
			neuralFight = true;
			startFight(fight);
			if (stageLoaded) {
				neuralSeed = neuralSeed * 1664525u + 1013904223u;
				stage->setNeural(&neuralPolicy, neuralPolicy.characterId(fight.player1),
					neuralPolicy.characterId(fight.player2), neuralSeed, cpu);
			}
		};
		Screen screen = Screen::Stage;
		if (forcedMode == "neural" && neuralLoaded) {
			neuralSelect.reset();
			screen = Screen::NeuralSelect;
		} else if (titleLoaded) {
			screen = Screen::Title;
		} else if (selectLoaded) {
			resetSelect(inputPath.empty() ? platform::pad(1).connected : !input2Path.empty());
			screen = Screen::Select;
		} else {
			startFight(config);
		}
		// The viewers get a Kung Fu Man of their own, loaded the first time they're opened (a whole character kept for
		// them would take memory a big game needs on the 360).
		std::unique_ptr<Viewers> viewers;
		Buttons buttons;
		// Where the debug screens go back to.
		Screen gameScreen = screen;

		// Storyboards (13e): the screenpack's logo and intro before the title, a character's arcade intro before its
		// first match and ending after its last. They play one after another from storyQueue, then afterStory runs.
		std::unique_ptr<Storyboard> story;
		std::vector<std::string> storyQueue;
		std::function<void()> afterStory;
		Press storySkip, storyCancel;
		// What follows the victory screen (the next match, the select screen...).
		std::function<void()> afterVictory;
		// The continue screen after an arcade loss, over the lost fight's last frame (13e).
		ContinueScreen continueScreen;
		if (motifLoaded)
			continueScreen.load(motif);
		std::shared_ptr<StageDemo> continueBackdrop;
		// The pause menu: Back in a fight opens it over the frozen fight (Ikemen's menu.lua).
		PauseMenu pauseMenu;
		if (motifLoaded)
			pauseMenu.load(motif);
		bool paused = false;
		// After the game ends (a "no" to continue, or the arcade route's ending and credits): the title, else the
		// select screen.
		auto toTitle = [&](bool secondController) {
			if (titleLoaded) {
				title.reset();
				screen = gameScreen = Screen::Title;
			} else {
				resetSelect(secondController);
				screen = gameScreen = Screen::Select;
			}
		};
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
				logMemory("now");
				frameSum = frameWorst = 0;
				frameCount = frameSlow = 0;
			}
			platform::endFrame();
			SpriteRenderer::newFrame();
			frameStart = Clock::now();
		};
		for (uint32_t frame = 0; frame <= quitFrame && platform::beginFrame(BACKGROUND); frame++) {
			if (!testRun)
				playedFrames++;
			const platform::Pad pad = inputPath.empty() ? platform::pad(0) : script.at(frame);
			// A missing --input2 is a connected, idle dummy during scripted fights. Selection separately uses the path's
			// presence so player 1 still picks the opponent, matching the documented headless-test behavior.
			const platform::Pad pad2 = inputPath.empty() ? platform::pad(1) : script2.at(frame);
			const bool secondController = inputPath.empty() ? pad2.connected : !input2Path.empty();
			// Back: out of a fight to the select screen, out of the select screen to the title, else quit. The title
			// handles it itself (it moves to Exit).
			const bool back = buttons.back.update(pad.back);
			// The screenpack select screen handles Back itself (out of the palette menu, else back to the title).
			const bool selectHandlesBack = (screen == Screen::Select && motifSelectLoaded) || screen == Screen::Storyboard ||
				screen == Screen::Victory || screen == Screen::Continue;
			if (back && screen == Screen::Stage && stage && stageLoaded && pauseMenu.loaded() && !paused) {
				// Each human player's command list (Ikemen lists those with no AI).
				std::vector<PauseMenu::Movelist> movelists;
				for (int i = 0; i < stage->fighterCount(); i++) {
					const Character &c = stage->fighter(i);
					if (c.aiLevel == 0)
						movelists.push_back(PauseMenu::Movelist{ c.name, c.movelist });
				}
				pauseMenu.open(std::move(movelists));
				paused = true;
				logf("pause: open");
			} else if (back && screen == Screen::Stage && paused) {
				// The pause menu takes Back itself.
			} else if (back && screen == Screen::Stage && selectLoaded) {
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
			else if (gameScreen == Screen::Victory)
				wantedMusic = "victory";
			else if (gameScreen == Screen::Continue)
				wantedMusic = "continue";
			else if (gameScreen == Screen::Storyboard && story)
				wantedMusic = "story " + story->path() + " " + std::to_string(story->sceneIndex());
			if (wantedMusic != musicFor) {
				musicFor = wantedMusic;
				if (wantedMusic == "title" || wantedMusic == "select")
					music.request(motifLoaded ? motif.music(wantedMusic) : MusicTrack{}, true);
				else if (wantedMusic == "vs" || wantedMusic == "option" || wantedMusic == "victory" ||
					wantedMusic == "continue")
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
				if (screen == Screen::Animations && !viewers)
					viewers = std::make_unique<Viewers>(config.player1);
			}
			platform::Pad gameplayPad = pad;
			if (pad.menu) {
				gameplayPad.start = false;
				gameplayPad.lb = false;
			}
			if (screen == Screen::Continue) {
				if (continueBackdrop)
					continueBackdrop->draw();
				continueScreen.update(gameplayPad);
				continueScreen.draw();
				finishFrame();
				if (continueScreen.done()) {
					continueBackdrop.reset();
					if (continueScreen.continued()) {
						// Continue: the same match again.
						startFight(selectedFight);
						screen = gameScreen = Screen::Stage;
					} else {
						// Game over: [Game Over Screen]'s storyboard, then the title.
						const std::string gameOver = continueScreen.gameOverScreen()
							? storyboardFor(motif.path(), "game over screen", "storyboard")
							: std::string();
						playStories({ gameOver }, [&, secondController]() { toTitle(secondController); });
					}
				}
				continue;
			}
			if (screen == Screen::Victory) {
				motifSelect.updateVictory(gameplayPad, pad2);
				motifSelect.drawVictory();
				finishFrame();
				if (motifSelect.victoryDone() && afterVictory) {
					const std::function<void()> then = afterVictory;
					afterVictory = nullptr;
					then();
				}
				continue;
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
				if (choice != TitleChoice::None)
					neuralFight = false;
				if (choice == TitleChoice::Options && optionsLoaded) {
					options.reset();
					screen = gameScreen = Screen::Options;
				} else if (choice == TitleChoice::NeuralVs && neuralLoaded) {
					neuralSelect.reset();
					screen = gameScreen = Screen::NeuralSelect;
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
			if (screen == Screen::NeuralSelect) {
				neuralSelect.update(gameplayPad);
				neuralSelect.draw();
				finishFrame();
				neuralSeed += 0x9E3779B9u; // how long the player takes varies the policy's play
				if (neuralSelect.done()) {
					startNeuralFight();
					screen = gameScreen = Screen::Stage;
				} else if (neuralSelect.cancelled()) {
					neuralFight = false;
					toTitle(secondController);
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
			const bool loaded = screen == Screen::Stage ? stageLoaded : viewers && viewers->loaded;
			if (!loaded) {
				drawText("COULDN'T LOAD:", 40, 40, 4, ERROR_TEXT);
				drawText(error.c_str(), 40, 90, 2, TEXT);
				drawText("TAB / LB+START: NEXT SCREEN   BACK: QUIT", 40, 680, 2, DIM);
			} else if (screen == Screen::Stage && paused) {
				// The fight holds still under the menu.
				stage->draw(!pauseMenu.hideBars());
				pauseMenu.update(gameplayPad, pad2);
				pauseMenu.draw();
				const PauseChoice choice = pauseMenu.result();
				if (choice != PauseChoice::None) {
					paused = false;
					if (choice != PauseChoice::Continue) {
						// Character Change: back to the select screen; Exit: to the title (Ikemen's endMatch).
						stage.reset();
						continueBackdrop.reset();
						if (choice == PauseChoice::CharacterChange && neuralFight) {
							neuralSelect.reset();
							screen = gameScreen = Screen::NeuralSelect;
						} else if (choice == PauseChoice::CharacterChange && selectLoaded) {
							resetSelect(secondController);
							screen = gameScreen = Screen::Select;
						} else {
							toTitle(secondController);
						}
					}
				}
			} else if (screen == Screen::Stage) {
				stage->update(buttons, gameplayPad, pad2);
				stage->draw();
				// Match over: back to pick again.
				if (stage->finished() && neuralFight) {
					// VS NEURAL CAP: pick again (or Back to the title).
					stage.reset();
					neuralSelect.reset();
					screen = gameScreen = Screen::NeuralSelect;
				} else if (stage->finished()) {
					int winner = stage->winner();
					winCounts[0] = stage->winCount(0);
					winCounts[1] = stage->winCount(1);
					int32_t life[2] = { stage->fighterLife(0), stage->fighterLife(1) };
					const int32_t lifeMax[2] = { stage->fighterLifeMax(0), stage->fighterLifeMax(1) };
					const int32_t power[2] = { stage->fighterPower(0), stage->fighterPower(1) };
					const int32_t timeLeft = stage->timeLeft();
					const std::string quotes[2] = { stage->victoryQuote(0), stage->victoryQuote(1) };
					// An arcade loss keeps the fight's last frame, for the continue screen to draw over.
					if ((mode == SelectMode::Arcade || mode == SelectMode::TeamArcade) && winner == 1)
						continueBackdrop = std::move(stage);
					stage.reset();
					logMemory("fight over");
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
							continueBackdrop.reset();
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
					// The victory screen first, after a decided match in the modes Ikemen shows it in (arcade, versus,
					// quick match, watch: main.lua's victoryscreen); then on as before.
					auto proceed = [&, nextTurn, nextSurvival, arcadeMode, winner, secondController]() {
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
							if (arcadeMode && winner != 0 && motifLoaded && continueScreen.enabled()) {
								// Lost: Continue? (Ikemen's continue screen, arcade only).
								continueScreen.start();
								screen = gameScreen = Screen::Continue;
							} else {
								continueBackdrop.reset();
								// A finished arcade route: the character's ending, the end credits, then the title.
								const bool complete = arcadeMode && winner == 0;
								std::vector<std::string> stories;
								if (complete) {
									stories.push_back(storyboardFor(selectedFight.player1, "arcade", "ending.storyboard"));
									int32_t credits = 1;
									const IniSection *endCredits = motif.section("end credits");
									if (endCredits && (!endCredits->readInt("enabled", credits) || credits != 0))
										stories.push_back(storyboardFor(motif.path(), "end credits", "storyboard"));
								}
								playStories(stories, [&, secondController, complete]() {
									if (complete) {
										toTitle(secondController);
									} else {
										resetSelect(secondController);
										screen = gameScreen = Screen::Select;
									}
								});
							}
						}
					};
					const bool victoryMode = mode == SelectMode::Arcade || mode == SelectMode::TeamArcade ||
						mode == SelectMode::QuickMatch || mode == SelectMode::Versus || mode == SelectMode::TeamVersus ||
						mode == SelectMode::Watch;
					const bool computerWon = winner == 1 && (cpu || modeCpu || mode == SelectMode::Watch);
					if (!nextTurn && victoryMode && winner >= 0 && motifSelectLoaded &&
						motifSelect.startVictory(winner, quotes[winner], computerWon)) {
						afterVictory = proceed;
						screen = gameScreen = Screen::Victory;
					} else {
						proceed();
					}
				}
			} else if (screen == Screen::Animations) {
				viewers->animations.update(buttons, gameplayPad);
				viewers->animations.draw();
			} else {
				viewers->sprites.update(buttons, gameplayPad);
				viewers->sprites.draw();
			}
			finishFrame();
		}
	}

	logf("mugen360: bye (textures %zu MB on a 360)", SpriteRenderer::totalTextureBytes() >> 20);
	platform::shutdown();
	return 0;
}
