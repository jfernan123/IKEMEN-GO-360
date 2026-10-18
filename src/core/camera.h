#pragma once

#include <algorithm>
#include <cstdint>

#include "platform/platform.h"

namespace m360 {

// How game units map to the screen this frame. x is 0 at the stage's centre; y is 0 at the top of the stage's space
// when the camera is at rest. The fight's camera zooms about the screen's centre column and the ground line (Ikemen's
// GroundLevel): zoomX / zoomY are its scale (a background element's own, which its delta softens), ground the line's
// y. Screenpack views leave them at 1 and 0.
struct View {
	float camX = 0, camY = 0;
	// Screen pixels per game unit, before zoom.
	float scale = 4;
	float zoomX = 1, zoomY = 1, ground = 0;
	// The stage y shown at the top of the screen with the camera at rest: a stage wider than the screen is scaled
	// to fill its height... and one cut, the stage's cuthigh / cutlow say how much from each end.
	float topCut = 0;
	// Where on the screen the game is drawn: all of it, or (a 4:3 stage on a 16:9 screen, as Ikemen keeps the stage's
	// shape) a centred area with black bars at the sides.
	float originX = 0, width = (float)platform::SCREEN_WIDTH;
	// Half the play area's width, in game units at this zoom.
	float halfWidth() const { return width / 2.0f / scale / zoomX; }
	// Screen pixels per game unit at this zoom: a sprite's draw scale.
	float drawScaleX() const { return scale * zoomX; }
	float drawScaleY() const { return scale * zoomY; }

	// Where a position relative to the screen's centre column (x) and the stage's top (y, with the camera's height
	// already taken off) lands on screen.
	float screenX(float x) const { return originX + width / 2.0f + x * scale * zoomX; }
	float screenY(float y) const { return (ground + (y - ground) * zoomY - topCut) * scale; }
	// The same view without the zoom: screen-space effects.
	View unzoomed() const
	{
		View v = *this;
		v.zoomX = v.zoomY = 1;
		return v;
	}
};

// MUGEN's [Camera] group, with Ikemen-GO's defaults (src/camera.go, newStageCamera, and stage.go's reading).
struct StageCamera {
	int32_t startx = 0, starty = 0;
	int32_t boundleft = -95, boundright = 95, boundhigh = -25, boundlow = 0;
	float verticalfollow = 0.2f;
	int32_t floortension = 0;
	int32_t tension = 50;
	int32_t cuthigh = 0, cutlow = 0;
	// Ikemen's MUGEN 1.1 zoom and vertical tension: "tensionlow" turns the vertical tension on.
	int32_t tensionhigh = 0, tensionlow = 0, overdrawhigh = 0, overdrawlow = 0;
	bool ytensionenable = false;
	float tensionvel = 1, startzoom = 1, zoomin = 1, zoomout = 1;
	float zoomindelay = 0, zoominspeed = 1, zoomoutspeed = 1, yscrollspeed = 1;
	float boundhighzoomdelta = 0, verticalfollowzoomdelta = 0;
	bool autocenter = false, lowestcap = false;
};

// The things the camera needs from the stage and the fight's game space, all in game units except localHeight. The
// caller sets every field: default values here made GCC build them with an instruction Xenia skips (see xenia_scan).
struct CameraSpace {
	// Game units per stage unit (Ikemen's stage localscl), which the [Camera] values are in.
	float localScale;
	float gameWidth, gameHeight;
	// The stage's [Bound] screenleft / screenright and [StageInfo] zoffset, in game units.
	float screenLeft, screenRight, zoffset;
	// The stage's localcoord height, in stage units.
	int32_t localHeight;
	// Screen pixels per game unit (Ikemen's heightScale).
	float heightScale;
};

// Ikemen-GO's fighting camera (src/camera.go: Reset, Init, ResetTracking, SaveRestoreTracking, Update, action and
// its helpers): eased toward keeping both players inside the tension margins, zooming between zoomout and zoomin on
// stages that allow it, following the highest player by verticalfollow (or the vertical tension). Without the Z axis,
// "autozoom", or the follow / free views.
class Camera {
public:
	// Ikemen's Init (with Reset): at the start position and zoom, snapping into place on its first move.
	void reset(const StageCamera &cam, const CameraSpace &space);
	// Before the characters run (Ikemen's ResetTracking).
	void resetTracking();
	// One character's place for the camera (Ikemen's Char.track): its left and right edges and y in game units, and its
	// x velocity in game units (0 when it can't move this tick).
	void track(float left, float right, float velocity, float y);
	// After the characters (Ikemen's camera action, then Update).
	void action();

	// Ikemen's ZoomActive and a stage that zooms.
	bool zoomEnabled() const { return zoomin_ != 1 || zoomout_ != 1; }
	// The players' limits (Ikemen's xmin / xmax): the screen's edges less the stage's screenleft / screenright.
	float xmin() const;
	float xmax() const;

	// The camera's centre column, its height (game units, negative up) and its zoom.
	float x = 0, y = 0, scale = 1;

private:
	float groundLevel() const { return zoff_ - aspectCorrection_; }
	void reduceZoomSpeed(float &newLeft, float &newRight, float &newScale, float oldLeft, float oldRight,
		float oldScale) const;
	void keepScreenEdge(float &left, float &right) const;
	void keepStageEdge(float &left, float &right) const;
	void hardLimit(float &left, float &right, float &scale) const;
	float boundY(float y, float scale) const;

	StageCamera cam_;
	CameraSpace space_;
	// In game units: the bounds, the tension margins and the camera's reach.
	float boundL_ = 0, boundR_ = 0, boundH_ = 0, boundLo_ = 0, halfWidth_ = 160;
	float minLeft_ = 0, maxRight_ = 0, minScale_ = 1, zoomin_ = 1, zoomout_ = 1;
	float zoff_ = 0, aspectCorrection_ = 0;
	// Ikemen's Pos[1]: the height times the zoom.
	float posY_ = 0;
	float ywithoutbound_ = 0, zoomindelaytime_ = 0;
	bool skipSmoothing_ = true;
	// What the characters gave this tick, and the last good values.
	float leftest_ = 0, rightest_ = 0, highest_ = 0, lowest_ = 0;
	float prevLeftest_ = 0, prevRightest_ = 0, prevHighest_ = 0, prevLowest_ = 0;
	float leftestVel_ = 0, rightestVel_ = 0;
};

} // namespace m360
