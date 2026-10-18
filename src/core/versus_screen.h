#pragma once

#include <cstdint>
#include <string>

#include "core/air.h"
#include "core/bgdef.h"
#include "core/layout.h"
#include "core/motif.h"
#include "core/sff.h"
#include "core/sprite_renderer.h"
#include "platform/platform.h"

namespace m360 {

// Ikemen's VS screen, from a screenpack's [VS Screen] and VersusBG (Ikemen's start.f_selectVersus, f_drawPortraits /
// f_portraitsXCalc, and motif.go's offsets). One character a side: each one's portrait (its own animation, or a
// sprite, in its chosen palette, clipped to its window), the names, the stage's portrait and name, Arcade's match
// number, between VersusBG's layers; it fades in, lasts [VS Screen] time, and a done or skip key cuts it short. Not
// yet: teams and the order select, the timer, background loading and its animations, face2, the VS music (8f).
class VersusScreen {
public:
	// What the select screen hands over for one side. The pointers belong to the select screen and outlive this.
	struct Fighter {
		std::string name;
		const Sff *sff = nullptr;
		SpriteRenderer *renderer = nullptr;
		Animation anim;
		bool hasAnim = false;
		// Ikemen's f_animGet: the screenpack's width over the character's localcoord, animations also by its [Size].
		float resFix = 1, sizeScaleX = 1, sizeScaleY = 1;
		int32_t palette = 1;
	};
	struct StagePick {
		std::string name;
		int32_t number = 1;
		SpriteRenderer *renderer = nullptr;
		float resFix = 1;
	};

	void load(Motif &motif);
	// The animation each side's portrait plays ("p1.anim"), -1 for the sprite ("p1.spr"); the select screen keeps
	// these in each character's sprites.
	int32_t faceAnim() const { return sides_[0].anim; }
	int32_t faceSprite(int i) const { return sides_[0].spr[i]; }
	int32_t stageSprite(int i) const { return stagePortraitSpr_[i]; }
	// matchNumber > 0 shows "Match %i" (Ikemen does in Arcade).
	void start(const Fighter fighters[2], const StagePick &stage, int32_t matchNumber);
	// humans[i]: side i's pad belongs to a person, whose skip key counts.
	void update(const platform::Pad &pad1, const platform::Pad &pad2, const bool humans[2]);
	void draw();
	bool done() const { return done_; }
	// Ticks since it started (negative while fading in), for the tests.
	int32_t counter() const { return counter_; }
	// A portrait's x before its offset: Ikemen's f_portraitsXCalc with "padding", which centres a side's characters
	// in the room for "num" of them. For the tests.
	static float paddedX(float pos, float spacing, int32_t num, int32_t count, int32_t member, bool padding);

private:
	struct Side {
		int32_t anim = -1, spr[2] = { 9000, 1 };
		Layout face;
		float pos[2] = { 0, 0 }, spacing[2] = { 0, 0 };
		int32_t num = 1;
		bool padding = false, applyPal = false;
		TextLayout name;
		float namePos[2] = { 0, 0 };
		Fighter fighter;
	};
	void readSide(Side &side, const std::string &prefix, const IniSection &s);
	void drawPortrait(Side &side);
	void drawElements(bool front);

	Motif *motif_ = nullptr;
	BgDef bg_;
	uint32_t clearColour_ = 0xFF000000;
	Side sides_[2];
	int32_t time_ = 150;
	ScreenFade fadeIn_, fadeOut_;
	TextLayout match_, stageText_;
	std::string matchText_ = "Match %i", stageFormat_ = "Next Stage: %s";
	float stagePos_[2] = { 0, 0 };
	Layout stagePortrait_;
	AnimLayout stageBg_;
	int32_t stagePortraitSpr_[2] = { -1, 0 };

	StagePick stage_;
	int32_t matchNumber_ = 0;
	int32_t counter_ = 0;
	bool fadingOut_ = false, done_ = false;
	platform::Pad before_[2];
};

} // namespace m360
