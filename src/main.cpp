// mugen360's entry point: reads data/mugen360.ini and the command line, then runs the screens (title, options,
// select, the fight, storyboards, victory and continue screens, the debug viewers) one frame at a time. The fight
// itself is StageDemo (app/stage_demo.h); test-run, RL and soak options hand off to app/.
//
// Tab / LB+Start switches to the debug viewers; Back (Esc) goes back a screen. Start remains available to the
// character as MUGEN's s button.

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

#include "app/fight_assets.h"
#include "app/input_script.h"
#include "app/rl_server.h"
#include "app/shared.h"
#include "app/soak.h"
#include "app/stage_demo.h"
#include "app/viewers.h"
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
