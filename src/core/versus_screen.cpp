#include "core/versus_screen.h"

#include <algorithm>

#include "core/log.h"

namespace m360 {

namespace {

// [VS Screen] values for keys a screenpack leaves out (Ikemen's data/system.base.def).
const char *const VERSUS_DEFAULTS[][2] = {
	{ "fadein.time", "15" },
	{ "fadeout.time", "15" },
	{ "time", "150" },
	{ "match.text", "Match %i" },
	{ "match.offset", "159, 12" },
	{ "p1.anim", "-1" },
	{ "p1.spr", "9000, 1" },
	{ "p1.facing", "1" },
	{ "p1.num", "1" },
	{ "p2.anim", "-1" },
	{ "p2.spr", "9000, 1" },
	{ "p2.facing", "-1" },
	{ "p2.num", "1" },
	{ "done.key", "a, b, c, x, y, z, s" },
	{ "skip.key", "s" },
	{ "stage.text", "Next Stage: %s" },
	{ "stage.portrait.spr", "-1, 0" },
};

// Ikemen's main.f_formatBySpec with %i (a number) and %s (a name).
std::string format(const std::string &text, int32_t number, const std::string &name)
{
	std::string out = text;
	size_t at = out.find("%i");
	if (at != std::string::npos)
		out.replace(at, 2, std::to_string(number));
	at = out.find("%s");
	if (at != std::string::npos)
		out.replace(at, 2, name);
	return out;
}

// done.key and skip.key: ikemen1's "a, b, c, x, y, z, s" is every attack button and Start, "s" is Start.
bool doneKeys(const platform::Pad &p)
{
	return p.a || p.b || p.x || p.y || p.rb || p.rt || p.start;
}

} // namespace

void VersusScreen::load(Motif &motif)
{
	motif_ = &motif;
	motif.loadBg("versusbg", bg_, clearColour_);
	IniSection s;
	for (const auto &pair : VERSUS_DEFAULTS)
		s.values[pair[0]] = pair[1];
	if (const IniSection *own = motif.section("vs screen")) {
		for (const auto &[key, value] : own->values)
			s.values[key] = value;
	}
	s.readInt("time", time_);
	fadeIn_.read("fadein.", s);
	fadeOut_.read("fadeout.", s);
	match_.read("match.", s);
	matchText_ = s.text("match.text");
	readSide(sides_[0], "p1.", s);
	readSide(sides_[1], "p2.", s);
	s.floats("stage.pos", { &stagePos_[0], &stagePos_[1] });
	stageText_.read("stage.", s);
	stageFormat_ = s.text("stage.text");
	stagePortrait_.read("stage.portrait.", s);
	s.ints("stage.portrait.spr", { &stagePortraitSpr_[0], &stagePortraitSpr_[1] });
	stageBg_.read("stage.portrait.bg.", s, motif.anims());
	for (TextLayout *t : { &match_, &stageText_, &sides_[0].name, &sides_[1].name })
		motif.resolveFont(*t);
}

void VersusScreen::readSide(Side &side, const std::string &p, const IniSection &s)
{
	s.readInt(p + "anim", side.anim);
	s.ints(p + "spr", { &side.spr[0], &side.spr[1] });
	side.face.read(p, s);
	s.floats(p + "pos", { &side.pos[0], &side.pos[1] });
	s.floats(p + "spacing", { &side.spacing[0], &side.spacing[1] });
	s.readInt(p + "num", side.num);
	int32_t v = 0;
	side.padding = s.readInt(p + "padding", v) && v != 0;
	side.applyPal = s.readInt(p + "applypal", v) && v != 0;
	side.name.read(p + "name.", s);
	s.floats(p + "name.pos", { &side.namePos[0], &side.namePos[1] });
}

float VersusScreen::paddedX(float pos, float spacing, int32_t num, int32_t count, int32_t member, bool padding)
{
	if (padding)
		return pos + (float)(2 * member - 1) * spacing * (float)num / (float)(2 * std::max(1, std::min(num, count)));
	return pos + (float)(member - 1) * spacing;
}

void VersusScreen::start(const Fighter fighters[2], const StagePick &stage, int32_t matchNumber)
{
	for (int i = 0; i < 2; i++) {
		sides_[i].fighter = fighters[i];
		sides_[i].fighter.anim.reset();
	}
	stage_ = stage;
	matchNumber_ = matchNumber;
	bg_.reset();
	stageBg_.reset();
	fadeIn_.start(true);
	fadeOut_.stop();
	fadingOut_ = false;
	done_ = false;
	// Ikemen counts from minus the fade-in, so "time" starts once it has faded in.
	counter_ = -fadeIn_.duration();
	// A button still held from the select screen doesn't count as a press.
	for (platform::Pad &p : before_) {
		p = platform::Pad{};
		p.a = p.b = p.x = p.y = p.rb = p.rt = p.start = true;
	}
	logf("versus: %s vs %s", fighters[0].name.c_str(), fighters[1].name.c_str());
}

void VersusScreen::update(const platform::Pad &pad1, const platform::Pad &pad2, const bool humans[2])
{
	if (done_)
		return;
	counter_++;
	bg_.update();
	stageBg_.action();
	for (Side &side : sides_)
		side.fighter.anim.action();
	if (fadeIn_.isActive())
		fadeIn_.step();
	const platform::Pad now[2] = { pad1, pad2 };
	if (!fadingOut_) {
		// Time up, or a person presses a done key (skip, Start, is one of them).
		bool leave = counter_ >= time_;
		for (int i = 0; i < 2; i++) {
			if (humans[i] && doneKeys(now[i]) && !doneKeys(before_[i]))
				leave = true;
		}
		if (leave) {
			fadeOut_.start(false);
			fadingOut_ = true;
			logf("versus: done at %d", (int)counter_);
		}
	}
	if (fadingOut_) {
		fadeOut_.step();
		if (!fadeOut_.isActive())
			done_ = true;
	}
	for (int i = 0; i < 2; i++)
		before_[i] = now[i];
}

// One side's portrait (Ikemen's drawPortraitLayer for one character): at pos plus offset, moved by the padding,
// clipped to its window, in the chosen palette if applypal.
void VersusScreen::drawPortrait(Side &side)
{
	const Fighter &f = side.fighter;
	if (!f.renderer || !f.sff)
		return;
	const ScreenSpace &space = motif_->space();
	const float x = paddedX(side.pos[0] + side.face.offsetX, side.spacing[0], side.num, 1, 1, side.padding);
	const float y = side.pos[1] + side.face.offsetY;
	const int base = f.sff->paletteIndex(1, 1), chosen = f.sff->paletteIndex(1, (uint16_t)f.palette);
	ClipRect window;
	const bool clip = side.face.windowRect(space, window);
	if (clip)
		platform::setClip(window.x, window.y, window.width, window.height);
	SpriteDraw how;
	side.face.apply(how, space);
	if (side.anim >= 0 && f.hasAnim) {
		f.renderer->setPalette(side.applyPal ? chosen : -1);
		how.scaleX = side.face.scaleX * f.resFix * f.sizeScaleX;
		how.scaleY = side.face.scaleY * f.resFix * f.sizeScaleY;
		f.renderer->drawAnimation(f.anim, space.x(x), space.y(y), space.scale, side.face.facing, how);
	} else if (side.spr[0] >= 0) {
		// A sprite with its own palette keeps it; one drawn with the character's 1,1 takes the chosen one.
		f.renderer->setPalette(-1);
		const Sprite *sprite = f.sff->get((uint16_t)side.spr[0], (uint16_t)side.spr[1]);
		if (side.applyPal && sprite && sprite->palette == base)
			how.palette = chosen;
		how.scaleX = side.face.scaleX * f.resFix * space.scale;
		how.scaleY = side.face.scaleY * f.resFix * space.scale;
		how.flipH = side.face.facing < 0;
		f.renderer->drawSprite(side.spr[0], side.spr[1], space.x(x), space.y(y), how);
	}
	f.renderer->setPalette(-1);
	if (clip)
		platform::clearClip();
}

// Ikemen's f_selectVersus draw order, for the elements on one side of VersusBG's layer 1: the portraits, the names,
// the stage's portrait frame, portrait and name, the match number.
void VersusScreen::drawElements(bool front)
{
	Motif &m = *motif_;
	const ScreenSpace &space = m.space();
	const auto inPass = [front](int32_t layer) { return (layer >= 1) == front; };
	for (Side &side : sides_) {
		if (inPass(side.face.layer))
			drawPortrait(side);
	}
	for (const Side &side : sides_) {
		if (inPass(side.name.layout.layer))
			side.name.draw(side.fighter.name, side.namePos[0], side.namePos[1], side.name.layout.layer, m.fonts(), space);
	}
	if (inPass(stageBg_.layout.layer))
		stageBg_.draw(stagePos_[0], stagePos_[1], stageBg_.layout.layer, m.renderer(), space);
	if (stage_.renderer && stagePortraitSpr_[0] >= 0 && inPass(stagePortrait_.layer)) {
		SpriteDraw how;
		stagePortrait_.apply(how, space);
		how.scaleX = stagePortrait_.scaleX * stage_.resFix * space.scale;
		how.scaleY = stagePortrait_.scaleY * stage_.resFix * space.scale;
		how.flipH = stagePortrait_.facing < 0;
		ClipRect window;
		const bool clip = stagePortrait_.windowRect(space, window);
		if (clip)
			platform::setClip(window.x, window.y, window.width, window.height);
		stage_.renderer->drawSprite(stagePortraitSpr_[0], stagePortraitSpr_[1],
			space.x(stagePos_[0] + stagePortrait_.offsetX), space.y(stagePos_[1] + stagePortrait_.offsetY), how);
		if (clip)
			platform::clearClip();
	}
	if (inPass(stageText_.layout.layer))
		stageText_.draw(format(stageFormat_, stage_.number, stage_.name), stagePos_[0], stagePos_[1],
			stageText_.layout.layer, m.fonts(), space);
	if (matchNumber_ > 0 && inPass(match_.layout.layer))
		match_.draw(format(matchText_, matchNumber_, std::string()), 0, 0, match_.layout.layer, m.fonts(), space);
}

void VersusScreen::draw()
{
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, clearColour_);
	const View view = motif_->view();
	bg_.draw(0, view);
	drawElements(false);
	bg_.draw(1, view);
	drawElements(true);
	if (fadeOut_.isActive())
		fadeOut_.draw();
	else if (fadeIn_.isActive())
		fadeIn_.draw();
}

} // namespace m360
