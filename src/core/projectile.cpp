#include "core/projectile.h"

namespace m360 {

void Projectile::advance()
{
	if (!active)
		return;
	if (hitPause > 0) {
		hitPause--;
		return;
	}
	if (missLeft > 0)
		missLeft--;
	if (removeTime > 0)
		removeTime--;
	if (!removing) {
		x += vx * facing;
		y += vy;
		vx = (vx + ax) * velmulX;
		vy = (vy + ay) * velmulY;
	}
	anim.action();
}

bool Projectile::outside(float cameraX, float halfWidth) const
{
	const float worldX = x * localScale;
	return worldX < cameraX - halfWidth - edgeBound * localScale ||
		worldX > cameraX + halfWidth + edgeBound * localScale || y < heightLow || y > heightHigh;
}

} // namespace m360
