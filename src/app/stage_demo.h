#pragma once

// The fight: a stage, its fighters, the camera, the round and the fight screen (StageDemo), one tick at a time.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/fight_assets.h"
#include "app/shared.h"
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

namespace m360 {

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

} // namespace m360
