#include "core/camera.h"

#include <cmath>
#include <limits>

namespace m360 {

namespace {

constexpr float FAR = std::numeric_limits<float>::max();

} // namespace

// Ikemen's Camera.Reset and Init.
void Camera::reset(const StageCamera &cam, const CameraSpace &space)
{
	cam_ = cam;
	space_ = space;
	const float ls = space.localScale, gw = space.gameWidth, gh = space.gameHeight;
	zoomout_ = cam.zoomout;
	zoomin_ = std::max(cam.zoomin, cam.zoomout);
	// MUGEN 1.1's zoom widens the bounds by how far it zooms out.
	const float widen = ((1 - zoomout_) * 100 * zoomout_) * (1 / zoomout_) * (1 / zoomout_) * 1.6f * (gw / 320);
	boundL_ = (float)(cam.boundleft - cam.startx) * ls - widen;
	boundR_ = (float)(cam.boundright - cam.startx) * ls + widen;
	halfWidth_ = gw / 2;
	zoff_ = space.zoffset;
	// A stage shorter or taller than the game space is moved to fit (Ikemen's aspectcorrection).
	aspectCorrection_ = 0;
	const float stageHeight = (float)space.localHeight * ls;
	int32_t cutlow = cam.cutlow;
	if (stageHeight - gh < 0) {
		aspectCorrection_ = std::min(0.0f, (stageHeight - gh) + std::min((gh - stageHeight) / 2, (float)cam.overdrawlow * ls));
	} else if (stageHeight - gh > 0) {
		const float diff = std::ceil(stageHeight) - gh;
		if (cam.cuthigh + cutlow <= 0) {
			aspectCorrection_ = diff;
		} else {
			const float high = std::ceil((float)cam.cuthigh * ls), low = std::ceil((float)cutlow * ls);
			const float part = high * diff / (high + low);
			aspectCorrection_ = diff - part <= (float)cutlow ? part : diff - low;
		}
	}
	cam_.boundlow = std::max(cam.boundhigh, cam.boundlow);
	boundH_ = (float)cam.boundhigh * ls;
	boundLo_ = (float)cam_.boundlow * ls;
	cam_.tensionvel = std::clamp(cam.tensionvel, 0.0f, 20.0f);
	if (cam.verticalfollow < 0)
		cam_.ytensionenable = true;
	const float xminscl = gw / (gw - boundL_ + boundR_);
	minScale_ = std::max(zoomout_, std::min(zoomin_, xminscl));
	maxRight_ = (float)cam.boundright * ls + halfWidth_ / zoomout_;
	minLeft_ = (float)cam.boundleft * ls - halfWidth_ / zoomout_;

	scale = cam.startzoom;
	x = (float)cam.startx * ls;
	posY_ = (float)cam.starty * ls;
	y = posY_ / scale;
	ywithoutbound_ = posY_;
	zoomindelaytime_ = cam.zoomindelay;
	skipSmoothing_ = true;
	prevLeftest_ = prevRightest_ = x;
	prevHighest_ = prevLowest_ = 0;
	resetTracking();
}

void Camera::resetTracking()
{
	leftest_ = FAR;
	rightest_ = -FAR;
	highest_ = FAR;
	lowest_ = -FAR;
	leftestVel_ = rightestVel_ = 0;
}

void Camera::track(float left, float right, float velocity, float charY)
{
	if (left < leftest_) {
		leftest_ = left;
		leftestVel_ = velocity;
	}
	if (right > rightest_) {
		rightest_ = right;
		rightestVel_ = velocity;
	}
	highest_ = std::min(charY, highest_);
	lowest_ = std::max(charY, lowest_);
}

float Camera::xmin() const
{
	float lo = x - halfWidth_ / scale + space_.screenLeft, hi = x + halfWidth_ / scale - space_.screenRight;
	if (lo > hi)
		lo = (lo + hi) / 2;
	if (std::fabs(minLeft_ - lo) < 0.0001f)
		lo = minLeft_;
	return lo;
}

float Camera::xmax() const
{
	float lo = x - halfWidth_ / scale + space_.screenLeft, hi = x + halfWidth_ / scale - space_.screenRight;
	if (lo > hi)
		hi = (lo + hi) / 2;
	if (std::fabs(maxRight_ - hi) < 0.0001f)
		hi = maxRight_;
	return hi;
}

// Ikemen's Camera.action (the fighting view, without the Z axis), then Update.
void Camera::action()
{
	const float ls = space_.localScale, gw = space_.gameWidth, gh = space_.gameHeight;
	const float hw = halfWidth_;
	const float oldX = x, oldPosY = posY_, oldScale = scale;
	float newY = oldPosY / oldScale;

	// SaveRestoreTracking: no character tracked keeps the last values.
	if (highest_ == FAR) highest_ = prevHighest_; else prevHighest_ = highest_;
	if (lowest_ == -FAR) lowest_ = prevLowest_; else prevLowest_ = lowest_;
	if (leftest_ == FAR) leftest_ = prevLeftest_; else prevLeftest_ = leftest_;
	if (rightest_ == -FAR) rightest_ = prevRightest_; else prevRightest_ = rightest_;

	if (cam_.lowestcap)
		lowest_ = std::max(lowest_, (float)cam_.boundhigh * ls - (gh - groundLevel() - (float)cam_.tensionlow) / zoomout_);

	const float tension = std::max(0.0f, (float)cam_.tension * ls);
	const float oldLeft = oldX - hw / oldScale, oldRight = oldX + hw / oldScale;
	float targetLeft = oldLeft, targetRight = oldRight;
	if (cam_.autocenter) {
		targetLeft = std::min(std::max((leftest_ + rightest_) / 2 - hw / oldScale, minLeft_), maxRight_ - 2 * hw / oldScale);
		targetRight = targetLeft + 2 * hw / oldScale;
	}

	// The edges move when a player is inside a tension margin.
	if (leftest_ < targetLeft + tension) {
		const float diff = targetLeft - std::max(leftest_ - tension, minLeft_);
		targetLeft = std::max(leftest_ - tension, minLeft_);
		targetRight = std::max(oldRight - diff, std::min(rightest_ + tension, maxRight_));
	} else if (rightest_ > targetRight - tension) {
		const float diff = targetRight - std::min(rightest_ + tension, maxRight_);
		targetRight = std::min(rightest_ + tension, maxRight_);
		targetLeft = std::min(oldLeft - diff, std::max(leftest_ - tension, minLeft_));
	}

	const float screenLeft = space_.screenLeft, screenRight = space_.screenRight;
	if (hw * 2 / (targetRight - targetLeft) < zoomout_) {
		// Wider than the furthest zoom: shrink toward the side with room, keeping the players off the screen's edges.
		const float rLeft = std::max(targetLeft + tension - leftest_, 0.0f);
		const float rRight = std::max(rightest_ - (targetRight - tension), 0.0f);
		float diff = 2 * ((targetRight - targetLeft) / 2 - hw / zoomout_);
		if (rLeft > rRight) {
			const float diff2 = rLeft - rRight;
			targetRight -= std::min(diff2, diff);
			diff -= std::min(diff2, diff);
		} else if (rRight > rLeft) {
			const float diff2 = rRight - rLeft;
			targetLeft += std::min(diff2, diff);
			diff -= std::min(diff2, diff);
		}
		targetLeft += diff / 2;
		targetRight -= diff / 2;
		if (leftest_ - targetLeft < screenLeft) {
			float shift = std::min(screenLeft - (leftest_ - targetLeft), targetLeft - minLeft_);
			if (targetRight - rightest_ < screenRight)
				shift -= std::min(screenRight - (targetRight - rightest_), maxRight_ - targetRight);
			targetLeft -= shift;
			targetRight -= shift;
		} else if (targetRight - rightest_ < screenRight) {
			const float shift = std::min(screenRight - (targetRight - rightest_), maxRight_ - targetRight);
			targetLeft += shift;
			targetRight += shift;
		}
	}

	float maxScale = zoomin_;
	if (cam_.ytensionenable) {
		maxScale = std::min(std::max(gh / ((lowest_ + (float)cam_.tensionlow * ls) - (highest_ - (float)cam_.tensionhigh * ls)),
								zoomout_),
			maxScale);
	}

	// Zooming in, after zoomindelay ticks.
	if (hw * 2 / (targetRight - targetLeft) < maxScale) {
		if (zoomindelaytime_ > 0) {
			zoomindelaytime_ -= 1;
		} else {
			float diffLeft = std::max(leftest_ - tension - targetLeft, 0.0f);
			float diffRight = std::min(rightest_ + tension - targetRight, 0.0f);
			if (hw * 2 / ((targetRight + diffRight) - (targetLeft + diffLeft)) > maxScale) {
				const float span = (targetRight + diffRight) - (targetLeft + diffLeft) - hw * 2 / maxScale;
				const float tmp = diffLeft / (diffLeft - diffRight) * span;
				const float tmp2 = diffRight / (diffLeft - diffRight) * span;
				diffLeft += tmp;
				diffRight += tmp2;
			}
			if (hw * 2 / ((targetRight + diffRight) - (targetLeft + diffLeft)) > oldScale) {
				targetLeft += diffLeft;
				targetRight += diffRight;
			} else {
				zoomindelaytime_ = cam_.zoomindelay;
			}
		}
	} else {
		zoomindelaytime_ = cam_.zoomindelay;
	}

	float targetX = (targetLeft + targetRight) / 2;
	float targetScale = std::min(hw * 2 / (targetRight - targetLeft), maxScale);

	if (!cam_.ytensionenable) {
		// Following the highest player by verticalfollow, eased.
		float ywithoutbound = ywithoutbound_;
		const float verticalfollow = std::max(cam_.verticalfollow, 0.0f) +
			(targetScale - zoomout_) * std::max(cam_.verticalfollowzoomdelta, 0.0f);
		const float targetY = (highest_ + (float)cam_.floortension * ls) * verticalfollow;
		if (skipSmoothing_) {
			ywithoutbound = targetY;
			newY = ywithoutbound;
		} else {
			for (int i = 0; i < 3; i++) {
				ywithoutbound = ywithoutbound * .85f + targetY * .15f;
				if (std::fabs(targetY - ywithoutbound) * space_.heightScale < 1)
					ywithoutbound = targetY;
				if (std::fabs(newY - ywithoutbound) < gw / 320 * 5.5f) {
					newY = ywithoutbound;
				} else if (newY > ywithoutbound) {
					newY -= gw / 320 * 0.5f;
					newY -= (newY - ywithoutbound) * verticalfollow / 10;
				} else {
					newY += gw / 320 * 0.5f;
					newY += (ywithoutbound - newY) * verticalfollow / 10;
				}
			}
		}
		ywithoutbound_ = ywithoutbound;
	} else {
		// The vertical tension: zoom to fit the highest and lowest players, the top one tensionhigh from the top.
		targetScale = std::min(
			std::min(std::max(gh / ((lowest_ + (float)cam_.tensionlow * ls) - (highest_ - (float)cam_.tensionhigh * ls)),
						 zoomout_),
				zoomin_),
			targetScale);
		targetX = std::min(std::max(targetX, (float)cam_.boundleft * ls - hw * (1 / zoomout_ - 1 / targetScale)),
			(float)cam_.boundright * ls + hw * (1 / zoomout_ - 1 / targetScale));
		targetLeft = targetX - hw / targetScale;
		targetRight = targetX + hw / targetScale;
		newY = ywithoutbound_;
		const float targetY = groundLevel() / targetScale + (highest_ - (float)cam_.tensionhigh * ls);
		if (skipSmoothing_) {
			newY = targetY;
		} else {
			const float step = gw / 320 * 2.5f;
			for (int i = 0; i < 3; i++) {
				newY = (newY + targetY) * .5f;
				if (std::fabs(targetY - newY) < step) {
					newY = targetY;
					break;
				} else if (targetY - newY > step) {
					newY = newY + step;
				} else {
					newY = newY - step;
				}
			}
		}
		ywithoutbound_ = newY;
	}

	// The edges eased toward their targets, never slower than the players at them move.
	float newLeft = oldLeft, newRight = oldRight;
	const float vel = cam_.tensionvel;
	if (skipSmoothing_) {
		newLeft = targetLeft;
		newRight = targetRight;
	} else {
		const float diff = gw / 3200;
		for (int i = 0; i < 3; i++) {
			newLeft = newLeft + (targetLeft - newLeft) * 0.05f * vel;
			newRight = newRight + (targetRight - newRight) * 0.05f * vel;
			const float diffLeft = targetLeft - newLeft, diffRight = targetRight - newRight;
			if (std::fabs(diffLeft) <= diff * vel)
				newLeft = targetLeft;
			else if (diffLeft > 0)
				newLeft += diff * vel;
			else
				newLeft -= diff * vel;
			if (newLeft - oldLeft > 0 && newLeft - oldLeft < rightestVel_)
				newLeft = std::min(oldLeft + rightestVel_, targetLeft);
			else if (newLeft - oldLeft < 0 && newLeft - oldLeft > leftestVel_)
				newLeft = std::max(oldLeft + leftestVel_, targetLeft);
			if (std::fabs(diffRight) <= diff * vel)
				newRight = targetRight;
			else if (diffRight > 0)
				newRight += diff * vel;
			else
				newRight -= diff * vel;
			if (newRight - oldRight > 0 && newRight - oldRight < rightestVel_)
				newRight = std::min(oldRight + rightestVel_, targetRight);
			else if (newRight - oldRight < 0 && newRight - oldRight > leftestVel_)
				newRight = std::max(oldRight + leftestVel_, targetRight);
		}
	}

	float newScale = std::min(hw * 2 / (newRight - newLeft), zoomin_);
	reduceZoomSpeed(newLeft, newRight, newScale, oldLeft, oldRight, oldScale);
	float newX = (newLeft + newRight) / 2;
	// reduceYScrollSpeed (against the old position, as Ikemen passes it).
	if (std::fabs(newY - oldPosY) >= 5e-5f && cam_.yscrollspeed >= 0 && cam_.yscrollspeed < 1)
		newY = oldPosY + (newY - oldPosY) * cam_.yscrollspeed;
	newY = boundY(newY, newScale);
	skipSmoothing_ = false;

	// System.action: without zoom, x is kept to quarter units; then Update.
	if (!zoomEnabled())
		newX = std::ceil(newX * 4 - 0.5f) / 4;
	scale = newScale;
	x = newX;
	posY_ = newY;
	y = posY_ / scale;
}

void Camera::reduceZoomSpeed(float &newLeft, float &newRight, float &newScale, float oldLeft, float oldRight,
	float oldScale) const
{
	const float speed = newScale > oldScale ? cam_.zoominspeed : cam_.zoomoutspeed;
	if (speed < 0 || speed >= 1)
		return;
	const float scaleDiff = newScale - oldScale;
	const float leftDiff = std::fabs(newLeft - oldLeft), rightDiff = std::fabs(newRight - oldRight);
	if (std::fabs(scaleDiff) < 5e-4f || (leftDiff < 5e-5f && rightDiff < 5e-5f))
		return;
	const float adjustedScale = oldScale + speed * scaleDiff;
	const float width = newRight - newLeft;
	const float widthDiff = width * (newScale / adjustedScale) - width;
	float left = newLeft - widthDiff * leftDiff / (leftDiff + rightDiff);
	float right = newRight + widthDiff * rightDiff / (leftDiff + rightDiff);
	keepScreenEdge(left, right);
	keepStageEdge(left, right);
	hardLimit(left, right, newScale);
	newLeft = left;
	newRight = right;
}

void Camera::keepScreenEdge(float &left, float &right) const
{
	const float screenLeftest = leftest_ - space_.screenLeft;
	if (left > screenLeftest) {
		right += screenLeftest - left;
		left = screenLeftest;
	}
	const float screenRightest = rightest_ + space_.screenRight;
	if (right < screenRightest) {
		left += screenRightest - right;
		right = screenRightest;
	}
}

void Camera::keepStageEdge(float &left, float &right) const
{
	if (left < minLeft_) {
		right += minLeft_ - left;
		left = minLeft_;
	}
	if (right > maxRight_) {
		left += maxRight_ - right;
		right = maxRight_;
	}
}

void Camera::hardLimit(float &left, float &right, float &newScale) const
{
	left = std::max(left, minLeft_);
	right = std::min(right, maxRight_);
	newScale = std::max(std::min(halfWidth_ * 2 / (right - left), zoomin_), zoomout_);
}

float Camera::boundY(float value, float newScale) const
{
	const float ls = space_.localScale;
	if (cam_.boundhighzoomdelta > 0) {
		const float topBound = (float)cam_.boundhigh * ls - groundLevel() / zoomout_;
		const float boundHigh = (float)cam_.boundhigh * ls +
			((topBound + groundLevel() / newScale) - (float)cam_.boundhigh * ls) / cam_.boundhighzoomdelta;
		return std::min(std::max(value, boundHigh), (float)cam_.boundlow * ls) * newScale;
	}
	return std::min(std::max(value, (float)cam_.boundhigh * ls), (float)cam_.boundlow * ls) * newScale;
}

} // namespace m360
