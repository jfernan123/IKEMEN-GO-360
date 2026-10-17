#include "core/match.h"

#include <cmath>

#include "core/log.h"

namespace m360 {

void Match::newMatch()
{
	roundNo_ = 1;
	wins_[0] = wins_[1] = 0;
	draws_ = 0;
	newRound();
}

void Match::newRound()
{
	intro_ = START_WAIT + CTRL_TIME + 1;
	announce_ = -1;
	time_ = ROUND_TIME;
	winposeTime_ = OVER_WIN;
	winWaitTime_ = FORCE_WIN;
	slowTime_ = SLOW_TIME;
	finish_ = Finish::None;
	winTeam_ = -1;
}

int32_t Match::roundState() const
{
	if (intro_ > CTRL_TIME + 1)
		return 0; // pre-intro
	if (intro_ > 0)
		return 1; // intros and the announcement
	if (intro_ == 0 || finish_ == Finish::None)
		return 2; // fighting
	if (intro_ < -OVER_WAIT)
		return 4; // win poses
	return 3;     // the round is decided
}

void Match::fillWorld(FightWorld &w) const
{
	w.roundState = roundState();
	w.roundNo = roundNo_;
	w.roundsExisted = roundNo_ - 1;
	w.winTeam = winTeam_;
	w.matchOver = matchOver();
	w.roundEnded = intro_ < -OVER_HIT;
	// Inputs are ignored at time over, and between losing control and the win poses.
	w.inputWait = (time_ == 0 && finish_ != Finish::None) || (intro_ <= -OVER_WAIT && winposeTime_ >= 0);
}

float Match::speed() const
{
	if (slowTime_ <= 0 || intro_ >= 0 || time_ == 0)
		return 1;
	// Slow at first, easing back to full speed over the last SLOW_FADE ticks.
	float s = SLOW_SPEED;
	if (slowTime_ < SLOW_FADE)
		s += (1 - s) * (float)(SLOW_FADE - slowTime_) / SLOW_FADE;
	return s;
}

// The round is over when one side is KO or the clock runs out (Ikemen's roundEndDecision).
bool Match::decide(Character &p1, Character &p2)
{
	if (intro_ > 0)
		return false;
	const bool ko[2] = { !p1.alive(), !p2.alive() };
	if (time_ == 0) {
		// Time over: the one with more life (as a share of the most) wins.
		const float l0 = (float)p1.life / p1.lifeMax(), l1 = (float)p2.life / p2.lifeMax();
		if (std::lround(l0 * 1000) != std::lround(l1 * 1000)) {
			finish_ = Finish::TimeOver;
			winTeam_ = l0 < l1 ? 1 : 0;
		} else {
			finish_ = Finish::TimeDraw;
			winTeam_ = -1;
		}
	}
	// A KO decides it while the double KO window is open.
	if (intro_ >= -1 && (ko[0] || ko[1])) {
		if (ko[0] && ko[1]) {
			finish_ = Finish::DoubleKO;
			winTeam_ = -1;
		} else {
			finish_ = Finish::KO;
			winTeam_ = ko[0] ? 1 : 0;
		}
	}
	return ko[0] || ko[1] || time_ == 0;
}

Match::Event Match::step(Character &p1, Character &p2)
{
	Character *p[2] = { &p1, &p2 };

	// Intros: a character asserting Intro holds the round until its intro is done.
	if (intro_ > CTRL_TIME) {
		intro_--;
		if ((p1.asserts(Special::Intro) || p2.asserts(Special::Intro)) && intro_ <= CTRL_TIME)
			intro_ = CTRL_TIME + 1;
	} else if (intro_ > 0) {
		if (intro_ == CTRL_TIME) {
			// Back to the start positions, then "Round N" and "Fight" before the count to control begins.
			if (announce_ < 0) {
				for (Character *c : p)
					c->posReset();
				announce_ = 0;
			}
			if (announce_ < ANNOUNCE) {
				announce_++;
				return Event::None;
			}
		}
		intro_--;
		if (intro_ == 0) {
			// Fight: everyone standing gets control.
			for (Character *c : p) {
				if (c->alive())
					c->roundStartControl();
			}
		}
	}

	// The clock runs while the round is fought.
	if (intro_ == 0 && !p1.asserts(Special::TimerFreeze) && !p2.asserts(Special::TimerFreeze) && time_ > 0)
		time_--;

	if (intro_ < -OVER_HIT || decide(p1, p2)) {
		const int32_t rs4 = -OVER_WAIT;
		intro_--;
		if (slowTime_ > 0 && time_ != 0)
			slowTime_--;
		// The winner's count goes up once the result can no longer change.
		if (intro_ == -OVER_HIT && finish_ != Finish::None) {
			if (winTeam_ >= 0)
				wins_[winTeam_]++;
			else
				draws_++;
			logf("match: round %d: %s, wins %d-%d", roundNo_,
				finish_ == Finish::KO           ? "KO"
					: finish_ == Finish::DoubleKO ? "double KO"
					: finish_ == Finish::TimeOver ? "time over"
												  : "draw",
				wins_[0], wins_[1]);
		}
		if (winposeTime_ > 0) {
			// Wait (up to FORCE_WIN) for each player to be done: KO'd, or standing idle with control.
			if (winWaitTime_ > 0 && intro_ == rs4 - 1) {
				for (Character *c : p) {
					if (!c->readyForWinPose()) {
						intro_ = rs4;
						break;
					}
				}
			}
			// Control is taken away as the win poses come.
			if (intro_ == rs4 - 1) {
				for (Character *c : p)
					c->ctrl = false;
			}
		}
		if (intro_ < rs4)
			winposeTime_--;
		if (winposeTime_ == 0)
			winWaitTime_ = 0;
		// Win poses (180), losing (170), drawn (175), for whoever is still standing.
		if (winposeTime_ <= 0) {
			for (int i = 0; i < 2; i++) {
				Character &c = *p[i];
				if (!c.overAlive() && c.alive() && !c.hitPause())
					c.startOverState(winTeam_ == i ? 180 : winTeam_ == 1 - i ? 170 : 175);
			}
		}
		if (winWaitTime_ > 0)
			winWaitTime_--;
		// RoundNotOver holds the round open.
		const int32_t end = -(OVER_WAIT + OVER_TIME);
		if ((p1.asserts(Special::RoundNotOver) || p2.asserts(Special::RoundNotOver)) && intro_ == end)
			intro_++;
		if (intro_ < end) {
			if (matchOver())
				return Event::MatchOver;
			roundNo_++;
			return Event::NextRound;
		}
	} else if (intro_ < 0) {
		intro_ = 0;
	}
	return Event::None;
}

} // namespace m360
