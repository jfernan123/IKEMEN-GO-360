#pragma once

#include <cstdint>

#include "core/air.h"
#include "core/hitdef.h"

namespace m360 {

// One MUGEN Projectile controller instance. Positions and velocities stay in the owner's local coordinates; drawing
// and collision multiply them by localScale, as Ikemen's Projectile does.
struct Projectile {
	int32_t id = 0;
	Animation anim;
	HitDef hitdef;
	float x = 0, y = 0, vx = 0, vy = 0, ax = 0, ay = 0, velmulX = 1, velmulY = 1;
	float localScale = 1, scaleX = 1, scaleY = 1;
	int facing = 1;
	int32_t animNo = -1, hitAnim = -1, removeAnim = -1, cancelAnim = -1;
	char animPrefix = 0, hitAnimPrefix = 0, removeAnimPrefix = 0, cancelAnimPrefix = 0;
	int32_t removeTime = -1, missTime = 0, missLeft = 0, hits = 1, priority = 1, sprPriority = 3;
	int32_t hitPause = 0;
	float edgeBound = 40, heightLow = -240, heightHigh = 1;
	bool removeOnHit = true, shared = false, active = true, removing = false, contact = false;

	void advance();
	bool outside(float cameraX, float halfWidth) const;
};

} // namespace m360
