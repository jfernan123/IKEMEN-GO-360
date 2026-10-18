#pragma once

#include <algorithm>
#include <cstdint>

#include "core/character.h"

namespace m360 {

class Match;

// The round's timings, from the fight screen's [Round] section (Ikemen's FightScreenRound fields). The defaults are
// newFightScreenRound's, used when there's no fight.def.
struct RoundTimings {
	int32_t startWait = 30;   // start.waittime: before the intros begin
	int32_t ctrlTime = 30;    // ctrl.time: from the "Fight" call to control
	int32_t overHit = 10;     // over.hittime: a double KO can still happen
	int32_t overWait = 45;    // over.waittime: then control is taken away
	int32_t overWin = 45;     // over.wintime: then the win poses start
	int32_t forceWin = 900;   // over.forcewintime: longest wait for players to be ready to pose
	int32_t overTime = 210;   // over.time: then the next round
	int32_t slowTime = 60;    // slow.time: KO slowdown, in game ticks
	int32_t slowFade = 45;    // slow.fadetime
	float slowSpeed = 0.25f;  // slow.speed
	int32_t matchWins = 2;    // match.wins
	int32_t maxDraws = 1;     // match.maxdrawgames
	int32_t fadeOutTime = 0;  // how long the round's fade-out lasts (Ikemen's fadeOut.duration)
	// Ikemen's FightScreenRound.overTime: the fade-out can make the round's ending longer.
	int32_t roundOverTime() const { return std::max(overTime, fadeOutTime); }
};

// The fight screen's round calls ("Round 1", "Fight", "KO", who won). Ikemen's stepRoundState only moves the round on
// when they let it (FightScreenRound.act): the intro waits for the "Fight" call.
class RoundAnnouncer {
public:
	virtual ~RoundAnnouncer() = default;
	// Steps the calls one game tick; false holds the round's clock this tick.
	virtual bool act(const Match &match) = 0;
};

// The rounds of a match between two characters: pre-intro and intros, "Round N" and "Fight", the clock, the end by
// KO or time over, win poses, and best of three. Translated from Ikemen-GO src/system.go (resetRound,
// stepRoundState, roundEndDecision, roundState, runNextRound), with the timings of the fight screen's [Round].
//
// The round's clock is Ikemen's "intro" counter: it counts down from start.waittime + ctrl.time + 1 to 0 (intros,
// then the announcement, then control), stays at 0 while the round is fought, and counts down below 0 once it's won.
class Match {
public:
	static constexpr int32_t ROUND_TIME = 99 * 60;

	enum class Finish : uint8_t { None, KO, DoubleKO, TimeOver, TimeDraw };
	enum class Event : uint8_t { None, NextRound, MatchOver };

	void setTimings(const RoundTimings &timings) { timings_ = timings; }
	const RoundTimings &timings() const { return timings_; }

	// A new match: round 1, no wins.
	void newMatch();
	// A new round (call after newMatch or a NextRound event); the characters are reset by the caller.
	void newRound();
	// Straight to the fight, no intros or announcement (the caller gives the characters control and skips the calls).
	void skipIntro() { intro_ = 0; }

	// One game tick, before the characters act (Ikemen runs stepRoundState first). Returns NextRound or MatchOver
	// once the round's ending has played out. The announcer can hold the round (see RoundAnnouncer).
	Event step(Character &p1, Character &p2, RoundAnnouncer &announcer);
	// What the characters see of it.
	void fillWorld(FightWorld &w) const;
	// How fast the game runs now: slowed after a KO.
	float speed() const;

	int32_t roundState() const;
	int32_t roundNo() const { return roundNo_; }
	int32_t wins(int side) const { return wins_[side]; }
	int32_t draws() const { return draws_; }
	int32_t timeLeft() const { return time_; }
	Finish finish() const { return finish_; }
	int winner() const { return winTeam_; }
	bool matchOver() const { return wins_[0] >= timings_.matchWins || wins_[1] >= timings_.matchWins; }
	// Ikemen's sys.intro.
	int32_t intro() const { return intro_; }
	// Frames since the round ended (-1 before).
	int32_t overTime() const { return intro_ < 0 ? -intro_ : -1; }
	// Ikemen's roundIsSingle and roundIsFinal: a one-round match, and a last round after the allowed draws.
	bool roundIsSingle() const { return roundNo_ == 1 && decisive(0) && decisive(1); }
	bool roundIsFinal() const
	{
		return roundNo_ > 1 && decisive(0) && decisive(1) && timings_.maxDraws >= 0 && draws_ >= timings_.maxDraws;
	}
	// The intro value at which the round's fade-out starts (Ikemen's fadeoutStart).
	int32_t fadeOutStart() const
	{
		return -timings_.overWait - 2 - timings_.roundOverTime() + timings_.fadeOutTime;
	}

private:
	bool decide(Character &p1, Character &p2);
	bool decisive(int side) const { return decisive_[side]; }

	RoundTimings timings_;
	int32_t intro_ = 0;
	int32_t time_ = ROUND_TIME;
	int32_t roundNo_ = 1;
	int32_t wins_[2] = { 0, 0 };
	bool decisive_[2] = { false, false };
	int32_t draws_ = 0;
	int32_t winposeTime_ = 45, winWaitTime_ = 900;
	int32_t slowTime_ = 60;
	Finish finish_ = Finish::None;
	int winTeam_ = -1;
};

} // namespace m360
