#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/coords.h"
#include "core/camera.h"
#include "core/expr.h"
#include "core/hitdef.h"
#include "core/input.h"
#include "core/projectile.h"
#include "core/sff.h"
#include "core/sound.h"
#include "core/sprite_renderer.h"
#include "core/states.h"

namespace m360 {

class FightEffects;

// What a character sees of the fight around it, in stage units, kept up to date by the game each frame.
struct FightWorld {
	float camX = 0, camY = 0;
	float halfWidth = 160;
	// How far from the camera's centre a character may stand (half the screen less the stage's screen margins).
	float screenEdge = 145;
	float leftBound = -1000, rightBound = 1000;
	uint32_t gameTime = 0;
	// The round (see Match): RoundState 0 pre-intro, 1 intro, 2 fight, 3 decided, 4 win poses.
	int32_t roundState = 2, roundNo = 1, roundsExisted = 0;
	int winTeam = -1;          // 0 player 1's side, 1 player 2's, -1 none yet or a draw
	bool matchOver = false;
	bool roundEnded = false;   // past the double KO window
	bool inputWait = false;    // inputs are ignored (time over, waiting for win poses)
	FightEffects *effects = nullptr;
	SoundBank *commonSound = nullptr;
};

// AssertSpecial flags a character can hold for a frame (the ones that do something so far).
namespace Special {
constexpr uint32_t NoWalk = 1, NoAutoTurn = 2, NoStandGuard = 4, NoCrouchGuard = 8, NoAirGuard = 16, Invisible = 32,
				   NoShadow = 64, NoJuggleCheck = 128, Unguardable = 256, NoKo = 512, Intro = 1024,
				   RoundNotOver = 2048, NoBarDisplay = 4096, NoFallHitFlag = 8192, TimerFreeze = 16384,
				   NoCornerPush = 32768;
}

// A MUGEN character, run by its own state files. Loads its .def, SFF, AIR, CMD and CNS (states and constants) and
// runs its states each frame in MUGEN's order. Translated from Ikemen-GO src/char.go (actionPrepare, actionRun,
// changeState, stateChange1/2, posUpdate, autoTurn, tick) and the controllers in src/bytecode.go.
// Positions are in the character's units: x across the stage, y up from the ground being negative; velocity x is
// toward where it faces.
class Character : public ExprContext {
public:
	bool load(const std::string &defPath, int paletteNo, std::string &error);
	// A round begins: at its start position with full life, its fight state cleared, into state 5900 (which goes on
	// to the intro or to standing). A new match also empties the power gauge.
	void startRound(float x, float y, int facing, bool newMatch);
	// Back to the start position (as "Round N" is announced).
	void posReset();
	// "Fight": control, and standing (Ikemen's intro end).
	void roundStartControl();
	// The round's over and it's still standing: into a win, lose or draw state (180, 170, 175), once.
	void startOverState(int32_t no);
	bool overAlive() const { return overAlive_; }
	// Done for the round, ready for the win poses: KO'd and down (5150), or idle on its feet with control.
	bool readyForWinPose() const;
	bool alive() const { return !ko_; }
	bool asserts(uint32_t special) const { return (special_ & special) != 0; }
	int32_t lifeMax() const { return (int32_t)constF("data.life", 1000); }
	int32_t powerMax() const { return (int32_t)constF("data.power", 3000); }
	// 0 for player 1's side, 1 for player 2's (Win, Lose, TeamSide).
	int playerNo = 0;
	// The computer's level (1-8) while it plays this character, 0 for a person (the AILevel trigger).
	int aiLevel = 0;
	void setOpponent(Character *opponent) { p2_ = opponent; }
	void setWorld(const FightWorld *world) { world_ = world; }

	// One frame, in order: turnAndInput, actionPrepare, actionRun (for both players, each phase before the next),
	// then the game pushes players apart and bounds them, then hit detection (beginHitDetection for both, then
	// hitDetection for both), then tick.
	void turnAndInput(const InputState &in);
	void actionPrepare();
	void actionRun();
	// Before anyone's hitDetection: clears this frame's hit flags and leaves the attacker's target list once no
	// longer being hit (Ikemen's actionFinish and collisionDetection).
	void beginHitDetection();
	// Whether this character's HitDef hits the opponent now, and what follows (Ikemen's hitDetectionPlayer).
	void hitDetection();
	void tick();

