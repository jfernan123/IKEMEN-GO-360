#pragma once

#include <algorithm>

#include "platform/platform.h"

namespace m360 {

// How stage units map to the screen this frame. Stage x is 0 at the stage's centre; stage y is 0 at the top of the
// stage's localcoord space (240 tall for a 320x240 stage) when the camera is at rest.
struct View {
	float camX = 0, camY = 0;
	// Screen pixels per stage unit.
	float scale = 4;
	// The stage y shown at the top of the screen with the camera at rest: a stage wider than the screen is scaled
	// to fill its height... and one cut, the stage's cuthigh / cutlow say how much from each end.
	float topCut = 0;
	// Where on the screen the game is drawn: all of it, or (a 4:3 stage on a 16:9 screen, as Ikemen keeps the stage's
	// shape) a centred area with black bars at the sides.
	float originX = 0, width = (float)platform::SCREEN_WIDTH;
	// Half the play area's width, in stage units.
	float halfWidth() const { return width / 2.0f / scale; }

	// Where a position relative to the screen's centre column (x) and the stage's top (y) lands on screen.
	float screenX(float x) const { return originX + width / 2.0f + x * scale; }
	float screenY(float y) const { return (y - topCut) * scale; }
};

// MUGEN's [Camera] group, with Ikemen-GO's defaults (src/camera.go, newStageCamera).
struct StageCamera {
	int32_t startx = 0, starty = 0;
	int32_t boundleft = -95, boundright = 95, boundhigh = -25, boundlow = 0;
	float verticalfollow = 0.2f;
	int32_t floortension = 0;
	int32_t tension = 50;
	int32_t cuthigh = 0, cutlow = 0;
};

// Follows the players as MUGEN's camera does, without zoom: x keeps both inside the tension margins within the
// stage's bounds, y rises toward the highest player by verticalfollow. Simplified from Ikemen-GO's Camera.action.
class Camera {
public:
	// scale: game units per stage unit (Ikemen's stage localscl), which the [Camera] values are in.
	void reset(const StageCamera &cam, float scale = 1)
	{
		cam_ = cam;
		scale_ = scale;
		x = (float)cam.startx * scale;
		y = (float)cam.starty * scale;
	}

	// leftest / rightest: the players' outermost x; highest: the smallest (highest) y, 0 on the ground. edge: how far
	// from the camera a player may stand (half the screen less the stage's screenleft / screenright); the camera never
	// moves so far that either player would be pushed past it.
	void update(float leftest, float rightest, float highest, float halfWidth, float edge)
	{
		// The camera positions that keep both players at least tension from the screen's sides. Moving only as far as
		// needed to get into that range keeps the camera still while they're well inside it; when they're too far
		// apart for any position to do it, it centres between them rather than chasing one side then the other.
		const float tension = std::clamp((float)cam_.tension * scale_, 0.0f, halfWidth);
		const float lowest = rightest - halfWidth + tension;
		const float highest_x = leftest + halfWidth - tension;
		x = lowest <= highest_x ? std::clamp(x, lowest, highest_x) : (leftest + rightest) / 2;
		x = std::clamp(x, (float)cam_.boundleft * scale_, (float)cam_.boundright * scale_);
		// Keep both players within reach of the screen's edges: the one behind holds the camera back.
		if (rightest - leftest <= 2 * edge)
			x = std::clamp(x, rightest - edge, leftest + edge);
		const float targetY = std::min(0.0f, highest + (float)cam_.floortension * scale_) * cam_.verticalfollow;
		y = std::clamp(targetY, (float)cam_.boundhigh * scale_, (float)cam_.boundlow * scale_);
	}

	float x = 0, y = 0;

private:
	StageCamera cam_;
	float scale_ = 1;
};

} // namespace m360
