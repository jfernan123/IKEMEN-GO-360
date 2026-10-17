#include "core/effects.h"

#include <algorithm>
#include <cmath>

namespace m360 {

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
	pauseOwner_ = -1;
	pauseLeft_ = pauseMoveLeft_ = 0;
	superPause_ = false;
}

void FightEffects::startPause(int owner, int32_t time, int32_t moveTime, bool pauseBackground, bool superPause)
{
	if (time <= 0)
		return;
	pauseOwner_ = owner;
	pauseLeft_ = time;
	pauseMoveLeft_ = std::max(0, moveTime);
	pauseBackground_ = pauseBackground;
	superPause_ = superPause;
}

void FightEffects::tickPause()
{
	if (pauseLeft_ > 0)
		pauseLeft_--;
	if (pauseMoveLeft_ > 0)
		pauseMoveLeft_--;
	if (pauseLeft_ <= 0) {
		pauseOwner_ = -1;
		superPause_ = false;
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
	effects_.push_back(effect);
	return true;
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
	for (const Effect &effect : effects_) {
		const int effectPass = effect.layer < 0 ? -1 : effect.layer > 0 ? 1 : effect.priority < 0 ? -1 : 0;
		if (effectPass != pass || !effect.renderer)
			continue;
		const float sx = view.screenX(effect.screen ? effect.x : effect.x - view.camX);
		const float sy = view.screenY(groundY + effect.y - (effect.screen ? 0 : view.camY));
		SpriteDraw how;
		how.scaleX = effect.scaleX;
		how.scaleY = effect.scaleY;
		effect.renderer->drawAnimation(effect.anim, sx, sy, view.scale, effect.facing, how);
	}
}

} // namespace m360