	void draw(const View &view, float groundY, int shadowIntensity, float shadowYScale);
	// The attack (red) and hurt (blue) boxes the hit detection uses, for the debug view.
	void drawBoxes(const View &view, float groundY) const;

	// The body's left and right edges and its top, for players pushing each other apart.
	void body(float &left, float &right, float &top) const;
	// Whether the stage's screen edges hold this character in (ScreenBound can let it out).
	bool screenBound() const { return screenBound_; }
	bool playerPush() const { return playerPush_; }
	// Whether boxes of a kind (1 attack, 2 hurt) touch the other character's of a kind. Players only push apart where
	// their hurt boxes touch, as in MUGEN.
	bool clsnTouch(const Character &other, int myKind, int theirKind) const;

	std::string name;
	CommandList commands;
	float x = 0, y = 0;
	float vx = 0, vy = 0;
	int facing = 1;
	int32_t stateNo = 0, prevStateNo = 0, stateTime = 0;
	char stateType = 'S', moveType = 'I', physics = 'N';
	bool ctrl = true;
	int32_t animNo = -1;
	Animation anim;
	int32_t life = 1000, power = 0, sprPriority = 0;
	// Stage units per character unit: 1 for localcoord 320, 0.25 for localcoord 1280.
	float localScale = 1;
	float worldX() const { return coords::toWorld(x, localScale); }
	float worldY() const { return coords::toWorld(y, localScale); }
	void setWorldX(float value) { x = coords::toLocal(value, localScale); }
	void addWorldX(float value) { x += coords::toLocal(value, localScale); }
	// When set, every state change is logged with this label ("p1"), for comparing runs frame by frame.
	std::string traceLabel;

	const Sff &sff() const { return sff_; }
	const AnimationTable &anims() const { return anims_; }
	const StateTable &states() const { return states_; }
	const HitDef &hitDef() const { return hitdef_; }
	const GetHitVars &getHitVars() const { return ghv_; }
	// Frozen by a hit: the attacker's pausetime (states, physics and animation stop).
	int32_t hitPauseTime() const { return hitPauseTime_; }
	bool hitPause() const { return hitPauseTime_ > 0; }
	// MoveContact: frames since this character's attack touched, 0 for none.
	int32_t moveContact() const { return moveContactTime_ < 0 ? -moveContactTime_ : moveContactTime_; }
	// Holding back where it may block (Ikemen's SCF_guard), and close enough to an attack to start blocking.
	bool guarding() const { return guarding_; }
	bool inGuardDist() const { return inGuardDist_; }
	// Whose states it's running: itself, or the attacker's after a throw (p2stateno, TargetState).
	const Character *stateOwner() const { return stateOwner_; }
	// Controller types met that don't do anything yet, with how often they ran; for the debug screen.
	const std::map<std::string, int> &unsupportedRuns() const { return unsupported_; }

	// ExprContext
	Value trigger(Trig t, Value arg) override;
	char flag(Flag f) override;
	bool command(const std::string &name) override;
	bool constant(const std::string &name, Value &out) override;
	bool getHitVar(const std::string &name, Value &out) override;
	Value var(VarKind kind, int32_t index) override;
	void setVar(VarKind kind, int32_t index, Value v) override;
	bool hitDefAttr(int32_t stateTypes, int32_t attacks) override;
	ExprContext *redirect(Redirect r, int32_t arg) override;

private:
	void loadConstants(const std::string &cnsPath);
	float constF(const std::string &name, float fallback) const;

