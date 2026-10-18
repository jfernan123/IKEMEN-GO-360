#include "core/effects.h"

#include <algorithm>
#include <cmath>

namespace m360 {

FightEffects::FightEffects()
{
	clear();
}

bool FightEffects::load(const std::string &sffPath, const std::string &airPath, std::string &error)
{
	if (!sff_.load(sffPath, error) || !anims_.load(airPath, error))
		return false;
	renderer_ = std::make_unique<SpriteRenderer>(sff_);
	return true;
}

void FightEffects::clear()
{
	effects_.clear();
	shakeTime_ = shakeLeft_ = 0;
	shakeOffsetX_ = shakeOffsetY_ = 0;
	envColorTime_ = 0;
	pauseOwner_ = -1;
	pauseLeft_ = pauseMoveLeft_ = 0;
	superPause_ = false;
	superBrightness_ = 1;
	superDarkenDelay_ = 0;
	allPalFX_ = PalFX{};
	allPalFX_.time = allPalFX_.startTime = 0;
	bgPalFX_ = PalFX{};
	bgPalFX_.time = bgPalFX_.startTime = 0;
}

void FightEffects::startPause(int owner, int32_t time, int32_t moveTime, bool pauseBackground, bool superPause,
	float brightness)
{
	if (time <= 0)
		return;
	pauseOwner_ = owner;
	pauseLeft_ = time;
	pauseMoveLeft_ = std::max(0, moveTime);
	pauseBackground_ = pauseBackground;
	superPause_ = superPause;
	superBrightness_ = superPause ? std::clamp(brightness, 0.0f, 1.0f) : 1.0f;
	// MUGEN compatibility: the SuperPause begins now, but its background darkening begins on the next frame.
	superDarkenDelay_ = superPause ? 2 : 0;
}

void FightEffects::tickPause()
{
	if (pauseLeft_ > 0)
		pauseLeft_--;
	if (pauseMoveLeft_ > 0)
		pauseMoveLeft_--;
	if (superDarkenDelay_ > 0)
		superDarkenDelay_--;
	if (pauseLeft_ <= 0) {
		pauseOwner_ = -1;
		superPause_ = false;
		superBrightness_ = 1;
		superDarkenDelay_ = 0;
	}
}

void FightEffects::startShake(int32_t time, float frequency, float amplitude, float phase, float multiplier,
	float direction, float directionAdd, float decay)
{
	if (time <= 0)
		return;
	shakeTime_ = shakeLeft_ = time;
	shakeFrequency_ = std::max(0.0f, frequency);
	shakeAmplitude_ = amplitude;
	shakePhase_ = std::isnan(phase) ? (shakeFrequency_ >= 90 ? 90.0f : 0.0f) : std::clamp(phase, -180.0f, 180.0f);
	shakeMultiplier_ = multiplier;
	shakeDirection_ = direction;
	shakeDirectionAdd_ = directionAdd;
	shakeDecay_ = decay;
}

void FightEffects::spawnSpark(int32_t animNo, float x, float y, int facing)
{
	Effect effect;
	if (animNo < 0 || !anims_.get(animNo, effect.anim))
		return;
	effect.renderer = renderer_.get();
	effect.x = x;
	effect.y = y;
	effect.facing = facing < 0 ? -1 : 1;
	effect.moveFacing = effect.facing;
	effect.layer = 1;
	effects_.push_back(effect);
}

bool FightEffects::spawnExplod(const Spawn &spawn)
{
	Effect effect;
	const AnimationTable *animations = spawn.shared ? &anims_ : spawn.animations;
	effect.renderer = spawn.shared ? renderer_.get() : spawn.renderer;
	if (!animations || !effect.renderer || spawn.animNo < 0 || !animations->get(spawn.animNo, effect.anim))
		return false;
	effect.x = spawn.x;
	effect.y = spawn.y;
	effect.velocityX = spawn.velocityX;
	effect.velocityY = spawn.velocityY;
	effect.accelX = spawn.accelX;
	effect.accelY = spawn.accelY;
	effect.scaleX = spawn.scaleX;
	effect.scaleY = spawn.scaleY;
	effect.facing = spawn.facing < 0 ? -1 : 1;
	effect.moveFacing = spawn.moveFacing < 0 ? -1 : 1;
	effect.removeTime = spawn.removeTime;
	effect.priority = spawn.priority;
	effect.owner = spawn.owner;
	effect.layer = spawn.layer;
	effect.screen = spawn.screen;
	effect.explod = true;
	effect.ownerKey = spawn.ownerKey;
	effect.id = spawn.id;
	effect.bindKey = spawn.bindKey;
	effect.bindTime = spawn.bindTime;
	effect.bindX = spawn.bindX;
	effect.bindY = spawn.bindY;
	effect.removeOnGetHit = spawn.removeOnGetHit;
	effect.vfacing = spawn.vfacing < 0 ? -1 : 1;
	effect.trans = spawn.trans;
	effect.srcAlpha = spawn.srcAlpha;
	effect.dstAlpha = spawn.dstAlpha;
	effects_.push_back(effect);
	return true;
}

int32_t FightEffects::countExplods(const void *ownerKey, int32_t id) const
{
	return (int32_t)std::count_if(effects_.begin(), effects_.end(), [&](const Effect &effect) {
		return effect.explod && effect.ownerKey == ownerKey && (id < 0 || effect.id == id);
	});
}

void FightEffects::removeExplods(const void *ownerKey, int32_t id)
{
	effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
		[&](const Effect &effect) {
			return effect.explod && effect.ownerKey == ownerKey && (id < 0 || effect.id == id);
		}), effects_.end());
}

