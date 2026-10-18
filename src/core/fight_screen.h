#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/character.h"
#include "core/ini.h"
#include "core/layout.h"
#include "core/match.h"
#include "core/sff.h"
#include "core/sound.h"
#include "core/sprite_renderer.h"

namespace m360 {

// A screen element that is a sprite or animation, else text, with a sound (Ikemen's AnimTextSnd): "Round 1",
// "Fight", "KO", "%s wins".
struct AnimTextSnd {
	int32_t snd[2] = { -1, 0 };
	TextLayout text;
	AnimLayout anim;
	// Ticks it shows; negative: until its animation ends.
	int32_t displayTime = -2;
	int32_t count = 0;

	void read(const std::string &prefix, const IniSection &section, const AnimationTable &anims, int32_t layer);
	void reset();
	void action();
	// Ikemen's AnimTextSnd.End: done showing after dt ticks.
	bool end(int32_t dt, bool inf) const;
	// Whether it has anything to show.
	bool drawable(const FontTable &fonts) const { return anim.present || (text.present(fonts) && !text.text.empty()); }
};

// Elements picked by a value: "front", "front25", "front50" are the lifebar's front for life from 0%, 25% and 50%
// (Ikemen's readMultipleValues / readMultipleFSText / readPBKeys). Key -1 is "max".
template <class T> struct Tiers {
	std::map<int32_t, T> items;
	// The element with the largest key the value reaches, else key 0's.
	T &pick(float value)
	{
		int32_t best = 0;
		for (const auto &[key, item] : items) {
			if (key > best && value >= (float)key)
				best = key;
		}
		return items[best];
	}
};

// Ikemen's fight screen, read from a screenpack's fight.def: lifebars, power bars, portraits, names, win icons, the
// clock, the combo counter, and the round calls with their fades. Translated from Ikemen-GO src/fightscreen.go
// (LifeBar, PowerBar, FightScreenFace / Name / WinIcon / Time / Combo / Round, FightScreen step / draw), for one
// player a side: no Simul / Tag / Turns, guard or stun bars, red life, [Action] messages, perfect or clutch wins.
class FightScreen : public RoundAnnouncer {
public:
	FightScreen() = default;
	FightScreen(const FightScreen &) = delete;
	FightScreen &operator=(const FightScreen &) = delete;

	bool load(const std::string &defPath, std::string &error);
	// The area the fight is drawn in (a 4:3 stage is pillarboxed): the fight screen fits its width there, from its
	// top, as Ikemen's FightScreen.setScale fits it to the game's viewport.
	void setViewport(float originX, float width);
	bool loaded() const { return loaded_; }
	const RoundTimings &timings() const { return timings_; }

	// A new match (win icons cleared) and a new round (everything back to its start, the fade in begins).
	void newMatch();
	// The mode ([WinCount] shows in the modes it's enabled for) and each side's matches won in a row so far.
	void setMatchInfo(const std::string &mode, const int winCount[2]);
	int winCount(int side) const { return winCount_[side].wins; }
	void reset();
	// --quickstart: the round and fight calls are done.
	void skipIntro();

	// RoundAnnouncer: "Round N" and "Fight", then the KO and winner calls (Ikemen's FightScreenRound.act).
	bool act(const Match &match) override;
	// Each game tick after the characters' states, before hit detection (Ikemen's FightScreen.step).
	void step(const Match &match, const Character &p1, const Character &p2);
	// Each shown frame: the KO and winner calls play at full speed through the KO slowdown.
	void frame(const Match &match);
	// Layer -1, 0 (behind the characters), 1 and 2 (in front).
	void draw(int32_t layer, const Match &match, const Character &p1, const Character &p2);
	// The fades, over everything.
	void drawFade() const;