	// The state machine (Ikemen's changeState, stateChange1, stateChange2). owner: whose states to take the new one
	// from; null keeps the current state's owner (ChangeState), this character's own for SelfState.
	void changeState(int32_t no, int32_t anim, int32_t ctrl, const Character *owner = nullptr);
	void stateChange1(int32_t no, const Character *owner = nullptr);
	bool stateChange2();
	// Runs a state's controllers; true when one changed state (the rest are skipped).
	bool runState(const StateDef &def, std::vector<int32_t> &persist);
	// Runs one controller; true when it changed state.
	bool runController(const StateController &sc);
	void initStateDef(const StateDef &def);
	// From this character's AIR, or table (ChangeAnim2: the state owner's).
	void changeAnim(int32_t no, const AnimationTable *table = nullptr);
	void runExplod(const StateController &sc);
	void runEnvShake(const StateController &sc);
	void runMakeDust(const StateController &sc);
	void runPalFX(const StateController &sc);
	void runAfterImage(const StateController &sc);
	void runPause(const StateController &sc, bool superPause);
	void runReversalDef(const StateController &sc);
	void runHitOverride(const StateController &sc);
	void runProjectile(const StateController &sc);
	void updateProjectiles();
	void hitDetectProjectiles(Character &getter);
	bool projectileClsnHits(const Projectile &projectile, const Character &getter) const;
	bool projectileClsnTouch(const Projectile &a, int aKind, const Projectile &b, int bKind) const;
	void tradeProjectiles(Character &opponent);
	void cancelProjectile(Projectile &projectile);
	bool setProjectileAnimation(Projectile &projectile, int32_t number, char prefix);
	void autoTurn();
	bool shouldFaceP2() const;
	void posUpdate();
	void setFacing(int f);

	// Hits (Ikemen's HitDef controller, attrCheck, hittableByChar, clsnCheck, hitResultCheck).
	void runHitDef(const StateController &sc);
	bool attrCheck(const HitDef &hd, char attackerStateType) const;
	bool hittableBy(const Character &attacker) const;
	bool clsnHits(const Character &getter) const;
	bool reversalMatches(const Character &attacker) const;
	int hitResultCheck(Character &getter);
	bool resolveReversal(Character &attacker);
	void spawnHitSpark(Character &getter, int result);
	void spawnProjectileHitSpark(const Projectile &projectile, Character &getter, int result);
	Reaction gethitAnimtype() const;
	// Damage after the attacker's attack (attackMul, 1 = 100) and this character's defence, held under its life.
	// absolute: no attack or defence multipliers.
	int32_t computeDamage(int32_t damage, bool kill, float attackMul, bool absolute = false) const;
	// The current element's boxes of a kind (1 attack, 2 hurt), or null.
	const std::vector<Clsn> *boxes(int kind) const;
	bool inGuardState() const;
	bool backHeld() const;
	// HitBy / NotHitBy: whether the slots let an attack with these attributes through.
	bool hitBySlotsAllow(int32_t attrState, int32_t attrAttack) const;
	void runHitBy(const StateController &sc, bool notHitBy);
	void runAssertSpecial(const StateController &sc);

	// Targets and binding (Ikemen's addTarget, dropTargets, exitTarget, targetBind, updateBinding).
	// Only the opponent can be a target here: one player against one.
	bool isTargetBound() const { return bindTo_ != nullptr && targetedBy_ && bindTo_ == p2_; }
	void resetBind();
	void updateBinding();
	void dropTarget();
	// The opponent while it's this character's target, else null.
	Character *target() const { return targetP2_ ? p2_ : nullptr; }
	bool runTargetController(const StateController &sc);

	// Parameter helpers for controllers.
	bool paramF(const StateController &sc, const char *name, size_t index, float &out);
	bool paramI(const StateController &sc, const char *name, size_t index, int32_t &out);

	Sff sff_;
	AnimationTable anims_;
	StateTable states_;
	std::map<std::string, Value> constants_;
	std::unique_ptr<SpriteRenderer> renderer_;
	SoundBank sound_;
	int palette_ = -1;
	bool animShared_ = false;
	int32_t makeDustSpacing_ = 0;
	struct PaletteEffect {
		int32_t time = 0, elapsed = 0, add[3] = { 0, 0, 0 }, sinAdd[3] = { 0, 0, 0 }, cycle = 0;
		int32_t mul[3] = { 256, 256, 256 };
	} palfx_;
	struct AfterImageFrame { Animation anim; float x = 0, y = 0; int facing = 1; };
	struct AfterImageState {
		int32_t time = 0, length = 20, timeGap = 1, frameGap = 4, recordGap = 0;
		int32_t bright[3] = { 30, 30, 30 }, contrast[3] = { 120, 120, 220 }, add[3] = { 10, 10, 25 };
		float mul[3] = { .65f, .65f, .75f };
		Trans trans = Trans::Add;
		std::vector<AfterImageFrame> frames;
	} afterImage_;