void FightEffects::modifyExplods(const void *ownerKey, int32_t id, const ExplodChange &change)
{
	for (Effect &e : effects_) {
		if (!e.explod || e.ownerKey != ownerKey || (id >= 0 && e.id != id))
			continue;
		if (change.hasScale) {
			e.scaleX = change.scaleX;
			e.scaleY = change.scaleY;
		}
		if (change.hasVelocity) {
			e.velocityX = change.velocityX;
			e.velocityY = change.velocityY;
		}
		if (change.hasAccel) {
			e.accelX = change.accelX;
			e.accelY = change.accelY;
		}
		if (change.hasRemoveTime)
			e.removeTime = change.removeTime;
		if (change.hasPriority)
			e.priority = change.priority;
		if (change.hasBindTime)
			e.bindTime = change.bindTime;
		if (change.hasPos) {
			e.bindX = change.bindX;
			e.bindY = change.bindY;
		}
		if (change.hasTrans) {
			e.trans = change.trans;
			e.srcAlpha = change.srcAlpha;
			e.dstAlpha = change.dstAlpha;
		}
	}
}

void FightEffects::setExplodBindTime(const void *ownerKey, int32_t id, int32_t time)
{
	for (Effect &e : effects_) {
		if (e.explod && e.ownerKey == ownerKey && (id < 0 || e.id == id))
			e.bindTime = time;
	}
}

void FightEffects::followBinder(const void *bindKey, float x, float y)
{
	for (Effect &e : effects_) {
		if (!e.explod || e.bindKey != bindKey || e.bindTime == 0)
			continue;
		e.x = x + e.bindX;
		e.y = y + e.bindY;
		if (e.bindTime > 0)
			e.bindTime--;
	}
}

void FightEffects::ownerHit(const void *ownerKey)
{
	effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
		[&](const Effect &effect) { return effect.explod && effect.removeOnGetHit && effect.ownerKey == ownerKey; }),
		effects_.end());
}

void FightEffects::startEnvColor(int32_t r, int32_t g, int32_t b, int32_t time, bool under)
{
	envColor_ = 0xFF000000u | (uint32_t)std::clamp(r, 0, 255) << 16 | (uint32_t)std::clamp(g, 0, 255) << 8 |
		(uint32_t)std::clamp(b, 0, 255);
	envColorTime_ = time;
	envColorUnder_ = under;
}

