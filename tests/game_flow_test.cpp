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

	TurnsSeries turns;
	turns.reset(2, 3);
	expect(turns.active(0) == 0 && turns.active(1) == 0 && !turns.over(), "Turns opens with each team's first member");
	expect(turns.result(0) && turns.active(1) == 1, "a Turns loss advances the losing side");
	expect(turns.result(1) && turns.active(0) == 1, "the other side advances after its loss");
	expect(turns.result(-1) == false && turns.over() && turns.winner() == 1,
		"a Turns draw removes both active members and can end the series");
	turns.reset(2, 2);
	expect(turns.result(0) && !turns.result(0) && turns.winner() == 0,
		"eliminating every member reports player 1 as the team winner");
	expect(!turns.result(0) && turns.active(1) == 2, "a completed Turns series ignores later results");
	turns.reset(1, 1);
	expect(!turns.result(-1) && turns.winner() == -1, "a final draw eliminates both teams without inventing a winner");
	turns.reset(0, -2);
	expect(turns.size(0) == 1 && turns.size(1) == 1, "Turns keeps both team sizes valid");
	expect(turnsRecovery(1000, Match::ROUND_TIME, Match::ROUND_TIME) == 275,
		"Turns grants Ikemen's full default recovery bonus when no time elapsed");
	expect(turnsRecovery(1000, Match::ROUND_TIME / 2, Match::ROUND_TIME) == 138,
		"Turns recovery scales with the remaining round time");
	expect(turnsRecovery(1000, 0, Match::ROUND_TIME) == 0 && turnsRecovery(1000, -1, Match::ROUND_TIME) == 275,
		"Turns recovery handles an expired or unlimited clock");

	std::printf("10a-10c modes and team flow: %d passed, %d failed\n", passed, failed);
	return failed;
}
