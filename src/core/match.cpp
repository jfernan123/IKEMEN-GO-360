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
	intro_ = timings_.startWait + timings_.ctrlTime + 1;
	time_ = ROUND_TIME;
	winposeTime_ = timings_.overWin;
	winWaitTime_ = timings_.forceWin;
	slowTime_ = timings_.slowTime;
	finish_ = Finish::None;
	winTeam_ = -1;
	// A side one win away from the match (Ikemen's decisiveRound).
	for (int i = 0; i < 2; i++)
		decisive_[i] = wins_[i] >= timings_.matchWins - 1;
}

int32_t Match::roundState() const
{
	if (intro_ > timings_.ctrlTime + 1)
		return 0; // pre-intro
	if (intro_ > 0)
		return 1; // intros and the announcement
	if (intro_ == 0 || finish_ == Finish::None)
		return 2; // fighting
	if (intro_ < -timings_.overWait)
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
	w.roundEnded = intro_ < -timings_.overHit;
	// Inputs are ignored at time over, and between losing control and the win poses.
	w.inputWait = (time_ == 0 && finish_ != Finish::None) || (intro_ <= -timings_.overWait && winposeTime_ >= 0);
}

float Match::speed() const
{
	if (slowTime_ <= 0 || intro_ >= 0 || time_ == 0)
		return 1;
	// Slow at first, easing back to full speed over the last slow.fadetime ticks.
	float s = timings_.slowSpeed;
	const int32_t fade = timings_.slowFade;
	if (slowTime_ < fade && fade > 0)
		s += (1 - s) * (float)(fade - slowTime_) / fade;
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

Match::Event Match::step(Character &p1, Character &p2, RoundAnnouncer &announcer)
{
	Character *p[2] = { &p1, &p2 };

	// The round calls hold the round while "Round N" plays and until "Fight" is called.
	if (!announcer.act(*this))
		return Event::None;

	// Intros: a character asserting Intro holds the round until its intro is done.
	const int32_t ctrlTime = timings_.ctrlTime;
	if (intro_ > ctrlTime) {
		intro_--;
		if ((p1.asserts(Special::Intro) || p2.asserts(Special::Intro)) && intro_ <= ctrlTime)
			intro_ = ctrlTime + 1;
	} else if (intro_ > 0) {
		// Back to the start positions as the count to control begins.
		if (intro_ == ctrlTime) {
			for (Character *c : p)
				c->posReset();
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
	if (!training_ && intro_ == 0 && !p1.asserts(Special::TimerFreeze) && !p2.asserts(Special::TimerFreeze) && time_ > 0)
		time_--;

	if (intro_ < -timings_.overHit || decide(p1, p2)) {
		const int32_t rs4 = -timings_.overWait;
		intro_--;
		if (slowTime_ > 0 && time_ != 0)
			slowTime_--;
		// The winner's count goes up once the result can no longer change.
		if (intro_ == -timings_.overHit && finish_ != Finish::None) {
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
			// Wait (up to over.forcewintime) for each player to be done: KO'd, or standing idle with control.
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
		const int32_t end = -(timings_.overWait + timings_.roundOverTime());
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