	const StateDef *state_ = nullptr;     // the current state's definition
	const Character *stateOwner_ = this;  // whose state file it comes from
	StateDef emptyState_;                 // stands in for a missing state
	std::vector<int32_t> persist_;        // the current state's persistent counters
	std::map<int32_t, std::vector<int32_t>> negPersist_; // -1, -2, -3's
	bool stchtmp_ = false;                // a state change waiting for stateChange2
	int minus_ = 0;                       // which negative state is running, 0 for the current state
	int changeStateNest_ = 0;

	int32_t vars_[60] = {};
	float fvars_[40] = {};
	int32_t sysvars_[5] = {};
	float sysfvars_[5] = {};

	// Reset every frame (actionPrepare).
	bool posFreeze_ = false, screenBound_ = true, playerPush_ = true;
	float widthFront_ = 0, widthBack_ = 0;
	// Turned at the end of actionRun when set by facep2 / Turn with deferral.
	int pendingFacing_ = 0;
	int airJumpCount_ = 0;

	// Hits.
	HitDef hitdef_;
	GetHitVars ghv_;
	int32_t hitPauseTime_ = 0;
	bool ranFree_ = true;           // this frame's actionRun wasn't in hitpause (Ikemen's acttmp > 0)
	bool gotHit_ = false;           // hit this frame (Ikemen's CSF_gethit)
	bool hitdefContact_ = false;    // the HitDef touched someone this frame
	char moveContactType_ = 'H';    // H hit, G guarded
	int32_t moveContactTime_ = 0;   // MoveContact's count; -1 just touched, becoming 1 at tick
	int32_t hitCount_ = 0, uniqHitCount_ = 0, guardCount_ = 0;
	int hitState_ = 0;              // 0 not hit, 1 hit, 2 falling (Ikemen's hittmp; F / - / + hitflags)
	int32_t unhittableTime_ = 0;
	int p1facing_ = 0;              // a facing a hit sets, applied at the end of actionRun
	// MUGEN 1.x character (not WinMUGEN): attacker pauses one frame longer, guard.hittime defaults differ.
	bool mugen1_ = true;
	bool guarding_ = false, inGuardDist_ = false;
	bool prevFallflag_ = false;     // HitFall before this frame's hits, for juggle costs
	// HitBy (not false) and NotHitBy (not true) slots: value and value2.
	struct HitBySlot {
		bool notHitBy = false;
		int32_t time = 0;
		int32_t attrState = 0, attrAttack = 0;
	};
	HitBySlot hitBy_[2];
	HitOverride hitOverride_[8];
	std::vector<Projectile> projectiles_;
	int32_t projectileContactTime_ = -1, projectileContactId_ = -1;
	char projectileContactType_ = 0;
	uint32_t special_ = 0;          // AssertSpecial flags, cleared each frame
	int32_t juggle_ = 0;            // this attack's juggle cost (statedef juggle)
	float customDefense_ = 1, fallDefenseMul_ = 1;
	bool defenseMulDelay_ = false;
	// Targets: whether the opponent is this character's target, and this character is the opponent's (with the
	// juggle points it has left against it).
	bool targetP2_ = false, targetedBy_ = false;
	int32_t juggleLeft_ = 15;
	// The attacker's backward displacement while its target is pinned to a screen corner.
	float cornerPushVel_ = 0, cornerPushMul_ = 0.7f;
	// Bound to another character (TargetBind): where, relative to it, for how long.
	const Character *bindTo_ = nullptr;
	int32_t bindTime_ = 0;
	float bindPos_[2] = { 0, 0 };

	// Rounds.
	bool ko_ = false;                       // knocked out (Alive is false)
	bool overAlive_ = false, overKo_ = false; // done for the round: in a win/lose pose, or KO'd and down
	float startX_ = 0, startY_ = 0;
	int startFacing_ = 1;
	int32_t powerGain_ = 0;                 // power from hits this frame, added at the end of actionRun

	// Constants, cached.
	float yaccel_ = 0.44f, standFriction_ = 0.85f, crouchFriction_ = 0.82f;
	float groundFront_ = 16, groundBack_ = 15, airFront_ = 12, airBack_ = 12, height_ = 60;
	float attackDist_[2] = { 160, 0 };
	int32_t liedownTime_ = 60, airJuggle_ = 15;
	float fallDefenceMul_ = 1.5f; // (100 + data.fall.defence_up) / 100

	Character *p2_ = nullptr;
	const FightWorld *world_ = nullptr;
	uint32_t random_ = 12345;
	std::map<std::string, int> unsupported_;
};

} // namespace m360