void FightEffects::drawEnvColor(bool under) const
{
	if (envColorTime_ != 0 && envColorUnder_ == under)
		platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, envColor_);
}

bool FightEffects::getSharedAnimation(int32_t animNo, Animation &out) const
{
	return anims_.get(animNo, out);
}

void FightEffects::drawSharedAnimation(const Animation &anim, float x, float y, float scale, int facing, SpriteDraw base)
{
	if (renderer_)
		renderer_->drawAnimation(anim, x, y, scale, facing, base);
}

int32_t FightEffects::countExplods(int owner) const
{
	return (int32_t)std::count_if(effects_.begin(), effects_.end(),
		[owner](const Effect &effect) { return effect.explod && effect.owner == owner; });
}

void FightEffects::tick()
{
	// Ikemen advances the global palette effects even while a Pause has stopped the stage.
	allPalFX_.step();
	bgPalFX_.step();
	if (shakeLeft_ > 0) {
		constexpr double PI = 3.14159265358979323846;
		const float elapsed = (float)(shakeTime_ - shakeLeft_);
		double factor = 1;
		if (shakeMultiplier_ != 0 && shakeMultiplier_ != 1)
			factor *= std::pow((double)shakeMultiplier_, std::floor(shakeFrequency_ * elapsed / 360.0f));
		const double remaining = (double)shakeLeft_ / shakeTime_;
		if (shakeDecay_ != 0 && remaining > 0)
			factor *= std::pow(remaining, (double)shakeDecay_);
		const double offset = shakeAmplitude_ * factor *
			std::sin((shakePhase_ + shakeFrequency_ * elapsed) * PI / 180.0);
		const double direction = (shakeDirection_ + shakeDirectionAdd_ * elapsed) * PI / 180.0;
		shakeOffsetX_ = (float)(offset * std::sin(-direction));
		shakeOffsetY_ = (float)(offset * std::cos(-direction));
		shakeLeft_--;
	} else {
		shakeOffsetX_ = shakeOffsetY_ = 0;
	}
	// EnvColor counts down (-1 lasts until replaced).
	if (envColorTime_ > 0)
		envColorTime_--;
	// Ikemen checks expiry before advancing an existing Explod. A newly committed one still advances once on its
	// creation tick, so removetime counts visible frames and the final animation element is not discarded.
	effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
		[](const Effect &effect) {
			return !effect.fresh && ((effect.removeTime >= 0 && effect.time >= effect.removeTime) ||
				(effect.removeTime <= -2 && effect.anim.loopEnd()));
		}), effects_.end());
	for (Effect &effect : effects_) {
		effect.fresh = false;
		effect.time++;
		effect.x += effect.velocityX * effect.moveFacing;
		effect.y += effect.velocityY;
		effect.velocityX += effect.accelX;
		effect.velocityY += effect.accelY;
		effect.anim.action();
	}
}

void FightEffects::draw(const View &view, float groundY, int pass)
{
	// Screen-space effects don't zoom with the camera (Ikemen's SprData.draw for "screen" sprites).
	const View flat = view.unzoomed();
	for (const Effect &effect : effects_) {
		const int effectPass = effect.layer < 0 ? -1 : effect.layer > 0 ? 1 : effect.priority < 0 ? -1 : 0;
		if (effectPass != pass || !effect.renderer)
			continue;
		const View &v = effect.screen ? flat : view;
		const float sx = v.screenX(effect.screen ? effect.x : effect.x - v.camX);
		const float sy = v.screenY(groundY + effect.y - (effect.screen ? 0 : v.camY));
		SpriteDraw how;
		how.scaleX = effect.scaleX;
		// vfacing -1 draws it upside down about its position.
		how.scaleY = effect.scaleY * effect.vfacing;
		how.trans = effect.trans;
		how.srcAlpha = effect.srcAlpha;
		how.dstAlpha = effect.dstAlpha;
		allPalFX_.stack(how);
		effect.renderer->drawAnimation(effect.anim, sx, sy, v.drawScaleX(), effect.facing, how);
	}
}

} // namespace m360
