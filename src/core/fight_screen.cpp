#include "core/fight_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"
#include "platform/platform.h"

namespace m360 {

namespace {

// The keys "<start><number>." in a section, by number ("p1.front" finds front25., front50.); "max" is -1.
std::map<int32_t, std::string> tierKeys(const IniSection &section, const std::string &start)
{
	std::map<int32_t, std::string> out;
	for (const auto &[key, value] : section.values) {
		if (!text::startsWith(key, start))
			continue;
		const std::string rest = key.substr(start.size());
		const size_t dot = rest.find('.');
		if (dot == std::string::npos || dot == 0)
			continue;
		const std::string number = rest.substr(0, dot);
		if (number == "max")
			out[-1] = number;
		else if (number.find_first_not_of("0123456789") == std::string::npos)
			out.emplace(text::atoi(number), number);
	}
	return out;
}

void readTiers(Tiers<AnimLayout> &tiers, const std::string &prefix, const std::string &name, const IniSection &section,
	const AnimationTable &anims, int32_t layer = 0)
{
	tiers.items[0].read(prefix + name + ".", section, anims, layer);
	for (const auto &[key, number] : tierKeys(section, prefix + name))
		tiers.items[key].read(prefix + name + number + ".", section, anims, layer);
}

// Text tiers; text missing from the def is defaultText (Ikemen's readFSText format string).
void readTiers(Tiers<TextLayout> &tiers, const std::string &prefix, const std::string &name, const IniSection &section,
	const std::string &defaultText, int32_t layer = 0, int32_t align = 0)
{
	const auto readOne = [&](TextLayout &t, const std::string &p) {
		t.read(p, section, align, layer);
		if (!section.has(p + "text"))
			t.text = defaultText;
	};
	readOne(tiers.items[0], prefix + name + ".");
	for (const auto &[key, number] : tierKeys(section, prefix + name))
		readOne(tiers.items[key], prefix + name + number + ".");
}

// The power bar's choice: the largest key reached, or "max" once the bar is full (Ikemen's resolvePBKey).
template <class T> T &pickPower(Tiers<T> &tiers, int32_t value, int32_t max)
{
	int32_t choice = 0;
	bool hasMax = false;
	for (const auto &[key, item] : tiers.items) {
		if (key == -1)
			hasMax = true;
		else if (key > choice && value >= key)
			choice = key;
	}
	if (hasMax && value >= max)
		choice = -1;
	return tiers.items[choice];
}

// Replaces the first "%<what>" with value.
std::string replaceFirst(std::string text, const std::string &what, const std::string &value)
{
	const size_t at = text.find(what);
	if (at != std::string::npos)
		text.replace(at, what.size(), value);
	return text;
}

// Whether a def names a sprite, animation or text under this prefix (Ikemen's sectionExists).
bool elementExists(const IniSection &section, const std::string &prefix)
{
	return section.has(prefix + "text") || section.has(prefix + "spr") || section.has(prefix + "anim");
}

// One axis of a bar's visible rectangle, in screen pixels: range is where the bar runs from and to, relative to pos,
// fill how much of it shows (Ikemen's calcBarFillRect).
void barSpan(int32_t pos, const int32_t range[2], float scale, float origin, float fill, int32_t &start, int32_t &size)
{
	const bool descending = range[0] > range[1];
	const int32_t r0 = std::min(range[0], range[1]), r1 = std::max(range[0], range[1]);
	size = (int32_t)((float)(r1 - r0 + 1) * scale * fill + 0.5f);
	if (descending)
		start = (int32_t)((float)(pos + r1 + 1) * scale + origin + 0.5f) - size;
	else
		start = (int32_t)((float)(pos + r0) * scale + origin + 0.5f);
}

} // namespace

void AnimTextSnd::read(const std::string &prefix, const IniSection &section, const AnimationTable &anims, int32_t layer)
{
	section.ints(prefix + "snd", { &snd[0], &snd[1] });
	text.read(prefix, section, 0, layer);
	anim.read(prefix, section, anims, layer);
	section.readInt(prefix + "displaytime", displayTime);
}

void AnimTextSnd::reset()
{
	anim.reset();
	count = 0;
	text.palfx.restart();
}

void AnimTextSnd::action()
{
	anim.action();
	count++;
	text.step();
}

bool AnimTextSnd::end(int32_t dt, bool inf) const
{
	if (displayTime < 0)
		return anim.ended(inf);
	return dt >= displayTime;
}

void FightScreen::Call::read(const std::string &prefix, const IniSection &section, const AnimationTable &anims,
	int32_t layer, int32_t bgLayer)
{
	main.read(prefix, section, anims, layer);
	top.read(prefix + "top.", section, anims, layer);
	bgs.clear();
	for (int i = 0; i < 32; i++) {
		const std::string bg = prefix + "bg" + std::to_string(i) + ".";
		if (elementExists(section, bg)) {
			bgs.emplace_back();
			bgs.back().read(bg, section, anims, bgLayer);
		}
	}
}

void FightScreen::Call::reset()
{
	main.reset();
	top.reset();
	for (AnimLayout &bg : bgs)
		bg.reset();
}

void FightScreen::Call::action()
{
	top.action();
	main.action();
	for (size_t i = bgs.size(); i-- > 0;)
		bgs[i].action();
}

void FightScreen::Call::draw(float x, float y, int32_t layer, SpriteRenderer &renderer, const FontTable &fonts,
	const ScreenSpace &space, const std::string *text) const
{
	for (const AnimLayout &bg : bgs)
		bg.draw(x, y, layer, renderer, space);
	// AnimTextSnd.Draw: its animation if it has one, else its text, line by line.
	if (main.displayTime <= 0 || main.count <= main.displayTime) {
		if (main.anim.present)
			main.anim.draw(x, y, layer, renderer, space);
		else
			main.text.draw(text ? *text : main.text.text, x, y, layer, fonts, space);
	}
	top.draw(x, y, layer, renderer, space);
}

void FightScreen::setViewport(float originX, float width)
{
	space_.scale = width / (float)std::max(1, localWidth_);
	space_.originX = originX;
	space_.originY = 0;
}

bool FightScreen::load(const std::string &defPath, std::string &error, int32_t defaultWidth, int32_t defaultHeight)
{
	localWidth_ = defaultWidth;
	localHeight_ = defaultHeight;
	std::vector<uint8_t> bytes;
	if (!readFile(defPath, bytes)) {
		error = "can't read " + defPath;
		return false;
	}
	const std::string body(bytes.begin(), bytes.end());
	const std::vector<IniSection> sections = parseIni(body);
	anims_.parse(defPath, body);
	path_ = defPath;

	if (const IniSection *info = findSection(sections, "info"))
		info->ints("localcoord", { &localWidth_, &localHeight_ });
	space_ = ScreenSpace::forLocalcoord(localWidth_, localHeight_);

	const IniSection *files = findSection(sections, "files");
	if (!files) {
		error = defPath + " has no [Files]";
		return false;
	}
	// Files as Ikemen's LoadFile finds them: next to fight.def, the game's root, data/.
	const std::string dir = directoryOf(defPath);
	const std::string sffName = files->text("sff");
	std::string sffError;
	if (sffName.empty() || !sff_.load(findBesideDef(dir, sffName), sffError)) {
		error = defPath + ": " + (sffName.empty() ? "no sff in [Files]" : sffError);
		return false;
	}
	renderer_ = std::make_unique<SpriteRenderer>(sff_);
	const std::string sndName = files->text("snd");
	std::string sndError;
	if (!sndName.empty() && !snd_.load(findBesideDef(dir, sndName), sndError))
		logf("fightscreen: %s: no sounds: %s", defPath.c_str(), sndError.c_str());
	loadFonts(*files, defPath, fonts_);
	// A fight screen can bring its own shared effects and sounds (Ikemen's fightfx.sff / fightfx.air / common.snd).
	for (const auto &[key, member] : { std::pair{ "fightfx.sff", &fightfxSff_ }, std::pair{ "fightfx.air", &fightfxAir_ },
			 std::pair{ "common.snd", &commonSnd_ } }) {
		const std::string name = files->text(key);
		*member = name.empty() ? std::string() : findBesideDef(dir, name);
	}

	for (int i = 0; i < 2; i++) {
		const std::string p = i == 0 ? "p1." : "p2.";
		if (const IniSection *s = findSection(sections, "lifebar"))
			readLifeBar(life_[i], p, *s);
		if (const IniSection *s = findSection(sections, "powerbar"))
			readPowerBar(power_[i], p, *s);
		if (const IniSection *s = findSection(sections, "face"))
			readFace(face_[i], p, *s);
		if (const IniSection *s = findSection(sections, "name"))
			readName(name_[i], p, *s);
		if (const IniSection *s = findSection(sections, "winicon")) {
			WinIcon &w = winIcon_[i];
			s->ints(p + "pos", { &w.pos[0], &w.pos[1] });
			s->ints(p + "iconoffset", { &w.offset[0], &w.offset[1] });
			w.counter.read(p + "counter.", *s);
			if (!s->has(p + "counter.text"))
				w.counter.text = "%i";
			w.bg0.read(p + "bg0.", *s, anims_);
			w.top.read(p + "top.", *s, anims_);
			w.icon[0].read(p + "n.", *s, anims_);
			w.icon[1].read(p + "t.", *s, anims_);
		}
		if (const IniSection *s = findSection(sections, "combo"))
			readCombo(combo_[i], s->has("team" + std::to_string(i + 1) + ".pos") ? "team" + std::to_string(i + 1) + "." : "",
				*s, i);
	}
	// Simul's two members a side: p1 / p3 and p2 / p4 (Ikemen's [Simul Lifebar], [Simul Face], [Simul Name]).
	for (int i = 0; i < 4; i++) {
		const std::string p = "p" + std::to_string(i + 1) + ".";
		if (const IniSection *s = findSection(sections, "simul lifebar"))
			readLifeBar(simulLife_[i], p, *s);
		if (const IniSection *s = findSection(sections, "simul face"))
			readFace(simulFace_[i], p, *s);
		if (const IniSection *s = findSection(sections, "simul name"))
			readName(simulName_[i], p, *s);
	}
	if (const IniSection *s = findSection(sections, "time")) {
		s->ints("pos", { &time_.pos[0], &time_.pos[1] });
		readTiers(time_.counter, "", "counter", *s, "");
		time_.bg.read("bg.", *s, anims_);
		time_.top.read("top.", *s, anims_);
		s->readInt("framespercount", time_.framesPerCount);
	}
	if (const IniSection *s = findSection(sections, "wincount")) {
		for (int i = 0; i < 2; i++) {
			const std::string p = i == 0 ? "p1." : "p2.";
			WinCount &w = winCount_[i];
			s->ints(p + "pos", { &w.pos[0], &w.pos[1] });
			w.text.read(p + "text.", *s);
			w.bg.read(p + "bg.", *s, anims_);
			w.top.read(p + "top.", *s, anims_);
			for (const auto &[key, value] : s->values) {
				if (text::startsWith(key, p + "enabled."))
					w.enabled[key.substr(p.size() + 8)] = text::atoi(value) != 0;
			}
		}
	}
	if (const IniSection *s = findSection(sections, "round"))
		readRound(*s);

	loaded_ = true;
	logf("fightscreen: %s: %dx%d, %zu fonts, ctrl.time %d, over.time %d", defPath.c_str(), (int)localWidth_,
		(int)localHeight_, fonts_.size(), (int)timings_.ctrlTime, (int)timings_.overTime);
	return true;
}

void FightScreen::readLifeBar(LifeBar &bar, const std::string &p, const IniSection &s)
{
	s.ints(p + "pos", { &bar.pos[0], &bar.pos[1] });
	s.ints(p + "range.x", { &bar.rangeX[0], &bar.rangeX[1] });
	s.ints(p + "range.y", { &bar.rangeY[0], &bar.rangeY[1] });
	for (int k = 0; k < 3; k++)
		readTiers(bar.bg[k], p, "bg" + std::to_string(k), s, anims_);
	bar.top.read(p + "top.", s, anims_);
	bar.mid.read(p + "mid.", s, anims_);
	readTiers(bar.front, p, "front", s, anims_);
	int32_t v = 0;
	if (s.readInt("mid.freeze", v))
		bar.midFreeze = v != 0;
	s.readInt("mid.delay", bar.midDelay);
	s.readFloat("mid.mult", bar.midMult);
	s.readFloat("mid.steps", bar.midSteps);
	bar.midSteps = std::max(1.0f, bar.midSteps);
	if (s.readInt(p + "scalefill", v))
		bar.scaleFill = v != 0;
}

void FightScreen::readPowerBar(PowerBar &bar, const std::string &p, const IniSection &s)
{
	s.ints(p + "pos", { &bar.pos[0], &bar.pos[1] });
	s.ints(p + "range.x", { &bar.rangeX[0], &bar.rangeX[1] });
	s.ints(p + "range.y", { &bar.rangeY[0], &bar.rangeY[1] });
	for (int k = 0; k < 3; k++)
		readTiers(bar.bg[k], p, "bg" + std::to_string(k), s, anims_);
	bar.mid.read(p + "mid.", s, anims_);
	bar.top.read(p + "top.", s, anims_);
	readTiers(bar.front, p, "front", s, anims_);
	readTiers(bar.counter, p, "counter", s, "%i");
	s.readInt(p + "counter.format.rounding", bar.counterRounding);
	if (bar.counterRounding < 1)
		bar.counterRounding = 1000;
	for (int i = 0; i < 10; i++) {
		bar.levelSnd[i][0] = -1;
		const std::string key = "level" + std::to_string(i + 1) + ".snd";
		if (!s.ints(p + key, { &bar.levelSnd[i][0], &bar.levelSnd[i][1] }))
			s.ints(key, { &bar.levelSnd[i][0], &bar.levelSnd[i][1] });
	}
	if (!s.ints(p + "levelmax.snd", { &bar.levelMaxSnd[0], &bar.levelMaxSnd[1] }))
		s.ints("levelmax.snd", { &bar.levelMaxSnd[0], &bar.levelMaxSnd[1] });
	int32_t v = 0;
	if (s.readInt(p + "levelbars", v))
		bar.levelBars = v != 0;
	if (s.readInt(p + "scalefill", v))
		bar.scaleFill = v != 0;
}

void FightScreen::readCombo(Combo &co, const std::string &pre, const IniSection &s, int side)
{
	s.ints(pre + "pos", { &co.pos[0], &co.pos[1] });
	s.readFloat(pre + "start.x", co.startX);
	if (side == 1) {
		// MUGEN 1.0 reuses the WinMUGEN code where both sides share one set of values.
		if (pre == "team2.") {
			const float start = co.startX;
			co.startX = (float)localWidth_ - start;
			if (co.startX >= 0)
				co.startX = std::min((float)co.pos[0] - start, 0.0f);
		} else {
			co.pos[0] = localWidth_ - co.pos[0];
		}
	}
	const int32_t align = pre.empty() ? (side == 0 ? 1 : -1) : 0;
	readTiers(co.counter, pre, "counter", s, "%i", 2, align);
	readTiers(co.text, pre, "text", s, "", 2, align);
	co.bg.read(pre + "bg0.", s, anims_, 2);
	co.top.read(pre + "top.", s, anims_, 2);
	s.readInt(pre + "displaytime", co.displayTime);
	s.readFloat(pre + "showspeed", co.showSpeed);
	co.showSpeed = std::max(1.0f, co.showSpeed);
	s.readFloat(pre + "hidespeed", co.hideSpeed);
	co.separator = s.text("format.decimal.separator");
	s.readInt("format.decimal.places", co.places);
	int32_t v = 0;
	if (s.readInt(pre + "autoalign", v))
		co.autoAlign = v != 0;
	co.counterX = co.startX * 2;
}

void FightScreen::readRound(const IniSection &s)
{
	RoundTimings &t = timings_;
	s.ints("pos", { &roundPos_[0], &roundPos_[1] });
	s.readInt("match.wins", t.matchWins);
	s.readInt("match.maxdrawgames", t.maxDraws);
	s.readInt("start.waittime", t.startWait);
	t.startWait = std::max(1, t.startWait);
	s.readInt("round.time", roundTime_);
	roundSndTime_ = roundTime_;
	s.readInt("round.sndtime", roundSndTime_);
	for (int i = 0; i < 9; i++)
		round_[i].read("round" + std::to_string(i + 1) + ".", s, anims_, 2);
	roundDefault_.read("round.default.", s, anims_, 2, 2);
	roundSingle_.read("round.single.", s, anims_, 2, 2);
	roundFinal_.read("round.final.", s, anims_, 2, 2);
	s.readInt("fight.time", fightTime_);
	fightSndTime_ = fightTime_;
	s.readInt("fight.sndtime", fightSndTime_);
	fight_.read("fight.", s, anims_, 2, 2);
	s.readInt("ctrl.time", t.ctrlTime);
	t.ctrlTime = std::max(1, t.ctrlTime);

	s.readInt("ko.time", koTime_);
	koSndTime_ = koTime_;
	s.readInt("ko.sndtime", koSndTime_);
	ko_.read("ko.", s, anims_, 1, 2);
	dkoTime_ = toTime_ = koTime_;
	dkoSndTime_ = toSndTime_ = koSndTime_;
	s.readInt("dko.time", dkoTime_);
	s.readInt("dko.sndtime", dkoSndTime_);
	int32_t v = 0;
	if (s.readInt("dko.showdraw", v))
		dkoShowDraw_ = v != 0;
	dko_.read("dko.", s, anims_, 1, 2);
	s.readInt("to.time", toTime_);
	s.readInt("to.sndtime", toSndTime_);
	to_.read("to.", s, anims_, 1, 2);

	s.readInt("slow.time", t.slowTime);
	t.slowFade = (int32_t)((float)t.slowTime * 0.75f);
	s.readInt("slow.fadetime", t.slowFade);
	t.slowFade = std::min(t.slowFade, t.slowTime);
	s.readFloat("slow.speed", t.slowSpeed);
	t.slowSpeed = std::clamp(t.slowSpeed, 0.01f, 1.0f);
	s.readInt("over.hittime", t.overHit);
	s.readInt("over.waittime", t.overWait);
	s.readInt("over.wintime", t.overWin);
	s.readInt("over.forcewintime", t.forceWin);
	s.readInt("over.time", t.overTime);
	t.overHit = std::max(1, t.overHit);
	t.overWait = std::max(1, t.overWait);
	t.overWin = std::max(1, t.overWin);
	t.forceWin = std::max(1, t.forceWin);
	t.overTime = std::max(1, t.overTime);
	s.readInt("win.time", winTime_);
	winSndTime_ = winTime_;
	s.readInt("win.sndtime", winSndTime_);

	// "p1.win." wins over "win." for player 1's winner call (Ikemen's readATS / readLayout / readBGs).
	for (int i = 0; i < 2; i++) {
		const std::string own = i == 0 ? "p1.win." : "p2.win.";
		Call &call = win_[i];
		call.main.read(elementExists(s, own) ? own : "win.", s, anims_, 1);
		call.top.read(elementExists(s, own + "top.") ? own + "top." : "win.top.", s, anims_, 1);
		call.bgs.clear();
		for (int j = 0; j < 32; j++) {
			const std::string ownBg = own + "bg" + std::to_string(j) + ".", bg = "win.bg" + std::to_string(j) + ".";
			if (elementExists(s, ownBg) || elementExists(s, bg)) {
				call.bgs.emplace_back();
				call.bgs.back().read(elementExists(s, ownBg) ? ownBg : bg, s, anims_, 1);
			}
		}
	}
	drawGame_.read("draw.", s, anims_, 1, 1);
	fadeIn_.read("fadein.", s);
	fadeOut_.read("fadeout.", s);
	s.readInt("callfight.time", callFightTime_);
	t.fadeOutTime = fadeOut_.duration();
}

void FightScreen::play(const int32_t snd[2])
{
	if (snd[0] >= 0)
		snd_.play(snd[0], snd[1]);
}

void FightScreen::setMatchInfo(const std::string &mode, const int winCount[2])
{
	for (int i = 0; i < 2; i++) {
		WinCount &w = winCount_[i];
		w.wins = winCount[i];
		const auto found = w.enabled.find(mode);
		w.active = found != w.enabled.end() && found->second;
	}
}

void FightScreen::newMatch()
{
	matchCounted_ = false;
	for (WinIcon &w : winIcon_)
		w.wins.clear();
	for (PowerBar &p : power_)
		p.started = false;
}

void FightScreen::reset()
{
	fadeOut_.stop();
	fadeIn_.start(true);
	for (int i = 0; i < 9; i++)
		round_[i].reset();
	for (Call *c : { &roundDefault_, &roundSingle_, &roundFinal_, &fight_, &ko_, &dko_, &to_, &drawGame_, &win_[0], &win_[1] })
		c->reset();
	roundTimer_ = fightTimer_ = koTimer_ = winTimer_ = 0;
	roundPhase_ = fightPhase_ = koPhase_ = winPhase_ = 0;
	outroStarted_ = false;
	for (LifeBar &l : simulLife_) {
		for (Tiers<AnimLayout> &t : l.bg)
			for (auto &[key, a] : t.items)
				a.reset();
		l.top.reset();
		l.mid.reset();
		for (auto &[key, a] : l.front.items)
			a.reset();
	}
	for (int i = 0; i < 2; i++) {
		LifeBar &l = life_[i];
		for (Tiers<AnimLayout> &t : l.bg)
			for (auto &[key, a] : t.items)
				a.reset();
		l.top.reset();
		l.mid.reset();
		for (auto &[key, a] : l.front.items)
			a.reset();
		PowerBar &p = power_[i];
		for (Tiers<AnimLayout> &t : p.bg)
			for (auto &[key, a] : t.items)
				a.reset();
		p.top.reset();
		p.mid.reset();
		for (auto &[key, a] : p.front.items)
			a.reset();
		for (AnimLayout &a : face_[i].bg)
			a.reset();
		face_[i].top.reset();
		name_[i].bg.reset();
		name_[i].top.reset();
		Combo &c = combo_[i];
		c.bg.reset();
		c.top.reset();
		c.shownHits = c.shownDamage = c.restTime = 0;
		c.shownPercent = 0;
		c.counterX = c.startX * 2;
	}
	time_.bg.reset();
	time_.top.reset();
}

void FightScreen::skipIntro()
{
	roundPhase_ = fightPhase_ = 2;
}

// Ikemen's FightScreenRound.act: whether the round's clock moves this tick.
bool FightScreen::act(const Match &match)
{
	if (fadeOut_.isActive()) {
		if (fadeOut_.step())
			play(fadeOut_.snd);
	} else if (fadeIn_.isActive()) {
		if (fadeIn_.step())
			play(fadeIn_.snd);
	}
	// Before the intros end: nothing has been called yet.
	if (match.intro() > timings_.ctrlTime) {
		roundPhase_ = fightPhase_ = 0;
		roundTimer_ = fightTimer_ = 0;
		return true;
	}
	if (roundPhase_ < 2 || fightPhase_ < 2)
		handleRoundIntro(match);
	// Once the round is decided, the KO call; it then plays on each frame (see frame()).
	if (fightPhase_ == 2 && match.intro() < 0 && (match.finish() != Match::Finish::None || match.timeLeft() == 0)) {
		if (!outroStarted_) {
			outroStarted_ = true;
			handleRoundOutro(match);
		}
		return true;
	}
	return fightPhase_ > 0;
}

void FightScreen::frame(const Match &match)
{
	if (outroStarted_ && match.intro() < 0)
		handleRoundOutro(match);
}

// "Round N" then, callfight.time later, "Fight" (Ikemen's handleRoundIntro).
void FightScreen::handleRoundIntro(const Match &match)
{
	const int32_t roundNo = match.roundNo();
	const bool single = match.roundIsSingle() && roundSingle_.main.snd[0] != -1;
	const bool final = !single && match.roundIsFinal() && roundFinal_.main.snd[0] != -1;
	if (roundPhase_ < 2) {
		if (roundTimer_ == roundSndTime_) {
			if (single)
				play(roundSingle_.main.snd);
			else if (final)
				play(roundFinal_.main.snd);
			else if (roundNo <= 9 && round_[roundNo - 1].snd[0] != -1)
				play(round_[roundNo - 1].snd);
			else
				play(roundDefault_.main.snd);
		}
		if (roundTimer_ >= roundTime_) {
			roundPhase_ = 1;
			const int32_t animTime = roundTimer_ - roundTime_ + 1;
			if (single || final) {
				Call &call = single ? roundSingle_ : roundFinal_;
				if (call.main.end(animTime, true) && roundDefault_.main.end(animTime, true)) {
					roundPhase_ = 2;
				} else {
					(call.top.present ? call.top : roundDefault_.top).action();
					call.main.action();
					roundDefault_.main.action();
					for (size_t i = (call.bgs.empty() ? roundDefault_.bgs : call.bgs).size(); i-- > 0;)
						(call.bgs.empty() ? roundDefault_.bgs : call.bgs)[i].action();
				}
			} else if (roundNo <= 9) {
				if (round_[roundNo - 1].end(animTime, true) && roundDefault_.main.end(animTime, true)) {
					roundPhase_ = 2;
				} else {
					roundDefault_.top.action();
					round_[roundNo - 1].action();
					roundDefault_.main.action();
					for (size_t i = roundDefault_.bgs.size(); i-- > 0;)
						roundDefault_.bgs[i].action();
				}
			} else if (roundDefault_.main.end(animTime, true)) {
				roundPhase_ = 2;
			} else {
				roundDefault_.action();
			}
		}
		roundTimer_++;
	}

	if (fightPhase_ == 0) {
		// "Fight" comes callfight.time ticks after the round call started.
		if (roundPhase_ > 0) {
			if (fightTimer_ >= callFightTime_) {
				fight_.main.reset();
				fight_.top.reset();
				fightPhase_ = 1;
				fightTimer_ = 0;
				logf("fightscreen: round %d call, fight call", (int)roundNo);
			} else {
				fightTimer_++;
			}
		}
	} else if (fightPhase_ == 1) {
		if (fightTimer_ == fightSndTime_)
			play(fight_.main.snd);
		if (fightTimer_ >= fightTime_) {
			fight_.action();
			const int32_t animTime = fightTimer_ - fightTime_ + 1;
			if (fight_.main.end(animTime, true) && fightTimer_ >= fightSndTime_)
				fightPhase_ = 2;
		}
		fightTimer_++;
	}
}

// The KO (or double KO, or time over) call, then the winner's (Ikemen's handleRoundOutro).
void FightScreen::handleRoundOutro(const Match &match)
{
	const auto stepCall = [&](AnimTextSnd &ats, int32_t elapsed, int32_t time, int32_t sndTime, int32_t &phase) {
		if (elapsed == sndTime)
			play(ats.snd);
		if (elapsed >= time) {
			if (ats.end(elapsed - time + 1, false)) {
				phase = 2;
			} else {
				ats.action();
				phase = 1;
			}
		}
	};
	const Match::Finish finish = match.finish();
	if (koPhase_ < 2) {
		Call &call = finish == Match::Finish::DoubleKO ? dko_
			: finish == Match::Finish::TimeOver || finish == Match::Finish::TimeDraw ? to_
																					   : ko_;
		const int32_t time = &call == &dko_ ? dkoTime_ : &call == &to_ ? toTime_ : koTime_;
		const int32_t sndTime = &call == &dko_ ? dkoSndTime_ : &call == &to_ ? toSndTime_ : koSndTime_;
		if (koTimer_ == 0)
			logf("fightscreen: %s call", &call == &dko_ ? "DKO" : &call == &to_ ? "TO" : "KO");
		call.top.action();
		stepCall(call.main, koTimer_, time, sndTime, koPhase_);
		for (size_t i = call.bgs.size(); i-- > 0;)
			call.bgs[i].action();
		koTimer_++;
	}
	if (winPhase_ < 2 && match.intro() < -timings_.overWait) {
		const bool draw = finish == Match::Finish::TimeDraw || (finish == Match::Finish::DoubleKO && dkoShowDraw_);
		if (draw || match.winner() >= 0) {
			Call &call = draw ? drawGame_ : win_[match.winner()];
			call.top.action();
			for (size_t i = call.bgs.size(); i-- > 0;)
				call.bgs[i].action();
			stepCall(call.main, winTimer_, winTime_, winSndTime_, winPhase_);
		}
		winTimer_++;
	}
}

void FightScreen::step(const Match &match, const Character &p1, const Character &p2)
{
	// A match won: that side's count goes up; a drawn match clears both (Ikemen, as the win poses start).
	if (match.matchOver() && !matchCounted_) {
		matchCounted_ = true;
		if (match.winner() >= 0)
			winCount_[match.winner()].wins++;
		else
			winCount_[0].wins = winCount_[1].wins = 0;
	}
	for (WinCount &w : winCount_) {
		w.bg.action();
		w.top.action();
		w.text.step();
	}
	// The round's fade-out starts as it nears its end (Ikemen's stepRoundState).
	if (match.intro() == match.fadeOutStart() && !fadeOut_.isActive())
		fadeOut_.start(false);
	const Character *p[2] = { &p1, &p2 };
	for (int i = 0; i < 2; i++) {
		if (partner_[i]) {
			stepLifeBar(simulLife_[i], *p[i]);
			stepLifeBar(simulLife_[i + 2], *partner_[i]);
			stepFaceName(simulFace_[i], simulName_[i]);
			stepFaceName(simulFace_[i + 2], simulName_[i + 2]);
		} else {
			stepLifeBar(life_[i], *p[i]);
			stepFaceName(face_[i], name_[i]);
		}
		stepPowerBar(power_[i], *p[i]);
		// Win icons: one per round won, added as the win poses start (Ikemen adds them at winposetime 0).
		WinIcon &w = winIcon_[i];
		if ((int32_t)w.wins.size() > match.wins(i))
			w.wins.resize((size_t)match.wins(i));
		if ((int32_t)w.wins.size() < match.wins(i) &&
			(match.intro() <= -(timings_.overWait + timings_.overWin) || match.intro() >= 0))
			w.wins.push_back(match.finish() == Match::Finish::TimeOver ? 1 : 0);
		w.bg0.action();
		w.top.action();
		for (AnimLayout &a : w.icon)
			a.action();
	}
	time_.bg.action();
	time_.top.action();
	for (auto &[key, t] : time_.counter.items)
		t.step();
	// The combo against each side's opponent (Ikemen's FightScreen.step).
	for (int side = 0; side < 2; side++) {
		const Character &target = *p[1 - side];
		int32_t hits = 0, damage = 0;
		float percent = 0;
		if (target.receivedHits() > 0 && (target.alive() || !target.overKo())) {
			hits = target.receivedHits();
			damage = target.receivedDamage();
			percent = (float)damage / (float)target.lifeMax() * 100;
		}
		stepCombo(combo_[side], side, hits, damage, percent);
	}
}

// Ikemen's LifeBar.step: the front follows life, the mid (red) part waits while the combo goes on, then drains.
void FightScreen::stepLifeBar(LifeBar &lb, const Character &c)
{
	const float life = (float)c.life / (float)c.lifeMax();
	const bool getHit = (c.receivedHits() != 0 || c.moveType == 'H') && !c.overKo();
	if (lb.topLife > life)
		lb.topLife += (life - lb.topLife) / 2;
	else
		lb.topLife = life;
	const bool hasMid = lb.mid.present;
	if (!lb.midFreeze && getHit && !lb.getHit && hasMid) {
		lb.midLifeTime = lb.midDelay;
		lb.midLife = lb.oldLife;
		lb.midLifeMin = lb.oldLife;
	}
	lb.getHit = getHit;
	if (lb.midFreeze && getHit && hasMid) {
		if (lb.midLifeTime < lb.midDelay) {
			lb.midLifeTime = lb.midDelay;
			lb.midLife = lb.oldLife;
			lb.midLifeMin = lb.oldLife;
		}
	} else {
		if (lb.midLifeTime > 0)
			lb.midLifeTime--;
		if (hasMid && lb.midLifeTime <= 0 && life < lb.midLifeMin) {
			lb.midLifeMin += (life - lb.midLifeMin) * (1 / (12 - (life - lb.midLifeMin) * 144)) * lb.midMult;
			if (lb.midLifeMin < life)
				lb.midLifeMin = life;
		} else {
			lb.midLifeMin = life;
		}
		if ((!hasMid || lb.midLifeTime <= 0) && lb.midLife > lb.midLifeMin)
			lb.midLife += (lb.midLifeMin - lb.midLife) / lb.midSteps;
		lb.oldLife = life;
	}
	const float least = std::max(lb.midLifeMin, life);
	if (lb.midLife < least)
		lb.midLife += (least - lb.midLife) / 2;

	for (Tiers<AnimLayout> &t : lb.bg)
		t.pick(life * 100).action();
	lb.top.action();
	lb.mid.action();
	lb.front.pick(life * 100).action();
}

// Ikemen's PowerBar.step: the level sounds, and the bar's elements.
void FightScreen::stepPowerBar(PowerBar &pb, const Character &c)
{
	const int32_t value = c.power, max = c.powerMax();
	const int32_t level = value / 1000;
	float power = (float)value / (float)max;
	if (pb.levelBars)
		power = (float)value / 1000 - std::min((float)level, (float)max / 1000 - 1);
	pb.midPower -= 1.0f / 144;
	if (power < pb.midPowerMin)
		pb.midPowerMin += (power - pb.midPowerMin) * (1 / (12 - (power - pb.midPowerMin) * 144));
	else
		pb.midPowerMin = power;
	if (pb.midPower < pb.midPowerMin)
		pb.midPower = pb.midPowerMin;
	// The first tick of a match sets the starting level, so it makes no sound.
	if (!pb.started) {
		pb.prevLevel = level;
		pb.prevPower = value;
		pb.started = true;
	}
	if (value >= max && pb.prevPower < max && pb.levelMaxSnd[0] != -1)
		play(pb.levelMaxSnd);
	else if (level > pb.prevLevel && level - 1 < 10)
		play(pb.levelSnd[level - 1]);
	if (level != pb.prevLevel) {
		for (auto &[key, t] : pb.counter.items)
			t.palfx.restart();
	}
	pb.prevLevel = level;
	pb.prevPower = value;
	for (Tiers<AnimLayout> &t : pb.bg)
		pickPower(t, value, max).action();
	pb.top.action();
	pb.mid.action();
	pickPower(pb.front, value, max).action();
	pickPower(pb.counter, value, max).step();
}

// Ikemen's FightScreenCombo.step, without the counter shake.
void FightScreen::stepCombo(Combo &co, int side, int32_t hits, int32_t damage, float percent)
{
	(void)side;
	co.bg.action();
	co.top.action();
	if (hits < 2 && co.shownHits >= 2)
		co.newCombo = true;
	if (co.restTime > 0) {
		// Slide in.
		co.counterX -= co.counterX / co.showSpeed;
		if (std::fabs(co.counterX) < 1)
			co.counterX = 0;
	} else if (hits < 2) {
		// Slide out once the combo is over.
		co.counterX -= co.hideSpeed * (float)localWidth_ / 320;
		if (co.counterX < co.startX * 2)
			co.counterX = co.startX * 2;
	}
	if (co.counterX == 0)
		co.restTime--;
	if (hits >= 2 && (co.newCombo || co.shownHits != hits || co.shownDamage != damage)) {
		if (co.newCombo || co.shownHits != hits) {
			for (auto &[key, t] : co.counter.items)
				t.palfx.restart();
			for (auto &[key, t] : co.text.items)
				t.palfx.restart();
		}
		co.restTime = co.displayTime;
		co.shownHits = hits;
		co.shownDamage = damage;
		co.shownPercent = percent;
		co.newCombo = false;
	}
	if (co.counterX != co.startX * 2) {
		co.counter.pick((float)co.shownHits).step();
		co.text.pick((float)co.shownHits).step();
	}
}

ClipRect FightScreen::barRect(const int32_t pos[2], const int32_t rangeX[2], const int32_t rangeY[2], float fill) const
{
	ClipRect r;
	r.width = platform::SCREEN_WIDTH;
	r.height = platform::SCREEN_HEIGHT;
	if (rangeX[0] != 0 || rangeX[1] != 0)
		barSpan(pos[0], rangeX, space_.scale, space_.originX, fill, r.x, r.width);
	if (rangeY[0] != 0 || rangeY[1] != 0)
		barSpan(pos[1], rangeY, space_.scale, space_.originY, fill, r.y, r.height);
	return r;
}

void FightScreen::drawLifeBar(LifeBar &lb, int32_t layer, const Character &c)
{
	const float life = (float)c.life / (float)c.lifeMax();
	const float x = (float)lb.pos[0], y = (float)lb.pos[1];
	for (Tiers<AnimLayout> &t : lb.bg)
		t.pick(life * 100).draw(x, y, layer, *renderer_, space_);
	ClipRect lr = barRect(lb.pos, lb.rangeX, lb.rangeY, lb.scaleFill ? 1 : lb.topLife);
	ClipRect mr = barRect(lb.pos, lb.rangeX, lb.rangeY, lb.scaleFill ? 1 : lb.midLife);
	float lxs = 1, lys = 1, mxs = 1, mys = 1;
	const bool vertical = lb.rangeY[0] != 0 || lb.rangeY[1] != 0;
	if (lb.scaleFill) {
		// Scale the sprite instead of cutting it.
		(vertical ? lys : lxs) = lb.topLife;
		(vertical ? mys : mxs) = lb.midLife;
	} else if (vertical) {
		if (lb.rangeY[0] < lb.rangeY[1])
			mr.y += lr.height;
		mr.height -= std::min(mr.height, lr.height);
	} else {
		// The mid part shows only where the front doesn't.
		if (lb.rangeX[0] < lb.rangeX[1])
			mr.x += lr.width;
		mr.width -= std::min(mr.width, lr.width);
	}
	lb.mid.draw(x, y, layer, *renderer_, space_, &mr, mxs, mys);
	lb.front.pick(life * 100).draw(x, y, layer, *renderer_, space_, &lr, lxs, lys);
	lb.top.draw(x, y, layer, *renderer_, space_);
}

void FightScreen::drawPowerBar(PowerBar &pb, int32_t layer, const Character &c)
{
	const int32_t value = c.power, max = c.powerMax();
	const int32_t level = value / 1000;
	float power = (float)value / (float)max;
	if (pb.levelBars)
		power = (float)value / 1000 - std::min((float)level, (float)max / 1000 - 1);
	const float x = (float)pb.pos[0], y = (float)pb.pos[1];
	for (Tiers<AnimLayout> &t : pb.bg)
		pickPower(t, value, max).draw(x, y, layer, *renderer_, space_);
	ClipRect pr = barRect(pb.pos, pb.rangeX, pb.rangeY, pb.scaleFill ? 1 : power);
	ClipRect mr = barRect(pb.pos, pb.rangeX, pb.rangeY, pb.scaleFill ? 1 : pb.midPower);
	float pxs = 1, pys = 1, mxs = 1, mys = 1;
	const bool vertical = pb.rangeY[0] != 0 || pb.rangeY[1] != 0;
	if (pb.scaleFill) {
		(vertical ? pys : pxs) = power;
		(vertical ? mys : mxs) = pb.midPower;
	} else if (vertical) {
		if (pb.rangeY[0] < pb.rangeY[1])
			mr.y += pr.height;
		mr.height -= std::min(mr.height, pr.height);
	} else {
		if (pb.rangeX[0] < pb.rangeX[1])
			mr.x += pr.width;
		mr.width -= std::min(mr.width, pr.width);
	}
	pb.mid.draw(x, y, layer, *renderer_, space_, &mr, mxs, mys);
	pickPower(pb.front, value, max).draw(x, y, layer, *renderer_, space_, &pr, pxs, pys);
	TextLayout &counter = pickPower(pb.counter, value, max);
	counter.draw(replaceFirst(counter.text, "%i", std::to_string(value / pb.counterRounding)), x, y, layer, fonts_, space_);
	pb.top.draw(x, y, layer, *renderer_, space_);
}

void FightScreen::readFace(Face &f, const std::string &p, const IniSection &s)
{
	s.ints(p + "pos", { &f.pos[0], &f.pos[1] });
	const char *names[4] = { "bg.", "bg0.", "bg1.", "bg2." };
	for (int k = 0; k < 4; k++)
		f.bg[k].read(p + names[k], s, anims_);
	f.top.read(p + "top.", s, anims_);
	f.ko.read(p + "ko.", s, anims_);
	s.ints(p + "face.spr", { &f.spr[0], &f.spr[1] });
	f.layout.read(p + "face.", s);
}

void FightScreen::readName(Name &n, const std::string &p, const IniSection &s)
{
	s.ints(p + "pos", { &n.pos[0], &n.pos[1] });
	n.name.read(p + "name.", s);
	n.bg.read(p + "bg.", s, anims_);
	n.top.read(p + "top.", s, anims_);
}

void FightScreen::stepFaceName(Face &face, Name &name)
{
	for (AnimLayout &a : face.bg)
		a.action();
	face.top.action();
	face.ko.action();
	name.bg.action();
	name.top.action();
	name.name.step();
}

void FightScreen::drawName(const Name &n, int32_t layer, const Character &c)
{
	const float x = (float)n.pos[0], y = (float)n.pos[1];
	n.bg.draw(x, y, layer, *renderer_, space_);
	n.name.draw(c.name, x, y, layer, fonts_, space_);
	n.top.draw(x, y, layer, *renderer_, space_);
}

void FightScreen::drawFace(const Face &f, int32_t layer, const Character &c)
{
	const float x = (float)f.pos[0], y = (float)f.pos[1];
	for (const AnimLayout &a : f.bg)
		a.draw(x, y, layer, *renderer_, space_);
	if (f.spr[0] >= 0 && f.layout.layer == layer) {
		// The character's portrait in its own palette, scaled from its localcoord to the screenpack's (Ikemen's
		// portraitscale and the fight screen's portraitScale).
		const float scale = c.localScale * space_.scale * (float)localWidth_ / 320;
		SpriteDraw how;
		how.scaleX = f.layout.scaleX * scale;
		how.scaleY = f.layout.scaleY * scale;
		how.flipH = f.layout.facing < 0;
		how.flipV = f.layout.vfacing < 0;
		c.drawSprite(f.spr[0], f.spr[1], space_.x(x + f.layout.offsetX), space_.y(y + f.layout.offsetY), how);
		if (!c.alive())
			f.ko.draw(x, y, layer, *renderer_, space_);
	}
	f.top.draw(x, y, layer, *renderer_, space_);
}

void FightScreen::drawCombo(Combo &co, int32_t layer, int side)
{
	if (co.restTime <= 0 && co.counterX == co.startX * 2)
		return;
	TextLayout &counter = co.counter.pick((float)co.shownHits);
	TextLayout &text = co.text.pick((float)co.shownHits);
	const std::string count = replaceFirst(counter.text, "%i", std::to_string(co.shownHits));
	const auto width = [&](const TextLayout &t, const std::string &line) {
		const auto found = fonts_.find(t.font);
		return found == fonts_.end() ? 0.0f : (float)found->second->textWidth(line, t.bank) * t.layout.scaleX;
	};
	float x = (float)co.pos[0];
	if (side == 0) {
		if (co.startX <= 0)
			x += co.counterX;
		if (co.autoAlign)
			x += width(counter, count);
	} else if (co.startX <= 0) {
		x -= co.counterX;
	}
	const float y = (float)co.pos[1];
	co.bg.draw(x, y, layer, *renderer_, space_);

	// "%i" hits, "%d" damage, "%p" the share of life it took.
	std::string line = replaceFirst(text.text, "%i", std::to_string(co.shownHits));
	line = replaceFirst(line, "%d", std::to_string(co.shownDamage));
	if (line.find("%p") != std::string::npos) {
		const double scaleBy = std::pow(10.0, co.places);
		const double truncated = std::floor(co.shownPercent * scaleBy) / scaleBy;
		char buffer[32];
		std::snprintf(buffer, sizeof buffer, "%.*f", (int)co.places, truncated);
		std::string formatted = buffer;
		if (co.places > 0) {
			const size_t dot = formatted.find('.');
			if (dot != std::string::npos)
				formatted.replace(dot, 1, co.separator);
		}
		line = replaceFirst(line, "%p", formatted);
	}
	float blockWidth = 0;
	size_t start = 0;
	for (;;) {
		const size_t end = line.find("\\n", start);
		blockWidth = std::max(blockWidth, width(text, line.substr(start, end == std::string::npos ? end : end - start)));
		if (end == std::string::npos)
			break;
		start = end + 2;
	}
	text.draw(line, x, y, layer, fonts_, space_);

	float shift = 0;
	if (co.autoAlign)
		shift = side == 0 ? width(counter, count) : blockWidth;
	counter.draw(count, x - shift, y, layer, fonts_, space_);
	co.top.draw(x, y, layer, *renderer_, space_);
}

void FightScreen::draw(int32_t layer, const Match &match, const Character &p1, const Character &p2)
{
	if (!loaded_)
		return;
	const Character *p[2] = { &p1, &p2 };
	for (int i = 0; i < 2; i++) {
		if (partner_[i]) {
			drawLifeBar(simulLife_[i], layer, *p[i]);
			drawLifeBar(simulLife_[i + 2], layer, *partner_[i]);
		} else {
			drawLifeBar(life_[i], layer, *p[i]);
		}
	}
	for (int i = 0; i < 2; i++)
		drawPowerBar(power_[i], layer, *p[i]);
	for (int i = 0; i < 2; i++) {
		if (partner_[i]) {
			drawFace(simulFace_[i], layer, *p[i]);
			drawFace(simulFace_[i + 2], layer, *partner_[i]);
		} else {
			drawFace(face_[i], layer, *p[i]);
		}
	}
	for (int i = 0; i < 2; i++) {
		if (partner_[i]) {
			drawName(simulName_[i], layer, *p[i]);
			drawName(simulName_[i + 2], layer, *partner_[i]);
		} else {
			drawName(name_[i], layer, *p[i]);
		}
	}

	// The clock: whole seconds left, rounded up, in the font for that many ("counter10" from 10 down).
	{
		const float x = (float)time_.pos[0], y = (float)time_.pos[1];
		time_.bg.draw(x, y, layer, *renderer_, space_);
		if (time_.framesPerCount > 0 && match.timeLeft() < 0) {
			// No time limit: "o" in the font for the most time (Ikemen's FightScreenTime.draw).
			time_.counter.pick(1e9f).draw("o", x, y, layer, fonts_, space_);
		} else if (time_.framesPerCount > 0) {
			const int32_t left = (match.timeLeft() + time_.framesPerCount - 1) / time_.framesPerCount;
			time_.counter.pick((float)left).draw(std::to_string(left), x, y, layer, fonts_, space_);
		}
		time_.top.draw(x, y, layer, *renderer_, space_);
	}

	// Matches won in a row ("WINS 2").
	for (const WinCount &w : winCount_) {
		if (!w.active)
			continue;
		const float x = (float)w.pos[0], y = (float)w.pos[1];
		w.bg.draw(x, y, layer, *renderer_, space_);
		w.text.draw(replaceFirst(w.text.text, "%s", std::to_string(w.wins)), x, y, layer, fonts_, space_);
		w.top.draw(x, y, layer, *renderer_, space_);
	}

	// One slot per round needed, filled with an icon for each won (Ikemen's FightScreenWinIcon.draw).
	for (int i = 0; i < 2; i++) {
		WinIcon &w = winIcon_[i];
		const int32_t limit = std::min<int32_t>(4, timings_.matchWins);
		const auto at = [&](int32_t k, float &x, float &y) {
			x = (float)(w.pos[0] + w.offset[0] * k);
			y = (float)(w.pos[1] + w.offset[1] * k);
		};
		float x = 0, y = 0;
		for (int32_t k = 0; k < limit; k++) {
			at(k, x, y);
			w.bg0.draw(x, y, layer, *renderer_, space_);
		}
		if ((int32_t)w.wins.size() > 4) {
			w.counter.draw(replaceFirst(w.counter.text, "%i", std::to_string(w.wins.size())), (float)w.pos[0],
				(float)w.pos[1], layer, fonts_, space_);
		} else {
			for (size_t k = 0; k < w.wins.size(); k++) {
				at((int32_t)k, x, y);
				AnimLayout &icon = w.icon[w.wins[k]].present ? w.icon[w.wins[k]] : w.icon[0];
				icon.draw(x, y, layer, *renderer_, space_);
			}
		}
		for (int32_t k = 0; k < limit; k++) {
			at(k, x, y);
			w.top.draw(x, y, layer, *renderer_, space_);
		}
	}

	for (int i = 0; i < 2; i++)
		drawCombo(combo_[i], layer, i);
	drawRound(layer, match, p1, p2);
}

void FightScreen::drawRound(int32_t layer, const Match &match, const Character &p1, const Character &p2)
{
	const float x = (float)roundPos_[0], y = (float)roundPos_[1];
	const int32_t roundNo = match.roundNo();
	if (roundPhase_ == 1) {
		for (const AnimLayout &bg : roundDefault_.bgs)
			bg.draw(x, y, layer, *renderer_, space_);
		// The round's own call ("round3."), the single or final round's, or just the default one with the number.
		const bool single = match.roundIsSingle() && (roundSingle_.main.drawable(fonts_) || !roundSingle_.bgs.empty());
		const bool final = !single && match.roundIsFinal() && (roundFinal_.main.drawable(fonts_) || !roundFinal_.bgs.empty());
		const AnimTextSnd *ref = nullptr;
		if (single || final) {
			const Call &call = single ? roundSingle_ : roundFinal_;
			for (const AnimLayout &bg : call.bgs)
				bg.draw(x, y, layer, *renderer_, space_);
			ref = &call.main;
		} else if (roundNo <= 9 && round_[roundNo - 1].drawable(fonts_)) {
			ref = &round_[roundNo - 1];
		}
		const std::string number = std::to_string(roundNo);
		const std::string defaultText = ref && !ref->text.text.empty()
			? std::string()
			: replaceFirst(replaceFirst(roundDefault_.main.text.text, "%i", number), "%d", number);
		const AnimTextSnd &def = roundDefault_.main;
		if (def.displayTime <= 0 || def.count <= def.displayTime) {
			if (def.anim.present)
				def.anim.draw(x, y, layer, *renderer_, space_);
			else
				def.text.draw(defaultText, x, y, layer, fonts_, space_);
		}
		if (ref && (ref->displayTime <= 0 || ref->count <= ref->displayTime)) {
			if (ref->anim.present)
				ref->anim.draw(x, y, layer, *renderer_, space_);
			else
				ref->text.draw(replaceFirst(replaceFirst(ref->text.text, "%i", number), "%d", number), x, y, layer,
					fonts_, space_);
		}
		if (single && roundSingle_.top.present)
			roundSingle_.top.draw(x, y, layer, *renderer_, space_);
		else if (final && roundFinal_.top.present)
			roundFinal_.top.draw(x, y, layer, *renderer_, space_);
		else
			roundDefault_.top.draw(x, y, layer, *renderer_, space_);
	}
	if (fightPhase_ == 1)
		fight_.draw(x, y, layer, *renderer_, fonts_, space_);
	if (koPhase_ == 1) {
		const Match::Finish finish = match.finish();
		const Call &call = finish == Match::Finish::DoubleKO ? dko_
			: finish == Match::Finish::TimeOver || finish == Match::Finish::TimeDraw ? to_
																					   : ko_;
		const int32_t time = &call == &dko_ ? dkoTime_ : &call == &to_ ? toTime_ : koTime_;
		if (koTimer_ >= time)
			call.draw(x, y, layer, *renderer_, fonts_, space_);
	}
	if (winPhase_ == 1) {
		const Match::Finish finish = match.finish();
		if (finish == Match::Finish::TimeDraw || (finish == Match::Finish::DoubleKO && dkoShowDraw_)) {
			drawGame_.draw(x, y, layer, *renderer_, fonts_, space_);
		} else if (match.winner() >= 0) {
			// "%s wins": the winner's name; "%d" their player number.
			const int w = match.winner();
			const Character &c = w == 0 ? p1 : p2;
			std::string line = replaceFirst(win_[w].main.text.text, "%d", std::to_string(w + 1));
			line = replaceFirst(line, "%s", c.name);
			win_[w].draw(x, y, layer, *renderer_, fonts_, space_, &line);
		}
	}
}

void FightScreen::drawFade() const
{
	if (!loaded_)
		return;
	if (fadeOut_.isActive())
		fadeOut_.draw();
	else if (fadeIn_.isActive())
		fadeIn_.draw();
}

} // namespace m360
