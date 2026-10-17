#pragma once

#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/camera.h"
#include "core/sff.h"
#include "core/sprite_renderer.h"

namespace m360 {

// Shared fight effects from fightfx.sff / fightfx.air. Hit sparks are Explods in Ikemen, but keeping the shared
// animation player here gives the later Explod and Projectile work one owner and avoids loading fightfx per player.
class FightEffects {
public:
	struct Spawn {
		int32_t animNo = -1;
		bool shared = false;
		const AnimationTable *animations = nullptr;
		SpriteRenderer *renderer = nullptr;
		int owner = -1;
		float x = 0, y = 0;
		float velocityX = 0, velocityY = 0;
		float accelX = 0, accelY = 0;
		float scaleX = 1, scaleY = 1;
		int facing = 1, moveFacing = 1;
		int32_t removeTime = -2;
		int32_t priority = 0;
		int layer = 0;
		bool screen = false;
	};

	bool load(const std::string &sffPath, const std::string &airPath, std::string &error);
	void clear();
	void spawnSpark(int32_t animNo, float x, float y, int facing);
	bool spawnExplod(const Spawn &spawn);
	bool getSharedAnimation(int32_t animNo, Animation &out) const;
	void drawSharedAnimation(const Animation &anim, float x, float y, float scale, int facing, SpriteDraw base = {});
	int32_t countExplods(int owner) const;
	void startShake(int32_t time, float frequency = 60, float amplitude = -4, float phase = NAN,
		float multiplier = 1, float direction = 0, float directionAdd = 0, float decay = 1);
	float shakeX() const { return shakeOffsetX_; }
	float shakeY() const { return shakeOffsetY_; }
	void startPause(int owner, int32_t time, int32_t moveTime, bool pauseBackground, bool superPause);
	bool paused() const { return pauseLeft_ > 0; }
	bool pausedFor(int owner) const { return pauseLeft_ > 0 && (owner != pauseOwner_ || pauseMoveLeft_ <= 0); }
	bool backgroundPaused() const { return pauseLeft_ > 0 && pauseBackground_; }
	bool superPaused() const { return pauseLeft_ > 0 && superPause_; }
	void tickPause();
	void tick();
	// Pass -1 is below players, 0 above players but below foreground stage layers, 1 above those layers.
	void draw(const View &view, float groundY, int pass);

private:
	struct Effect {
		Animation anim;
		SpriteRenderer *renderer = nullptr;
		float x = 0, y = 0;
		float velocityX = 0, velocityY = 0;
		float accelX = 0, accelY = 0;
		float scaleX = 1, scaleY = 1;
		int facing = 1, moveFacing = 1;
		int32_t removeTime = -2, time = 0;
		int32_t priority = 0;
		int owner = -1, layer = 0;
		bool screen = false, explod = false, fresh = true;
	};

	Sff sff_;
	AnimationTable anims_;
	std::unique_ptr<SpriteRenderer> renderer_;
	std::vector<Effect> effects_;
	int32_t shakeTime_ = 0, shakeLeft_ = 0;
	float shakeFrequency_ = 60, shakeAmplitude_ = -4, shakePhase_ = 0, shakeMultiplier_ = 1;
	float shakeDirection_ = 0, shakeDirectionAdd_ = 0, shakeDecay_ = 1;
	float shakeOffsetX_ = 0, shakeOffsetY_ = 0;
	int pauseOwner_ = -1;
	int32_t pauseLeft_ = 0, pauseMoveLeft_ = 0;
	bool pauseBackground_ = true, superPause_ = false;
};

} // namespace m360
