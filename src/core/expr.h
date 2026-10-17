#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// MUGEN's expression language: the triggers and parameter values in CNS and CMD files.
// Parsed once at load into a tree; evaluated each tick against an ExprContext (the character).
// The grammar and its quirks follow Ikemen-GO src/compiler.go (tokenizer, expBoolOr .. expValue, evaluateComparison,
// expRange); only the triggers common characters use are known so far, the rest parse as errors.
namespace m360 {

// A number: MUGEN keeps ints and floats apart (5 / 2 is 2, 5 / 2.0 is 2.5). Truth values are ints 0 and 1.
struct Value {
	bool isFloat = false;
	int32_t i = 0;
	float f = 0;

	static Value I(int32_t v) { return Value{ false, v, 0 }; }
	static Value F(float v) { return Value{ true, 0, v }; }
	static Value B(bool v) { return I(v ? 1 : 0); }
	float toF() const { return isFloat ? f : (float)i; }
	int32_t toI() const { return isFloat ? (int32_t)f : i; }
	bool toB() const { return isFloat ? f != 0 : i != 0; }
};

// Triggers and functions that take no special syntax, by id. Each is a value of the context's character.
enum class Trig : uint16_t {
	// Plain values
	Time, AnimTime, Anim, StateNo, PrevStateNo, Ctrl, Facing, Power, PowerMax, Life, LifeMax, Alive, HitOver,
	HitShakeOver, MoveContact, MoveHit, MoveGuarded, MoveReversed, HitFall, HitCount, UniqHitCount, CanRecover,
	InGuardDist, RoundState, RoundNo, RoundsExisted, MatchOver, PalNo, IsBound, GameTime, TeamSide, NumProj,
	NumHelper, NumTarget, NumExplod, Random, Win, Lose, DrawGame, IsHelper, AiLevel, NumEnemy, NumPartner,
	FrontEdgeBodyDist, BackEdgeBodyDist, FrontEdgeDist, BackEdgeDist, StateTime,
	// With an axis (x, y, z): "vel x"
	Vel, Pos, P2Dist, P2BodyDist, ScreenPos, CameraPos, HitVel,
	// With one argument: "animelemtime(2)"
	AnimElemTime, AnimElemNo, SelfAnimExist, AnimExist, Const720p, ProjHitTime, ProjContactTime, ProjGuardedTime,
	Count
};

// "statetype = S" and friends compare a letter.
enum class Flag : uint8_t { StateType, MoveType, P2StateType, P2MoveType };

// "p2, statetype": who a value is read from.
enum class Redirect : uint8_t { P2, Parent, Root, Target, Helper, Enemy, EnemyNear, Partner, PlayerId };

enum class VarKind : uint8_t { Var, FVar, SysVar, SysFVar };

// What an expression reads from and writes to: a character, as the game runs.
class ExprContext {
public:
	virtual ~ExprContext() = default;
	// A plain trigger; arg is the axis (0 x, 1 y, 2 z) or the argument for those that take one.
	virtual Value trigger(Trig t, Value arg) = 0;
	// "statetype" etc.: the letter, uppercase (S C A L, I A H).
	virtual char flag(Flag f) = 0;
	virtual bool command(const std::string &name) = 0;
	// const(velocity.walk.fwd.x) and gethitvar(xvel): by lowercase name; false when unknown.
	virtual bool constant(const std::string &name, Value &out) = 0;
	virtual bool getHitVar(const std::string &name, Value &out) = 0;
	virtual Value var(VarKind kind, int32_t index) = 0;
	virtual void setVar(VarKind kind, int32_t index, Value v) = 0;
	// "hitdefattr = SC, NA": whether the current HitDef matches. stateTypes bits S=1 C=2 A=4; attacks bit
	// (class * 3 + kind), class N=0 S=1 H=2, kind A=0 T=1 P=2.
	virtual bool hitDefAttr(int32_t stateTypes, int32_t attacks) = 0;
	// Another character to read from, or null (the expression then reads as 0, like MUGEN's "bottom").
	virtual ExprContext *redirect(Redirect r, int32_t arg) = 0;
};

struct ExprNode;

// One parsed expression. An empty Expr (parse failed, or never set) evaluates to 0.
class Expr {
public:
	Expr();
	~Expr();
	Expr(Expr &&) noexcept;
	Expr &operator=(Expr &&) noexcept;

	// Parses text; on failure error says why and the expression stays empty.
	bool parse(const std::string &text, std::string &error);
	Value eval(ExprContext &ctx) const;
	bool empty() const { return !root_; }
	const std::string &source() const { return source_; }

private:
	friend std::vector<Expr> parseExprList(const std::string &text, std::string &error);
	std::unique_ptr<ExprNode> root_;
	std::string source_;
};

// A parameter's comma-separated expressions ("velset = 2, -3"); missing ones are empty.
std::vector<Expr> parseExprList(const std::string &text, std::string &error);

} // namespace m360
