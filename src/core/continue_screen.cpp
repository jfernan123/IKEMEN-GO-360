#include "core/continue_screen.h"

#include <algorithm>

#include "core/log.h"

namespace m360 {

namespace {

// [Continue Screen] values for keys a screenpack leaves out (Ikemen's defaultMotif.ini).
const char *const CONTINUE_DEFAULTS[][2] = {
	{ "enabled", "1" },
	{ "legacymode.enabled", "1" },
	{ "gameover.enabled", "1" },
	{ "fadein.time", "8" },
	{ "fadeout.time", "120" },
	{ "pos", "160, 40" },
	{ "continue.font", "f-6x9.def, 0, 0, 255, 255, 255, 255" },
	{ "continue.text", "Continue?" },
	{ "yes.font", "f-6x9.def, 0, 0, 191, 191, 191, 255" },
	{ "yes.offset", "-17, 20" },
	{ "yes.text", "Yes" },
	{ "yes.active.font", "f-6x9.def, 0, 0, 255, 255, 255, 255" },
	{ "yes.active.offset", "-17, 20" },
	{ "yes.active.text", "Yes" },
	{ "no.font", "f-6x9.def, 0, 0, 191, 191, 191, 255" },
	{ "no.offset", "15, 20" },
	{ "no.text", "No" },
	{ "no.active.font", "f-6x9.def, 0, 0, 255, 255, 255, 255" },
	{ "no.active.offset", "15, 20" },
	{ "no.active.text", "No" },
	{ "move.snd", "100, 0" },
	{ "done.snd", "100, 1" },
	{ "overlay.col", "0, 0, 0" },
	{ "overlay.alpha", "0, 128" },
	{ "counter.anim", "-1" },
	{ "counter.spr", "-1, 0" },
};

// Ikemen's keys: done = Start, skip = an attack button, move = a direction.
bool doneKey(const platform::Pad &p)
{
	return p.start;
}
bool skipKey(const platform::Pad &p)
{
	return p.a || p.b || p.x || p.y || p.rb || p.rt;
}
bool moveKey(const platform::Pad &p)
{
	return p.up || p.down || p.left || p.right;
}

} // namespace

void ContinueScreen::load(Motif &motif)
{
	motif_ = &motif;
	motif.loadBg("continuebg", bg_, clearColour_);
	IniSection s;
	for (const auto &pair : CONTINUE_DEFAULTS)
		s.values[pair[0]] = pair[1];
	if (const IniSection *own = motif.section("continue screen")) {
		for (const auto &[key, value] : own->values)
			s.values[key] = value;
	}
	int32_t v = 0;
	enabled_ = s.readInt("enabled", v) && v != 0;
	legacy_ = s.readInt("legacymode.enabled", v) && v != 0;
	gameOverEnabled_ = s.readInt("gameover.enabled", v) && v != 0;
	if (const IniSection *over = motif.section("game over screen"))
		gameOverEnabled_ = gameOverEnabled_ && (!over->readInt("enabled", v) || v != 0);
	s.floats("pos", { &pos_[0], &pos_[1] });
	continue_.read("continue.", s);
	yes_.read("yes.", s);
	yesActive_.read("yes.active.", s);
	no_.read("no.", s);
	noActive_.read("no.active.", s);
	for (TextLayout *t : { &continue_, &yes_, &yesActive_, &no_, &noActive_ })
		motif.resolveFont(*t);
	// "alpha = src, dst": the overlay kept at dst over the fight, as a plain blend of 255 - dst.
	int32_t c[3] = { 0, 0, 0 }, alpha[2] = { 0, 128 };
	s.ints("overlay.col", { &c[0], &c[1], &c[2] });
	s.ints("overlay.alpha", { &alpha[0], &alpha[1] });
	overlayColour_ = (uint32_t)std::clamp(255 - alpha[1], 0, 255) << 24 | (uint32_t)(c[0] & 0xFF) << 16 |
		(uint32_t)(c[1] & 0xFF) << 8 | (uint32_t)(c[2] & 0xFF);
	counterAnim_.read("counter.", s, motif.anims(), 2);
	s.readInt("counter.endtime", counterEndTime_);
	s.readInt("counter.skipstart", counterSkipStart_);
	for (int n = 0; n < 10; n++) {
		const std::string p = "counter." + std::to_string(n) + ".";
		s.readInt(p + "skiptime", counts_[n].skipTime);
		s.ints(p + "snd", { &counts_[n].snd[0], &counts_[n].snd[1] });
	}
	s.readInt("counter.end.skiptime", end_.skipTime);
	s.ints("counter.end.snd", { &end_.snd[0], &end_.snd[1] });
	s.ints("move.snd", { &moveSnd_[0], &moveSnd_[1] });
	s.ints("done.snd", { &doneSnd_[0], &doneSnd_[1] });
	fadeIn_.read("fadein.", s);
	fadeOut_.read("fadeout.", s);
}

void ContinueScreen::start()
{
	bg_.reset();
	counterAnim_.reset();
	fadeIn_.start(true);
	fadeOut_.stop();
	counter_ = 0;
	endTimer_ = -1;
	yesSide_ = true;
	selected_ = showEndAnim_ = continued_ = false;
	done_ = false;
	before_ = platform::Pad{};
	before_.start = before_.a = before_.b = before_.x = before_.y = before_.rb = before_.rt = true;
	logf("continue: %s", legacy_ ? "Continue? yes / no" : "counting down");
}

// Ikemen's processSelection: credits are unlimited, so yes always continues.
void ContinueScreen::choose(bool yes)
{
	continued_ = yes;
	selected_ = true;
	endTimer_ = -1;
	logf("continue: %s", yes ? "yes" : "no");
}

// Ikemen's skipCounter: on to the next number's skiptime (the numbers count down, 9 first).
void ContinueScreen::skipCounter()
{
	for (int n = 9; n >= 0; n--) {
		if (counts_[n].skipTime >= 0 && counter_ < counts_[n].skipTime) {
			while (counter_ < counts_[n].skipTime) {
				counter_++;
				counterAnim_.action();
			}
			break;
		}
	}
}

// Ikemen's MotifContinue.step.
void ContinueScreen::update(const platform::Pad &pad)
{
	if (done_)
		return;
	bg_.update();
	if (fadeIn_.isActive())
		fadeIn_.step();
	const bool done = doneKey(pad) && !doneKey(before_), skip = skipKey(pad) && !skipKey(before_),
			   move = moveKey(pad) && !moveKey(before_);
	before_ = pad;
	if (!selected_ || showEndAnim_)
		counterAnim_.action();
	// The countdown's own end plays to counter.endtime before fading out.
	if (selected_ && showEndAnim_ && endTimer_ == -1 && counterEndTime_ > 0 && counter_ >= counterEndTime_) {
		fadeOut_.start(false);
		endTimer_ = counter_ + fadeOut_.duration();
	}
	// A choice made: fade out (Ikemen waits for the characters' poses first; there are none here).
	if (selected_ && !showEndAnim_ && endTimer_ == -1) {
		fadeOut_.start(false);
		endTimer_ = counter_ + fadeOut_.duration();
	}
	if (!selected_) {
		if (legacy_) {
			if (move) {
				motif_->play(moveSnd_);
				yesSide_ = !yesSide_;
			} else if (done || skip) {
				motif_->play(doneSnd_);
				choose(yesSide_);
			}
		} else if (counter_ < end_.skipTime || end_.skipTime < 0) {
			if (done) {
				motif_->play(doneSnd_);
				choose(true);
			} else if (skip && counter_ >= counterSkipStart_) {
				skipCounter();
			}
			for (const Count &c : counts_) {
				if (counter_ == c.skipTime) {
					motif_->play(c.snd);
					break;
				}
			}
		} else if (counter_ == end_.skipTime) {
			motif_->play(end_.snd);
			if (!gameOverEnabled_) {
				// Without a game over screen, the countdown's animation shows the end.
				selected_ = showEndAnim_ = true;
				continued_ = false;
				logf("continue: no");
			} else {
				choose(false);
			}
		}
	}
	if (fadeOut_.isActive())
		fadeOut_.step();
	if (selected_ && endTimer_ != -1 && counter_ >= endTimer_) {
		done_ = true;
		return;
	}
	counter_++;
}

void ContinueScreen::draw()
{
	const ScreenSpace &space = motif_->space();
	const View view = motif_->view();
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, overlayColour_);
	for (int32_t layer = 0; layer <= 2; layer++) {
		bg_.draw(layer, view);
		if (legacy_) {
			continue_.draw(pos_[0], pos_[1], layer, motif_->fonts(), space);
			(yesSide_ ? yesActive_ : yes_).draw(pos_[0], pos_[1], layer, motif_->fonts(), space);
			(yesSide_ ? no_ : noActive_).draw(pos_[0], pos_[1], layer, motif_->fonts(), space);
		} else if ((!selected_ || showEndAnim_) && counterAnim_.present) {
			counterAnim_.draw(0, 0, layer, motif_->renderer(), space);
		}
	}
	if (fadeOut_.isActive())
		fadeOut_.draw();
	else if (fadeIn_.isActive())
		fadeIn_.draw();
}

} // namespace m360
