#pragma once

#include <climits>
#include <cmath>
#include <cstdint>

namespace m360 {

// A HitDef's attack and what it does to whoever it hits, and the get-hit values it leaves on them.
// Translated from Ikemen-GO src/char.go (HitDef, reset, finalizeParams; GetHitVar, reset, selectiveReset).

// "Not given" for whole-number parameters whose default depends on others (Ikemen's IErr).
constexpr int32_t NOT_SET = INT32_MIN;
// "Not given" for decimals.
inline float notSetF() { return std::nanf(""); }
inline bool isSet(float f) { return !std::isnan(f); }

// hitflag / guardflag letters.
namespace HitFlag {
constexpr int32_t High = 1, Low = 2, Air = 4, Fall = 8, Down = 16, P = 32, Minus = 64, Plus = 128;
}

// ground.type, air.type: values as GetHitVar returns them.
enum class HitType : int8_t { Unknown = -1, None = 0, High = 1, Low = 2, Trip = 3 };
// animtype: Light .. Diagup, as GetHitVar(animtype).
enum class Reaction : int8_t { Unknown = -1, Light = 0, Medium = 1, Hard = 2, Back = 3, Up = 4, Diagup = 5 };

struct HitDef {
	// attr: state types (S 1, C 2, A 4) and attack bits, as ExprContext::hitDefAttr. Unset or spent: active false.
	bool active = false;
	int32_t attrState = 0, attrAttack = 0;
	int32_t hitflag = HitFlag::High | HitFlag::Low | HitFlag::Air | HitFlag::Fall;
	int32_t guardflag = 0;
	Reaction animtype = Reaction::Light, airAnimtype = Reaction::Unknown, fallAnimtype = Reaction::Unknown;
	int32_t priority = 4;
	char priorityType = 'H'; // Hit, Miss, Dodge
	int32_t hitDamage = 0, guardDamage = 0;
	int32_t pausetime[2] = { 0, 0 };
	int32_t guardPausetime[2] = { NOT_SET, NOT_SET };
	HitType groundType = HitType::High, airType = HitType::Unknown;
	int32_t groundSlidetime = 0, groundHittime = 0, airHittime = 20, downHittime = 20;
	int32_t guardSlidetime = NOT_SET, guardHittime = NOT_SET, guardCtrltime = NOT_SET, airguardCtrltime = NOT_SET;
	float groundVel[2] = { 0, 0 }, airVel[2] = { 0, 0 };
	float guardVel[2] = { notSetF(), 0 }, airguardVel[2] = { notSetF(), notSetF() }, downVel[2] = { notSetF(), notSetF() };
	float xaccel = 0, yaccel = 0.35f;
	int32_t p1stateno = -1, p2stateno = -1;
	bool p2getp1state = false;
	int32_t p1sprpriority = NOT_SET, p2sprpriority = 0;
	int32_t p1facing = 0, p2facing = 0, p1getp2facing = 0;
	int32_t forcestand = NOT_SET, forcecrouch = NOT_SET;
	bool forcenofall = false;
	bool groundFall = false;
	int32_t airFall = NOT_SET;
	float fallXvel = notSetF(), fallYvel = -4.5f;
	bool fallRecover = true, fallKill = true;
	int32_t fallRecovertime = 4, fallDamage = 0;
	int32_t fallEnvShakeTime = 0, fallEnvShakeAmpl = -4;
	float fallEnvShakeFreq = 60, fallEnvShakePhase = notSetF(), fallEnvShakeMul = 1;
	float fallEnvShakeDir = 0, fallEnvShakeDirAdd = 0, fallEnvShakeDecay = 1;
	int32_t envShakeTime = 0, envShakeAmpl = -4;
	float envShakeFreq = 60, envShakePhase = notSetF(), envShakeMul = 1, envShakeDir = 0;
	bool downBounce = false;
	int32_t id = 0, chainid = -1;
	// 1 hits once and is spent, 0 can hit each target once, -1 is spent (or not yet decided: throws default to 1).
	int32_t hitonce = -1;
	int32_t numhits = 1;
	// Power for the attacker (get) and the one hit (give), on hit and on guard.
	int32_t hitGetPower = NOT_SET, guardGetPower = NOT_SET, hitGivePower = NOT_SET, guardGivePower = NOT_SET;
	bool kill = true, guardKill = true;
	float mindist[2] = { notSetF(), notSetF() }, maxdist[2] = { notSetF(), notSetF() };
	// How near an opponent must be to start guarding; stays as it was until a state resets it (hitdefpersist).
	float guardDist[2] = { 0, 0 };
	bool keepstate = false;
	int32_t unhittabletime[2] = { NOT_SET, NOT_SET };
	// Spark and sound resources, with 'F' for the shared fight files and 'S' for the character's own files.
	int32_t sparkno = -1, guardSparkno = -1;
	char sparkPrefix = 'F', guardSparkPrefix = 'F';
	float sparkxy[2] = { 0, 0 };
	int32_t hitsound[2] = { -1, 0 }, guardsound[2] = { -1, 0 };
	char hitsoundPrefix = 'F', guardsoundPrefix = 'F';
	// Whether this HitDef has already hit the opponent (Ikemen's hitdefTargets).
	bool hitOpponent = false;

	// Fills in the parameters whose defaults come from others. mugen10: a MUGEN 1.0 character's (not Ikemen's)
	// guard.hittime defaults to ground.slidetime.
	void finalize(bool mugen10);
	// NT, ST, HT (attack bit class * 3 + 1).
	bool isThrow() const { return (attrAttack & (1 << 1 | 1 << 4 | 1 << 7)) != 0; }
	// HA, HT, HP: a hyper (super) attack.
	bool isHyper() const { return (attrAttack & (1 << 6 | 1 << 7 | 1 << 8)) != 0; }
};

struct GetHitVars {
	int32_t attrState = 0, attrAttack = 0;
	HitType type = HitType::None, groundType = HitType::None, airType = HitType::None;
	Reaction animtype = Reaction::Light, groundAnimtype = Reaction::Light, airAnimtype = Reaction::Light;
	Reaction fallAnimtype = Reaction::Unknown;
	int32_t damage = 0, hitcount = 0, guardcount = 0, fallcount = 0;
	int32_t hitshaketime = 0, hittime = -1, slidetime = 0, ctrltime = 0;
	float xvel = 0, yvel = 0, xaccel = 0, yaccel = 0.35f;
	int32_t hitid = -1;
	float fallXvel = notSetF(), fallYvel = -4.5f;
	int32_t fallTime = 0;
	bool fallRecover = true, fallKill = true;
	int32_t fallRecovertime = 4, fallDamage = 0;
	int32_t fallEnvShakeTime = 0, fallEnvShakeAmpl = -4;
	float fallEnvShakeFreq = 60, fallEnvShakePhase = notSetF(), fallEnvShakeMul = 1;
	float fallEnvShakeDir = 0, fallEnvShakeDirAdd = 0, fallEnvShakeDecay = 1;
	bool fallflag = false, guarded = false, p2getp1state = false, forcestand = false, forcecrouch = false;
	bool kill = true, keepstate = false;
	int32_t downRecovertime = 60;
	int32_t priority = 0;
	float xoff = 0, yoff = 0;

	// Clears them for a new hit, keeping what stacks across a combo (Ikemen's selectiveReset).
	void newHit();
};

} // namespace m360
