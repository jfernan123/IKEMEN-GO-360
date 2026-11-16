#pragma once

#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/camera.h"
#include "core/layout.h"
#include "core/sff.h"
#include "core/sprite_renderer.h"

namespace m360 {

// Shared fight effects from fightfx.sff / fightfx.air. Hit sparks are Explods in Ikemen, but keeping the shared
// animation player here gives the later Explod and Projectile work one owner and avoids loading fightfx per player.
class FightEffects {
public:
	FightEffects();
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
		// Who made it (for RemoveExplod, ModifyExplod, NumExplod), and its Explod id (-1 none).
		const void *ownerKey = nullptr;
		int32_t id = -1;
		// Kept at (bindX, bindY) from bindKey's position for bindTime ticks (-1 for good).
		const void *bindKey = nullptr;
		int32_t bindTime = 0;
		float bindX = 0, bindY = 0;
		bool removeOnGetHit = false;
		int vfacing = 1;
		Trans trans = Trans::None;
		uint8_t srcAlpha = 255, dstAlpha = 0;
		// How long it keeps moving into a SuperPause / Pause (0: frozen at once, -1: always moves).
		int32_t superMoveTime = 0, pauseMoveTime = 0;
		// ownpal = 0 (MUGEN's default): it takes its owner's PalFX, flashing with it (Ikemen shares the owner's).
		bool ownPal = false;
		// shadow = r, g, b: a shadow on the ground in that colour (-1: the stage's); 0, 0, 0 none.
		int32_t shadow[3] = { 0, 0, 0 };
	};

	// The stage's shadow, for Explods with one.
	struct GroundShadow {
		int intensity = 0;
		float yscale = 1, offsetX = 0, offsetY = 0;
		uint32_t colour = 0;
	};

	// An Explod's settings that ModifyExplod can change.
	struct ExplodChange {
		bool hasScale = false, hasVelocity = false, hasAccel = false, hasRemoveTime = false, hasPriority = false,
			 hasBindTime = false, hasTrans = false, hasPos = false;
		float scaleX = 1, scaleY = 1, velocityX = 0, velocityY = 0, accelX = 0, accelY = 0, bindX = 0, bindY = 0;
		int32_t removeTime = -2, priority = 0, bindTime = 0;
		Trans trans = Trans::None;
		uint8_t srcAlpha = 255, dstAlpha = 0;
	};

	bool load(const std::string &sffPath, const std::string &airPath, std::string &error, bool simulationOnly = false);
	void clear();
	void spawnSpark(int32_t animNo, float x, float y, int facing);
	bool spawnExplod(const Spawn &spawn);
	bool getSharedAnimation(int32_t animNo, Animation &out) const;
	void drawSharedAnimation(const Animation &anim, float x, float y, float scale, int facing, SpriteDraw base = {});
	int32_t countExplods(int owner) const;
	// NumExplod: a character's Explods, of one id or (id < 0) all.
	int32_t countExplods(const void *ownerKey, int32_t id) const;
	// RemoveExplod, ModifyExplod, ExplodBindTime: a character's Explods of an id, or all of them (id < 0).
	void removeExplods(const void *ownerKey, int32_t id);
	void modifyExplods(const void *ownerKey, int32_t id, const ExplodChange &change);
	void setExplodBindTime(const void *ownerKey, int32_t id, int32_t time);
	// Each tick, before the effects move: Explods bound to a character follow it.
	void followBinder(const void *bindKey, float x, float y);
	// The character was hit: its removeongethit Explods go.
	void ownerHit(const void *ownerKey);
	// A character's PalFX this tick, for its Explods without ownpal.
	void setOwnerColour(const void *ownerKey, const SpriteDraw &colour);
	// EnvColor: the screen filled with a colour for time ticks, in place of the stage, the lifebars' lower layers and,
	// unless under is set, the characters.
	void startEnvColor(int32_t r, int32_t g, int32_t b, int32_t time, bool under);
	// Counts down before the characters run (Ikemen's System.action), so time = 1 lasts the frame that set it.
	void tickEnvColor();
	bool envColorOn() const { return envColorTime_ != 0; }
	bool envColorUnder() const { return envColorUnder_; }
	void drawEnvColor() const;
	void startShake(int32_t time, float frequency = 60, float amplitude = -4, float phase = NAN,
		float multiplier = 1, float direction = 0, float directionAdd = 0, float decay = 1);
	float shakeX() const { return shakeOffsetX_; }
	float shakeY() const { return shakeOffsetY_; }
	void startPause(int owner, int32_t time, int32_t moveTime, bool pauseBackground, bool superPause,
		float brightness = 1);
	bool paused() const { return pauseLeft_ > 0; }
	bool pausedFor(int owner) const { return pauseLeft_ > 0 && (owner != pauseOwner_ || pauseMoveLeft_ <= 0); }
	bool backgroundPaused() const { return pauseLeft_ > 0 && pauseBackground_; }
	bool superPaused() const { return pauseLeft_ > 0 && superPause_; }
	float superBrightness() const { return superPaused() && superDarkenDelay_ == 0 ? superBrightness_ : 1; }
	void setAllPalFX(const PalFX &effect) { allPalFX_ = effect; }
	void setBgPalFX(const PalFX &effect) { bgPalFX_ = effect; }
	const PalFX *stagePalFX() const { return allPalFX_.enabled ? &allPalFX_ : bgPalFX_.enabled ? &bgPalFX_ : nullptr; }
	const PalFX &allPalFX() const { return allPalFX_; }
	const PalFX &bgPalFX() const { return bgPalFX_; }
	void tickPause();
	void tick();
	// Pass -2 is layerno -1 (under the stage's layer 0), -1 layer 0's negative priorities (below players, over the fight
	// screen's layer 0), 0 the rest above players but below foreground stage layers, 1 layerno 1 (above those).
	// Pass 0 is layer 0; from and to pick a range of sprpriority, so the fight can draw those Explods
	// between the fighters by sprpriority (Ikemen's DrawList sorts both together).
	void draw(const View &view, float groundY, int pass, int32_t from = INT32_MIN, int32_t to = INT32_MAX);
	// The shadows of Explods that have one, on the ground under them, before the players draw.
	void drawShadows(const View &view, float groundY, const GroundShadow &ground);

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
		const void *ownerKey = nullptr, *bindKey = nullptr;
		int32_t id = -1, bindTime = 0;
		float bindX = 0, bindY = 0;
		bool removeOnGetHit = false;
		int vfacing = 1;
		Trans trans = Trans::None;
		uint8_t srcAlpha = 255, dstAlpha = 0;
		// Hit sparks always move; Explods freeze in pauses (see frozen).
		int32_t superMoveTime = -1, pauseMoveTime = -1;
		bool ownPal = true;
		int32_t shadow[3] = { 0, 0, 0 };
	};
	// Each character's PalFX colours this tick (setOwnerColour).
	std::vector<std::pair<const void *, SpriteDraw>> ownerColours_;
	// Whether a pause holds this effect still: no time, animation, movement or binding (Ikemen's pauseStatus).
	bool frozen(const Effect &effect) const;
	uint32_t envColor_ = 0;
	int32_t envColorTime_ = 0;
	bool envColorUnder_ = false;

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
	float superBrightness_ = 1;
	int32_t superDarkenDelay_ = 0;
	PalFX allPalFX_, bgPalFX_;
	std::vector<const Effect *> drawOrder_;
};

} // namespace m360
