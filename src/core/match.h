#pragma once

#include <cstdint>

#include "core/character.h"

namespace m360 {

// The rounds of a match between two characters: pre-intro and intros, "Round N" and "Fight", the clock, the end by
// KO or time over, win poses, and best of three. Translated from Ikemen-GO src/system.go (resetRound,
// stepRoundState, roundEndDecision, roundState, runNextRound), with the timings of its default fight screen
// (src/fightscreen.go newFightScreenRound).
//
// The round's clock is Ikemen's "intro" counter: it counts down from start.waittime + ctrl.time + 1 to 0 (intros,
// then the announcement, then control), stays at 0 while the round is fought, and counts down below 0 once it's won.
class Match {
public:
	static constexpr int32_t START_WAIT = 30;  // round.start.waittime: before the intros begin
	static constexpr int32_t CTRL_TIME = 30;   // round.ctrl.time: from "Fight" to control
	static constexpr int32_t ANNOUNCE = 100;   // how long "Round N" and "Fight" show before the count to control
	static constexpr int32_t OVER_HIT = 10;    // round.over.hittime: a double KO can still happen
	static constexpr int32_t OVER_WAIT = 45;   // round.over.waittime: then control is taken away
	static constexpr int32_t OVER_WIN = 45;    // round.over.wintime: then the win poses start
	static constexpr int32_t FORCE_WIN = 900;  // longest wait for players to be ready to pose
	static constexpr int32_t OVER_TIME = 210;  // round.over.time: then the next round
	static constexpr int32_t SLOW_TIME = 60;   // KO slowdown, in game ticks
	static constexpr int32_t SLOW_FADE = 45;
	static constexpr float SLOW_SPEED = 0.25f;
	static constexpr int32_t ROUND_TIME = 99 * 60;
	static constexpr int32_t MATCH_WINS = 2;

	enum class Finish : uint8_t { None, KO, DoubleKO, TimeOver, TimeDraw };
	enum class Event : uint8_t { None, NextRound, MatchOver };

	// A new match: round 1, no wins.
	void newMatch();
	// A new round (call after newMatch or a NextRound event); the characters are reset by the caller.
	void newRound();
	// Straight to the fight, no intros or announcement (the caller gives the characters control).
	void skipIntro()
	{
		intro_ = 0;
		announce_ = ANNOUNCE;
	}

	// One game tick, before the characters act (Ikemen runs stepRoundState first). Returns NextRound or MatchOver
	// once the round's ending has played out.
	Event step(Character &p1, Character &p2);
	// What the characters see of it.
	void fillWorld(FightWorld &w) const;
	// How fast the game runs now: slowed after a KO.
	float speed() const;

	int32_t roundState() const;
	int32_t roundNo() const { return roundNo_; }
	int32_t wins(int side) const { return wins_[side]; }
	int32_t timeLeft() const { return time_; }
	Finish finish() const { return finish_; }
	int winner() const { return winTeam_; }
	bool matchOver() const { return wins_[0] >= MATCH_WINS || wins_[1] >= MATCH_WINS; }
	// For the fight screen: frames since the announcement began (-1 before), and since the round ended (-1 before).
	int32_t announceTime() const { return announce_ >= 0 ? announce_ : -1; }
	int32_t overTime() const { return intro_ < 0 ? -intro_ : -1; }

private:
	bool decide(Character &p1, Character &p2);

	int32_t intro_ = 0;
	int32_t announce_ = -1;
	int32_t time_ = ROUND_TIME;
	int32_t roundNo_ = 1;
	int32_t wins_[2] = { 0, 0 };
	int32_t draws_ = 0;
	int32_t winposeTime_ = OVER_WIN, winWaitTime_ = FORCE_WIN;
	int32_t slowTime_ = SLOW_TIME;
	Finish finish_ = Finish::None;
	int winTeam_ = -1;
};

} // namespace m360