	// For the plain fallback HUD: which call is showing.
	bool roundCalled() const { return roundPhase_ > 0; }
	bool fightCalled() const { return fightPhase_ > 0; }

private:
	struct LifeBar {
		int32_t pos[2] = { 0, 0 }, rangeX[2] = { 0, 0 }, rangeY[2] = { 0, 0 };
		Tiers<AnimLayout> bg[3], front;
		AnimLayout top, mid;
		float topLife = 0, oldLife = 1, midLife = 1, midLifeMin = 1;
		int32_t midLifeTime = 0;
		bool midFreeze = true, getHit = false, scaleFill = false;
		int32_t midDelay = 30;
		float midMult = 1, midSteps = 8;
	};
	struct PowerBar {
		int32_t pos[2] = { 0, 0 }, rangeX[2] = { 0, 0 }, rangeY[2] = { 0, 0 };
		Tiers<AnimLayout> bg[3], front;
		AnimLayout top, mid;
		Tiers<TextLayout> counter;
		int32_t counterRounding = 1000;
		int32_t levelSnd[10][2] = {}, levelMaxSnd[2] = { -1, 0 };
		bool levelBars = false, scaleFill = false;
		float midPower = 0, midPowerMin = 0;
		int32_t prevLevel = 0, prevPower = 0;
		bool started = false;
	};
	struct Face {
		int32_t pos[2] = { 0, 0 };
		AnimLayout bg[4], top, ko;
		int32_t spr[2] = { -1, 0 };
		Layout layout;
	};
	struct Name {
		int32_t pos[2] = { 0, 0 };
		TextLayout name;
		AnimLayout bg, top;
	};
	struct WinIcon {
		int32_t pos[2] = { 0, 0 }, offset[2] = { 0, 0 };
		TextLayout counter;
		AnimLayout bg0, top;
		// [0] a normal win ("n."), [1] a win at time over ("t.").
		AnimLayout icon[2];
		std::vector<int> wins;
	};
	struct Combo {
		int32_t pos[2] = { 0, 0 };
		float startX = 0;
		Tiers<TextLayout> counter, text;
		AnimLayout bg, top;
		int32_t displayTime = 90;
		float showSpeed = 8, hideSpeed = 4;
		bool autoAlign = true;
		std::string separator;
		int32_t places = 0;
		int32_t shownHits = 0, shownDamage = 0, restTime = 0;
		float shownPercent = 0, counterX = 0;
		bool newCombo = false;
		int32_t count = 0;
	};
	struct Time {
		int32_t pos[2] = { 0, 0 };
		Tiers<TextLayout> counter;
		AnimLayout bg, top;
		int32_t framesPerCount = 60;
	};
	// A round call with its own background layers and top layer.
	struct Call {
		AnimTextSnd main;
		AnimLayout top;
		std::vector<AnimLayout> bgs;
		void read(const std::string &prefix, const IniSection &section, const AnimationTable &anims, int32_t layer,
			int32_t bgLayer);
		void reset();
		void action();
		void draw(float x, float y, int32_t layer, SpriteRenderer &renderer, const FontTable &fonts,
			const ScreenSpace &space, const std::string *text = nullptr) const;
	};

	void readLifeBar(LifeBar &bar, const std::string &prefix, const IniSection &section);
	void readPowerBar(PowerBar &bar, const std::string &prefix, const IniSection &section);
	void readRound(const IniSection &section);
	void readCombo(Combo &combo, const std::string &prefix, const IniSection &section, int side);
	void play(const int32_t snd[2]);

	void stepLifeBar(LifeBar &bar, const Character &c);
	void stepPowerBar(PowerBar &bar, const Character &c);
	void stepCombo(Combo &combo, int side, int32_t hits, int32_t damage, float percent);
	void handleRoundIntro(const Match &match);
	void handleRoundOutro(const Match &match);

	ClipRect barRect(const int32_t pos[2], const int32_t rangeX[2], const int32_t rangeY[2], float fill) const;
	void drawLifeBar(LifeBar &bar, int32_t layer, const Character &c);
	void drawPowerBar(PowerBar &bar, int32_t layer, const Character &c);
	void drawFace(const Face &face, int32_t layer, const Character &c);
	void drawCombo(Combo &combo, int32_t layer, int side);
	void drawRound(int32_t layer, const Match &match, const Character &p1, const Character &p2);

	bool loaded_ = false;
	std::string path_;
	ScreenSpace space_;
	int32_t localWidth_ = 320, localHeight_ = 240;
	Sff sff_;
	std::unique_ptr<SpriteRenderer> renderer_;
	SoundBank snd_;
	AnimationTable anims_;
	FontTable fonts_;
	RoundTimings timings_;

	LifeBar life_[2];
	PowerBar power_[2];
	Face face_[2];
	Name name_[2];
	WinIcon winIcon_[2];
	Combo combo_[2];
	Time time_;
	// [WinCount]: matches won in a row ("WINS %s"), shown in the modes it's enabled for (Ikemen's FightScreenWinCount).
	struct WinCount {
		int32_t pos[2] = { 0, 0 };
		TextLayout text;
		AnimLayout bg, top;
		std::map<std::string, bool> enabled;
		bool active = false;
		int32_t wins = 0;
	};
	WinCount winCount_[2];
	bool matchCounted_ = false;

	// [Round]
	int32_t roundPos_[2] = { 0, 0 };
	int32_t roundTime_ = 0, roundSndTime_ = 0, fightTime_ = 0, fightSndTime_ = 0, callFightTime_ = 60;
	int32_t koTime_ = 0, koSndTime_ = 0, dkoTime_ = 0, dkoSndTime_ = 0, toTime_ = 0, toSndTime_ = 0;
	int32_t winTime_ = 0, winSndTime_ = 0;
	bool dkoShowDraw_ = false;
	AnimTextSnd round_[9];
	Call roundDefault_, roundSingle_, roundFinal_, fight_, ko_, dko_, to_, drawGame_;
	Call win_[2];
	ScreenFade fadeIn_, fadeOut_;

	int32_t roundTimer_ = 0, roundPhase_ = 0, fightTimer_ = 0, fightPhase_ = 0;
	int32_t koTimer_ = 0, koPhase_ = 0, winTimer_ = 0, winPhase_ = 0;
	bool outroStarted_ = false;
};

} // namespace m360
