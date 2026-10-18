#include <algorithm>
#include <cstdio>

#include "core/character.h"
#include "core/game_flow.h"
#include "core/match.h"

using namespace m360;

namespace {
int passed, failed;

void expect(bool value, const char *name)
{
	if (value)
		passed++;
	else {
		failed++;
		std::printf("FAIL  10a: %s\n", name);
	}
}

struct ImmediateAnnouncer : RoundAnnouncer {
	bool act(const Match &) override { return true; }
};
} // namespace

int run10aTests()
{
	passed = failed = 0;
	ArcadeLadder ladder;
	ladder.reset(4, 2, 0x12345678u);
	std::vector<int32_t> sorted = ladder.opponents();
	std::sort(sorted.begin(), sorted.end());
	expect(sorted == std::vector<int32_t>({ 0, 1, 2, 3 }), "Arcade route contains every roster fighter once");
	expect(ladder.opponent() != 2 && ladder.matchNo() == 1 && ladder.matchCount() == 4,
		"Arcade route avoids an opening mirror and reports its length");
	expect(ladder.advance() && ladder.matchNo() == 2, "Arcade win advances to match two");
	ladder.advance();
	ladder.advance();
	expect(!ladder.advance() && !ladder.active(), "Arcade route completes after its last opponent");
	ladder.reset(1, 0, 1);
	expect(ladder.opponent() == 0 && !ladder.advance(), "a one-character roster permits its mirror match");

	Character p1, p2;
	p1.life = 37;
	p1.power = 12;
	p1.trainingRestore();
	expect(p1.life == p1.lifeMax() && p1.power == p1.powerMax(), "Training restores a fighter's life and power");
	p2.life = 1000;
	Match match;
	match.setTraining(true);
	match.newMatch();
	match.skipIntro();
	ImmediateAnnouncer announcer;
	for (int i = 0; i < 120; i++)
		match.step(p1, p2, announcer);
	expect(match.timeLeft() == Match::ROUND_TIME && match.finish() == Match::Finish::None,
		"Training keeps the clock at 99 and does not decide the round");

	std::printf("10a modes and Arcade flow: %d passed, %d failed\n", passed, failed);
	return failed;
}
