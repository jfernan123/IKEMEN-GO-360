#pragma once

// The debug viewers (Tab / LB+Start): a character's animations with their boxes, and its sprites.

#include <cstdint>
#include <string>
#include <vector>

#include "app/shared.h"
#include "core/fight/character.h"
#include "core/formats/air.h"
#include "core/formats/sff.h"
#include "core/render/debug_text.h"
#include "core/render/sprite_renderer.h"
#include "core/util/log.h"

namespace m360 {

class AnimationViewer {
public:
	AnimationViewer(const AnimationTable &table, SpriteRenderer &renderer) : table_(table), renderer_(renderer)
	{
		for (const auto &[no, anim] : table.all())
			actions_.push_back(no);
		// Start on action 0, standing.
		for (size_t i = 0; i < actions_.size(); i++) {
			if (actions_[i] == 0)
				current_ = i;
		}
		if (!actions_.empty())
			load();
	}

	void update(Buttons &b, const platform::Pad &pad)
	{
		updatePalette(b, pad, renderer_);
		if (actions_.empty())
			return;
		const size_t n = actions_.size();
		const size_t was = current_;
		if (b.right.update(pad.right))
			current_ = (current_ + 1) % n;
		if (b.left.update(pad.left))
			current_ = (current_ + n - 1) % n;
		if (b.rb.update(pad.rb))
			current_ = (current_ + 10) % n;
		if (b.lb.update(pad.lb))
			current_ = (current_ + n - 10 % n) % n;
		if (current_ != was)
			load();
		if (b.a.update(pad.a))
			paused_ = !paused_;
		if (b.x.update(pad.x))
			facing_ = -facing_;
		if (b.y.update(pad.y))
			showClsn_ = !showClsn_;
		const bool step = b.b.update(pad.b);
		if (!paused_ || step)
			anim_.action();
	}

	void draw()
	{
		drawGround();
		if (actions_.empty()) {
			drawText("NO ACTIONS LOADED", 40, 40, 4, ERROR_TEXT);
			return;
		}
		renderer_.drawAnimation(anim_, GROUND_X, GROUND_Y, SCALE, facing_);
		const AnimFrame *f = anim_.drawFrame();
		if (showClsn_ && f) {
			for (const Clsn &box : f->clsn2)
				drawClsn(box, facing_, 0x4080FF);
			for (const Clsn &box : f->clsn1)
				drawClsn(box, facing_, 0xFF4040);
		}

		drawTextf(40, 30, 4, TEXT, "ACTION %d%s", actions_[current_], paused_ ? "   (PAUSED)" : "");
		drawTextf(40, 75, 3, DIM, "%zu / %zu    ELEMENT %d / %zu    TIME %d / %d    ANIMTIME %d", current_ + 1,
			actions_.size(), anim_.curElem() + 1, anim_.frames.size(), anim_.curElemTime(), f ? f->time : 0,
			anim_.animTime());
		if (f) {
			drawTextf(40, 105, 2, DIM, "SPRITE %d,%d   OFFSET %d,%d   FLIP %s%s   TRANS %s   LOOPSTART %d   LOOPS %d",
				anim_.spriteGroup(), anim_.spriteNumber(), f->xoffset, f->yoffset, f->hflip < 0 ? "H" : "",
				f->vflip < 0 ? "V" : (f->hflip < 0 ? "" : "-"), transName(f->trans), anim_.loopstart + 1,
				anim_.loopCount());
			drawTextf(40, 125, 2, DIM, "BOXES: %zu ATTACK (RED), %zu HURT (BLUE)%s", f->clsn1.size(), f->clsn2.size(),
				showClsn_ ? "" : " - HIDDEN");
		}
		drawText("LEFT/RIGHT: ACTION  A: PAUSE  B: STEP  X: TURN  Y: BOXES  UP/DOWN: PALETTE  TAB/LB+START: NEXT", 40, 680,
			2, DIM);
	}

private:
	void load() { table_.get(actions_[current_], anim_); }

	const AnimationTable &table_;
	SpriteRenderer &renderer_;
	std::vector<int32_t> actions_;
	size_t current_ = 0;
	Animation anim_;
	int facing_ = 1;
	bool paused_ = false;
	bool showClsn_ = true;
};

class SpriteViewer {
public:
	SpriteViewer(const Sff &sff, SpriteRenderer &renderer) : sff_(sff), renderer_(renderer)
	{
		// Start on 0,0, a character's standing pose, wherever it sits in the file.
		if (const Sprite *stand = sff.get(0, 0))
			current_ = (size_t)(stand - sff.sprites().data());
	}

	void update(Buttons &b, const platform::Pad &pad)
	{
		updatePalette(b, pad, renderer_);
		const size_t n = sff_.sprites().size();
		if (!n)
			return;
		if (b.right.update(pad.right))
			current_ = (current_ + 1) % n;
		if (b.left.update(pad.left))
			current_ = (current_ + n - 1) % n;
		if (b.rb.update(pad.rb))
			current_ = (current_ + 10) % n;
		if (b.lb.update(pad.lb))
			current_ = (current_ + n - 10 % n) % n;
	}

	void draw()
	{
		drawGround();
		const size_t n = sff_.sprites().size();
		if (!n)
			return;
		const Sprite &s = sff_.sprites()[current_];
		// Sprites whose axis would put them off screen (portraits, effects) are centred instead.
		float x = GROUND_X, y = GROUND_Y;
		const float w = s.width * SCALE, h = s.height * SCALE;
		const bool fits = x - s.axisX * SCALE >= 0 && y - s.axisY * SCALE >= 150 &&
			x - s.axisX * SCALE + w <= platform::SCREEN_WIDTH && y - s.axisY * SCALE + h <= 660;
		if (!fits) {
			x = (platform::SCREEN_WIDTH - w) / 2 + s.axisX * SCALE;
			y = 150 + (510 - h) / 2 + s.axisY * SCALE;
		}
		SpriteDraw how;
		how.scaleX = how.scaleY = SCALE;
		renderer_.drawSprite(s.group, s.number, x, y, how);

		const int palette = renderer_.palette() >= 0 ? renderer_.palette() : s.palette;
		drawTextf(40, 30, 4, TEXT, "SPRITE %u,%u", s.group, s.number);
		drawTextf(40, 75, 3, DIM, "%zu / %zu    %u X %u    AXIS %d,%d    PALETTE %d%s", current_ + 1, n, s.width,
			s.height, s.axisX, s.axisY, palette, renderer_.palette() < 0 ? " (OWN)" : "");
		if (!fits)
			drawText("(CENTRED: ITS AXIS PUTS IT OFF SCREEN)", 40, 110, 2, DIM);
		drawText("LEFT/RIGHT: SPRITE   LB/RB: 10 AT A TIME   UP/DOWN: PALETTE   TAB/LB+START: NEXT", 40, 680, 2, DIM);
	}

private:
	const Sff &sff_;
	SpriteRenderer &renderer_;
	size_t current_ = 0;
};

// The debug viewers and the character they show.
struct Viewers {
	Character kfm;
	std::string error;
	bool loaded;
	SpriteRenderer renderer;
	AnimationViewer animations;
	SpriteViewer sprites;

	explicit Viewers(const std::string &def)
		: loaded(kfm.load(def, 1, error)), renderer(kfm.sff()), animations(kfm.anims(), renderer),
		  sprites(kfm.sff(), renderer)
	{
		if (!loaded)
			logf("mugen360: %s", error.c_str());
	}
};

} // namespace m360
