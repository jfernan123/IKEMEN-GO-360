// --soak (13d). See soak.h.

#include "app/soak.h"

#include <vector>

#include "app/stage_demo.h"
#include "core/game/compat.h"
#include "platform/platform.h"

namespace m360 {

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

} // namespace m360
