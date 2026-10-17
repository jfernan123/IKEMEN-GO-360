#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/expr.h"

namespace m360 {

// Every state controller type MUGEN 1.1 has, plus Ikemen's common ones. Unknown names load as Unknown.
enum class CtrlType : uint16_t {
	AfterImage, AfterImageTime, AllPalFX, AngleAdd, AngleDraw, AngleMul, AngleSet, AppendToClipboard, AssertSpecial,
	AttackDist, AttackMulSet, BGPalFX, BindToParent, BindToRoot, BindToTarget, ChangeAnim, ChangeAnim2, ChangeState,
	ClearClipboard, CtrlSet, DefenceMulSet, DestroySelf, DisplayToClipboard, EnvColor, EnvShake, Explod,
	ExplodBindTime, FallEnvShake, ForceFeedback, GameMakeAnim, Gravity, Helper, HitAdd, HitBy, HitDef, HitFallDamage,
	HitFallSet, HitFallVel, HitOverride, HitVelSet, LifeAdd, LifeSet, MakeDust, ModifyExplod, MoveHitReset, NotHitBy,
	Null, Offset, PalFX, ParentVarAdd, ParentVarSet, Pause, PlayerPush, PlaySnd, PosAdd, PosFreeze, PosSet, PowerAdd,
	PowerSet, Projectile, RemapPal, RemoveExplod, ReversalDef, ScreenBound, SelfState, SndPan, SprPriority,
	StateTypeSet, StopSnd, SuperPause, TargetBind, TargetDrop, TargetFacing, TargetLifeAdd, TargetPowerAdd,
	TargetState, TargetVelAdd, TargetVelSet, Trans, Turn, VarAdd, VarRandom, VarRangeSet, VarSet, VelAdd, VelMul,
	VelSet, VictoryQuote, Width,
	Unknown
};

const char *ctrlTypeName(CtrlType t);

// One "key = value" line of a controller, in file order.
struct Param {
	std::string name;  // lowercase: "value", "x", "var(1)"
	std::string text;  // as written, comment removed
	// For an expression parameter: its comma-separated values, parsed at load; prefix 'F' or 'S' for "F72" / "S5".
	std::vector<Expr> exprs;
	char prefix = 0;
};

// One [State N, label] block.
struct StateController {
	CtrlType type = CtrlType::Unknown;
	std::string typeName;
	std::string label;
	std::string file;
	int line = 0;
	// All of these must hold...
	std::vector<Expr> triggerAll;
	// ...and every line of at least one group (trigger1, trigger2, ...).
	std::vector<std::vector<Expr>> triggers;
	// How often it may run while its triggers hold: 1 every tick, 0 once per state, n every nth time.
	int32_t persistent = 1;
	bool ignoreHitPause = false;
	std::vector<Param> params;

	// The first value of a parameter, or null.
	const std::string *param(const std::string &name) const;
	// The parameter itself, or null.
	const Param *find(const std::string &name) const;
	bool triggered(ExprContext &ctx) const;
};

// One [Statedef N] with its controllers.
struct StateDef {
	int32_t no = 0;
	// S C A L, I A H, S C A N; 'U' leaves the character's as it is.
	char type = 'S', moveType = 'I', physics = 'N';
	// Parameters given; empty Exprs when not.
	Expr anim, ctrl, poweradd, juggle, facep2, sprpriority, hitdefpersist, movehitpersist, hitcountpersist;
	std::vector<Expr> velset;
	std::vector<StateController> ctrls;
	std::string file;
	int line = 0;
};

// A character's states from all its files. Translated from Ikemen-GO src/compiler.go (stateCompileCNS, stateDef,
// parseSection): files load in order, and the first file to define a state keeps it; negative states (-1, -2, -3,
// run every tick) gather controllers from every file.
class StateTable {
public:
	// Adds a CNS (or CMD) file's states; problems are logged and the bad controllers skipped, as MUGEN does.
	bool load(const std::string &path, std::string &error);
	const StateDef *find(int32_t no) const;
	const std::map<int32_t, StateDef> &all() const { return states_; }

	// Counts for a load report.
	struct Stats {
		int states = 0, controllers = 0, skipped = 0, paramErrors = 0;
		std::map<std::string, int> byType;           // controller type -> how many
		std::vector<std::string> problems;           // one line each
	};
	const Stats &stats() const { return stats_; }

private:
	std::map<int32_t, StateDef> states_;
	Stats stats_;
};

// Whether a parameter's value is written in the expression language (most are) or is text such as "S, NA" for attr.
bool isExpressionParam(CtrlType type, const std::string &param);

// Animation and sound numbers may name the shared effects files: "F72" (fightfx.air) or "S5, 0" (common.snd).
// Returns the text without the letter, and prefix 'F', 'S' or 0.
std::string resourcePrefix(const std::string &text, char &prefix);

} // namespace m360
