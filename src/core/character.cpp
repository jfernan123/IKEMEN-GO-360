#include "core/character.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstring>

#include "core/file.h"
#include "core/effects.h"
#include "core/ini.h"
#include "core/log.h"
#include "core/text.h"
#include "platform/platform.h"

namespace m360 {

namespace {

// The most ChangeStates one frame may chain before MUGEN calls it a loop.
constexpr int MAX_STATE_LOOP = 2500;

// A file next to the character, else under data/ (where common1.cns lives), else as given.
std::string findFile(const std::string &dir, const std::string &name)
{
	std::vector<uint8_t> probe;
	for (const std::string &path : { dir + name, "data/" + name, name }) {
		if (readFile(path, probe))
			return path;
	}
	return dir + name;
}

Value number(const std::string &s)
{
	const std::string t(text::trim(s));
	if (t.find_first_of(".eE") != std::string::npos)
		return Value::F((float)text::atof(t));
	return Value::I(text::atoi(t));
}

} // namespace

// --- Loading ---------------------------------------------------------------------------------------------------------

bool Character::load(const std::string &defPath, int paletteNo, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(defPath, bytes)) {
		error = "can't read " + defPath;
		return false;
	}
	const std::vector<IniSection> def = parseIni(std::string(bytes.begin(), bytes.end()));
	const std::string dir = directoryOf(defPath);
	if (const IniSection *info = findSection(def, "info")) {
		name = info->text("displayname");
		if (name.empty())
			name = info->text("name");
		int32_t lw = 320, lh = 240;
		if (info->ints("localcoord", { &lw, &lh }) && lw > 0)
			localScale = 320.0f / lw;
		// "1.0" and "1.1" are MUGEN 1.x; WinMUGEN characters give a date ("04.14.2001") or nothing.
		mugen1_ = text::startsWith(text::trim(info->text("mugenversion")), "1.");
	}
	const IniSection *files = findSection(def, "files");
	if (!files) {
		error = defPath + " has no [Files] section";
		return false;
	}
	if (!sff_.load(dir + files->text("sprite"), error))
		return false;
	if (!anims_.load(dir + files->text("anim"), error))
		return false;
	renderer_ = std::make_unique<SpriteRenderer>(sff_);
	// Palette n is 1,n in an SFF v2; a missing one falls back to the sprites' own.
	palette_ = sff_.paletteIndex(1, (uint16_t)paletteNo);
	renderer_->setPalette(palette_);
	const std::string soundFile = files->text("sound");
	if (!soundFile.empty() && !sound_.load(dir + soundFile, error))
		return false;

	const std::string cmd = files->text("cmd");
	if (!cmd.empty() && !commands.load(dir + cmd, error))
		return false;
	const std::string cns = files->text("cns");
	if (!cns.empty())
		loadConstants(dir + cns);

	// States: st, st0..st9 in order, then the CMD (its -1), then the common states. The first to define a state
	// keeps it, so a character's own files override common1.cns.
	std::vector<std::pair<std::string, std::string>> stFiles;
	for (const auto &[key, value] : files->values) {
		if (key == "st" || (key.size() == 3 && key.compare(0, 2, "st") == 0 && std::isdigit((unsigned char)key[2])))
			stFiles.push_back({ key, value });
	}
	std::sort(stFiles.begin(), stFiles.end());
	for (const auto &[key, value] : stFiles) {
		const std::string file(text::trim(value));
		if (!file.empty() && !states_.load(findFile(dir, file), error))
			return false;
	}
	if (!cmd.empty())
		states_.load(dir + cmd, error);
	const std::string common = files->text("stcommon");
	if (!common.empty() && !states_.load(findFile(dir, common), error))
		return false;
	emptyState_.type = emptyState_.moveType = emptyState_.physics = 'U';

	logf("char: %s: \"%s\", palette %d, %zu states, life %d", defPath.c_str(), name.c_str(), paletteNo,
		states_.all().size(), (int)constF("data.life", 1000));
	for (const std::string &p : states_.stats().problems)
		logf("char: %s", p.c_str());
	startRound(0, 0, 1, true);
	return true;
}

// The [Data], [Size], [Velocity] and [Movement] groups of the CNS, as const() names: "velocity.walk.fwd.x",
// "movement.yaccel", "size.head.pos.y". Velocities always take .x / .y; other groups do when given several values.
void Character::loadConstants(const std::string &cnsPath)
{
	// Ikemen's defaults (src/char.go, CharGlobalInfo / newCharGlobalInfo), for constants a character leaves out.
	const std::pair<const char *, double> defaults[] = { { "data.life", 1000 }, { "data.power", 3000 },
		{ "data.attack", 100 }, { "data.defence", 100 }, { "data.fall.defence_up", 50 }, { "data.liedown.time", 60 },
		{ "data.airjuggle", 15 }, { "size.xscale", 1 }, { "size.yscale", 1 }, { "size.ground.back", 15 },
		{ "size.ground.front", 16 }, { "size.air.back", 12 }, { "size.air.front", 12 }, { "size.height", 60 },
		{ "movement.yaccel", 0.44f }, { "movement.stand.friction", 0.85f }, { "movement.crouch.friction", 0.82f },
		{ "movement.stand.friction.threshold", 2 }, { "movement.crouch.friction.threshold", 0.05f },
		{ "movement.air.gethit.groundlevel", 25 }, { "movement.air.gethit.groundrecover.ground.threshold", -20 },
		{ "movement.air.gethit.groundrecover.groundlevel", 10 }, { "movement.air.gethit.airrecover.threshold", -1 },
		{ "movement.air.gethit.airrecover.yaccel", 0.35f }, { "movement.air.gethit.trip.groundlevel", 15 },
		{ "movement.down.bounce.offset.x", 0 }, { "movement.down.bounce.offset.y", 20 },
		{ "movement.down.bounce.yaccel", 0.4f }, { "movement.down.bounce.groundlevel", 12 },
		{ "movement.down.friction.threshold", 0.05f }, { "movement.airjump.num", 0 },
		{ "movement.airjump.height", 35 } };
	// [Data] values are whole numbers; the rest are measurements.
	for (const auto &[n, v] : defaults)
		constants_[n] = text::startsWith(n, "data.") ? Value::I((int32_t)v) : Value::F((float)v);

	std::vector<uint8_t> bytes;
	if (!readFile(cnsPath, bytes))
		return;
	const std::vector<IniSection> sections = parseIni(std::string(bytes.begin(), bytes.end()));
	for (const char *group : { "data", "size", "velocity", "movement" }) {
		const IniSection *s = findSection(sections, group);
		if (!s)
			continue;
		const bool velocity = std::string(group) == "velocity";
		for (const auto &[key, value] : s->values) {
			const std::vector<std::string> parts = text::split(value, ',');
			const std::string base = std::string(group) + "." + key;
			if (parts.size() == 1 && !velocity) {
				constants_[base] = number(parts[0]);
				continue;
			}
			static const char *const axes[] = { ".x", ".y", ".z" };
			for (size_t i = 0; i < parts.size() && i < 3; i++) {
				if (!parts[i].empty())
					constants_[base + axes[i]] = Value::F((float)text::atof(parts[i]));
			}
		}
	}
	// MUGEN's names for the vertical part of the jumps: jump.neu's y.
	auto alias = [&](const char *to, const char *from) {
		const auto it = constants_.find(from);
		if (it != constants_.end() && !constants_.count(to))
			constants_[to] = it->second;
	};
	alias("velocity.jump.y", "velocity.jump.neu.y");
	alias("velocity.airjump.y", "velocity.airjump.neu.y");
	alias("velocity.runjump.y", "velocity.runjump.fwd.y");

	yaccel_ = constF("movement.yaccel", 0.44f);
	standFriction_ = constF("movement.stand.friction", 0.85f);
	crouchFriction_ = constF("movement.crouch.friction", 0.82f);
	groundFront_ = constF("size.ground.front", 16);
	groundBack_ = constF("size.ground.back", 15);
	airFront_ = constF("size.air.front", 12);
	airBack_ = constF("size.air.back", 12);
	height_ = constF("size.height", 60);
	attackDist_[0] = constF("size.attack.dist", constF("size.attack.dist.x", 160));
	attackDist_[1] = constF("size.attack.dist.y", 0);
	liedownTime_ = (int32_t)constF("data.liedown.time", 60);
	airJuggle_ = (int32_t)constF("data.airjuggle", 15);
	fallDefenceMul_ = (constF("data.fall.defence_up", 50) + 100) / 100;
}

float Character::constF(const std::string &n, float fallback) const
{
	const auto it = constants_.find(n);
	return it == constants_.end() ? fallback : it->second.toF();
}

// Ikemen's resetRound for one character: posReset, clearState, prepareNextRound, selfState 5900.
void Character::startRound(float px, float py, int face, bool newMatch)
{
	startX_ = px;
	startY_ = py;
	startFacing_ = face >= 0 ? 1 : -1;
	posReset();
	ctrl = false;
	life = lifeMax();
	if (newMatch)
		power = 0;
	ko_ = overAlive_ = overKo_ = false;
	powerGain_ = 0;
	for (int32_t &v : sysvars_)
		v = 0;
	for (float &v : sysfvars_)
		v = 0;
	commands.reset();
	stateType = 'S';
	moveType = 'I';
	physics = 'S';
	stchtmp_ = false;
	minus_ = 0;
	hitdef_ = HitDef{};
	hitdef_.guardDist[0] = attackDist_[0];
	hitdef_.guardDist[1] = attackDist_[1];
	ghv_ = GetHitVars{};
	ghv_.downRecovertime = liedownTime_;
	hitPauseTime_ = 0;
	gotHit_ = hitdefContact_ = false;
	moveContactTime_ = hitCount_ = uniqHitCount_ = guardCount_ = 0;
	hitState_ = 0;
	unhittableTime_ = 0;
	p1facing_ = 0;
	guarding_ = inGuardDist_ = prevFallflag_ = false;
	makeDustSpacing_ = 0;
	palfx_ = PaletteEffect{};
	afterImage_ = AfterImageState{};
	for (HitBySlot &s : hitBy_)
		s = HitBySlot{};
	for (HitOverride &s : hitOverride_)
		s = HitOverride{};
	special_ = 0;
	juggle_ = 0;
	customDefense_ = fallDefenseMul_ = 1;
	defenseMulDelay_ = false;
	targetP2_ = targetedBy_ = false;
	juggleLeft_ = 0;
	cornerPushVel_ = 0;
	cornerPushMul_ = 0.7f;
	resetBind();
	stateNo = prevStateNo = 0;
	stateTime = 0;
	animNo = -1;
	changeAnim(0);
	// State 5900 (common1.cns) clears the variables on the first round, then goes to the intro or stands.
	stateChange1(5900, this);
}

void Character::posReset()
{
	x = startX_;
	y = startY_;
	facing = startFacing_;
	vx = vy = 0;
}

void Character::roundStartControl()
{
	overAlive_ = false;
	ctrl = true;
	if (stateNo != 0)
		changeState(0, -1, 1, this);
}

void Character::startOverState(int32_t no)
{
	overAlive_ = true;
	changeState(no, -1, -1, this);
}

bool Character::readyForWinPose() const
{
	if (overAlive_ || overKo_)
		return true;
	return ctrl && moveType == 'I' && stateType == 'S';
}

// --- The state machine -----------------------------------------------------------------------------------------------

void Character::changeAnim(int32_t no, const AnimationTable *table)
{
	// MUGEN turns a negative action into 0; -2 is Ikemen's "no sprite".
	if (no < 0 && no != -2)
		no = 0;
	Animation next;
	if ((table ? *table : anims_).get(no, next)) {
		anim = std::move(next);
		animNo = no;
		animShared_ = false;
	} else if (no != -2) {
		logf("char: %s: no action %d (state %d)", name.c_str(), no, stateNo);
	}
}

bool Character::shouldFaceP2() const
{
	return p2_ && (p2_->x - x) * facing < 0;
}

// Turns to face the opponent, with the turning animation (5 standing, 6 crouching).
void Character::autoTurn()
{
	if (!shouldFaceP2() || (special_ & Special::NoAutoTurn))
		return;
	if (stateType == 'S' && animNo != 5)
		changeAnim(5);
	else if (stateType == 'C' && animNo != 6)
		changeAnim(6);
	facing = -facing;
	vx = -vx;
}

void Character::changeState(int32_t no, int32_t animNoNew, int32_t ctrlNew, const Character *owner)
{
	// MUGEN turns a character with control, standing or crouching, as it changes state (until the round is decided).
	if (minus_ <= 0 && ctrl && (stateType == 'S' || stateType == 'C') && (!world_ || world_->roundState <= 2))
		autoTurn();
	if (animNoNew != -1)
		changeAnim(animNoNew);
	if (ctrlNew >= 0)
		ctrl = ctrlNew != 0;
	stateChange1(no, owner);
	// From the current state, the new one starts now, and runs this frame; from -1, -2, -3 (and before them) it waits
	// for actionRun.
	if (changeStateNest_ == 0 && minus_ == 0) {
		while (stchtmp_ && changeStateNest_ < MAX_STATE_LOOP) {
			stateChange2();
			changeStateNest_++;
			if (!runState(*state_, persist_))
				break;
		}
		if (changeStateNest_ >= MAX_STATE_LOOP)
			logf("char: %s: state loop at state %d", name.c_str(), stateNo);
		changeStateNest_ = 0;
	}
}

void Character::stateChange1(int32_t no, const Character *owner)
{
	if (owner)
		stateOwner_ = owner;
	if (!traceLabel.empty())
		logf("%s frame %u: state %d -> %d%s (time %d, anim %d, pos %.2f,%.2f, vel %.2f,%.2f)", traceLabel.c_str(),
			world_ ? world_->gameTime : 0, stateNo, no, stateOwner_ != this ? " of the opponent" : "", stateTime,
			animNo, x, y, vx, vy);
	prevStateNo = stateNo;
	stateNo = std::max(0, no);
	stateTime = 0;
	state_ = stateOwner_->states_.find(stateNo);
	if (!state_) {
		logf("char: %s: changed to missing state %d (from %d)", name.c_str(), no, prevStateNo);
		state_ = &emptyState_;
	}
	persist_.assign(state_->ctrls.size(), 0);
	stchtmp_ = true;
}

bool Character::stateChange2()
{
	// A state change waits out a hitpause.
	if (!stchtmp_ || hitPause())
		return false;
	initStateDef(*state_);
	stchtmp_ = false;
	return true;
}

// The statedef's own settings, in Ikemen's order (stateDef.Run).
void Character::initStateDef(const StateDef &def)
{
	// A new state forgets the last one's hit counts, contact and HitDef, unless told to keep them.
	auto keeps = [&](const Expr &e) { return !e.empty() && e.eval(*this).toB(); };
	if (!keeps(def.hitcountpersist))
		hitCount_ = uniqHitCount_ = guardCount_ = 0;
	if (!keeps(def.movehitpersist))
		moveContactTime_ = 0;
	if (!keeps(def.hitdefpersist)) {
		hitdef_ = HitDef{};
		hitdef_.guardDist[0] = attackDist_[0];
		hitdef_.guardDist[1] = attackDist_[1];
	}
	if (def.type != 'U')
		stateType = def.type;
	if (def.moveType != 'U')
		moveType = def.moveType;
	if (def.physics != 'U')
		physics = def.physics;
	if (!def.sprpriority.empty())
		sprPriority = def.sprpriority.eval(*this).toI();
	if (!def.juggle.empty())
		juggle_ = def.juggle.eval(*this).toI();
	if (!def.facep2.empty() && def.facep2.eval(*this).toB() && shouldFaceP2()) {
		facing = -facing;
		vx = -vx;
	}
	if (!def.velset.empty()) {
		if (!def.velset[0].empty())
			vx = def.velset[0].eval(*this).toF();
		if (def.velset.size() > 1 && !def.velset[1].empty())
			vy = def.velset[1].eval(*this).toF();
	}
	if (!def.anim.empty()) {
		const int32_t a = def.anim.eval(*this).toI();
		if (a != -1)
			changeAnim(a);
	}
	if (!def.ctrl.empty())
		ctrl = def.ctrl.eval(*this).toB();
	if (!def.poweradd.empty())
		power = std::clamp(power + def.poweradd.eval(*this).toI(), 0, (int32_t)constF("data.power", 3000));
}

// Controllers in order, each gated by its persistent counter and triggers (Ikemen's StateBlock.Run).
bool Character::runState(const StateDef &def, std::vector<int32_t> &persist)
{
	if (persist.size() < def.ctrls.size())
		persist.resize(def.ctrls.size(), 0);
	for (size_t i = 0; i < def.ctrls.size(); i++) {
		const StateController &sc = def.ctrls[i];
		// A hit freezes the state, all but what asks to run anyway.
		if (hitPause() && !sc.ignoreHitPause)
			continue;
		int32_t &ps = persist[i];
		if (ps != INT32_MAX)
			ps--;
		if (ps > 0)
			continue;
		if (!sc.triggered(*this))
			continue;
		if (runController(sc))
			return true;
		// persistent = 0 runs once in the state; n runs every nth time the triggers hold.
		ps = sc.persistent > 128 ? 1 : sc.persistent <= 0 ? INT32_MAX : sc.persistent;
	}
	return false;
}

bool Character::paramF(const StateController &sc, const char *n, size_t index, float &out)
{
	const Param *p = sc.find(n);
	if (!p || index >= p->exprs.size() || p->exprs[index].empty())
		return false;
	out = p->exprs[index].eval(*this).toF();
	return true;
}

bool Character::paramI(const StateController &sc, const char *n, size_t index, int32_t &out)
{
	const Param *p = sc.find(n);
	if (!p || index >= p->exprs.size() || p->exprs[index].empty())
		return false;
	out = p->exprs[index].eval(*this).toI();
	return true;
}

bool Character::runController(const StateController &sc)
{
	float f = 0;
	int32_t i = 0;
	switch (sc.type) {
	case CtrlType::ChangeState:
	case CtrlType::SelfState: {
		int32_t value = -1, a = -1, c = -1;
		paramI(sc, "value", 0, value);
		paramI(sc, "ctrl", 0, c);
		const Param *ap = sc.find("anim");
		if (ap && ap->prefix != 'F')
			paramI(sc, "anim", 0, a);
		// SelfState returns to this character's own states; ChangeState stays in whoever's it's running.
		changeState(value, a, c, sc.type == CtrlType::SelfState ? this : nullptr);
		return true;
	}
	case CtrlType::ChangeAnim:
	case CtrlType::ChangeAnim2: {
		const Param *vp = sc.find("value");
		if (!vp)
			return false;
		paramI(sc, "value", 0, i);
		if (vp->prefix == 'F') {
			Animation next;
			if (world_ && world_->effects && world_->effects->getSharedAnimation(i, next)) {
				anim = std::move(next);
				animNo = i;
				animShared_ = true;
			} else {
				logf("char: %s: no shared action F%d (state %d)", name.c_str(), i, stateNo);
			}
		} else {
			// ChangeAnim2: an action of the state owner's AIR, drawn with this character's sprites.
			changeAnim(i, sc.type == CtrlType::ChangeAnim2 ? &stateOwner_->anims_ : nullptr);
		}
		int32_t elem = 0, elemtime = 0;
		const bool hasElem = paramI(sc, "elem", 0, elem);
		const bool hasTime = paramI(sc, "elemtime", 0, elemtime);
		if (hasElem || hasTime)
			anim.setAnimElem(hasElem ? elem : 1, elemtime);
		return false;
	}
	case CtrlType::VelSet:
		if (paramF(sc, "x", 0, f))
			vx = f;
		if (paramF(sc, "y", 0, f))
			vy = f;
		return false;
	case CtrlType::VelAdd:
		if (paramF(sc, "x", 0, f))
			vx += f;
		if (paramF(sc, "y", 0, f))
			vy += f;
		return false;
	case CtrlType::VelMul:
		if (paramF(sc, "x", 0, f))
			vx *= f;
		if (paramF(sc, "y", 0, f))
			vy *= f;
		return false;
	case CtrlType::PosSet:
		// x is from the camera's centre, y from the ground.
		if (paramF(sc, "x", 0, f))
			x = (world_ ? world_->camX : 0) + f;
		if (paramF(sc, "y", 0, f))
			y = f;
		return false;
	case CtrlType::PosAdd:
		if (paramF(sc, "x", 0, f))
			x += f * facing;
		if (paramF(sc, "y", 0, f))
			y += f;
		return false;
	case CtrlType::CtrlSet:
		if (paramI(sc, "value", 0, i))
			ctrl = i != 0;
		return false;
	case CtrlType::Turn:
		facing = -facing;
		vx = -vx;
		return false;
	case CtrlType::StateTypeSet: {
		auto letter = [&](const char *n, char &out, const char *allowed) {
			if (const std::string *t = sc.param(n)) {
				const std::string v(text::trim(*t));
				const char c = v.empty() ? 0 : (char)std::toupper((unsigned char)v[0]);
				if (c && std::strchr(allowed, c))
					out = c;
			}
		};
		letter("statetype", stateType, "SCAL");
		letter("movetype", moveType, "IAH");
		letter("physics", physics, "SCAN");
		return false;
	}
	case CtrlType::VarSet:
	case CtrlType::VarAdd: {
		const bool add = sc.type == CtrlType::VarAdd;
		for (const Param &p : sc.params) {
			VarKind kind;
			int32_t index = 0;
			if (p.name == "v" || p.name == "fv" || p.name == "sysvar" || p.name == "sysfvar") {
				// "v = 3, value = 5" form: the index here, the value in "value".
				kind = p.name == "v" ? VarKind::Var : p.name == "fv" ? VarKind::FVar
					: p.name == "sysvar" ? VarKind::SysVar : VarKind::SysFVar;
				if (p.exprs.empty())
					continue;
				index = p.exprs[0].eval(*this).toI();
				const Param *vp = sc.find("value");
				if (!vp || vp->exprs.empty())
					continue;
				const Value v = vp->exprs[0].eval(*this);
				const Value cur = var(kind, index);
				setVar(kind, index,
					!add ? v
						 : cur.isFloat || v.isFloat ? Value::F(cur.toF() + v.toF()) : Value::I(cur.i + v.toI()));
				continue;
			}
			// "var(3) = 5" form.
			const size_t open = p.name.find('(');
			if (open == std::string::npos || p.exprs.empty())
				continue;
			const std::string fn = p.name.substr(0, open);
			if (fn == "var")
				kind = VarKind::Var;
			else if (fn == "fvar")
				kind = VarKind::FVar;
			else if (fn == "sysvar")
				kind = VarKind::SysVar;
			else if (fn == "sysfvar")
				kind = VarKind::SysFVar;
			else
				continue;
			Expr indexExpr;
			std::string err;
			indexExpr.parse(p.name.substr(open + 1, p.name.size() - open - 2), err);
			index = indexExpr.eval(*this).toI();
			const Value v = p.exprs[0].eval(*this);
			if (add) {
				const Value cur = var(kind, index);
				setVar(kind, index, cur.isFloat || v.isFloat ? Value::F(cur.toF() + v.toF()) : Value::I(cur.i + v.toI()));
			} else {
				setVar(kind, index, v);
			}
		}
		return false;
	}
	case CtrlType::VarRangeSet: {
		int32_t first = 0, last = 59;
		paramI(sc, "first", 0, first);
		if (paramI(sc, "value", 0, i)) {
			paramI(sc, "last", 0, last);
			for (int32_t k = std::max(0, first); k <= std::min(59, last); k++)
				vars_[k] = i;
		} else if (paramF(sc, "fvalue", 0, f)) {
			last = 39;
			paramI(sc, "last", 0, last);
			for (int32_t k = std::max(0, first); k <= std::min(39, last); k++)
				fvars_[k] = f;
		}
		return false;
	}
	case CtrlType::PosFreeze:
		i = 1;
		paramI(sc, "value", 0, i);
		posFreeze_ = i != 0;
		return false;
	case CtrlType::SprPriority:
		i = 0;
		paramI(sc, "value", 0, i);
		sprPriority = i;
		return false;
	case CtrlType::Width:
		if (paramF(sc, "value", 0, f)) {
			widthFront_ = f;
			paramF(sc, "value", 1, widthBack_);
		}
		if (paramF(sc, "player", 0, f)) {
			widthFront_ = f;
			paramF(sc, "player", 1, widthBack_);
		}
		return false;
	case CtrlType::ScreenBound:
		i = 1;
		paramI(sc, "value", 0, i);
		screenBound_ = i != 0;
		return false;
	case CtrlType::PlayerPush:
		i = 1;
		paramI(sc, "value", 0, i);
		playerPush_ = i != 0;
		return false;
	case CtrlType::HitDef: runHitDef(sc); return false;
	case CtrlType::ReversalDef: runReversalDef(sc); return false;
	case CtrlType::HitOverride: runHitOverride(sc); return false;
	case CtrlType::Explod: runExplod(sc); return false;
	case CtrlType::EnvShake: runEnvShake(sc); return false;
	case CtrlType::MakeDust: runMakeDust(sc); return false;
	case CtrlType::PalFX: runPalFX(sc); return false;
	case CtrlType::AfterImage: runAfterImage(sc); return false;
	case CtrlType::AfterImageTime:
		paramI(sc, "time", 0, afterImage_.time);
		if (afterImage_.time == 1)
			afterImage_.time = 0;
		return false;
	case CtrlType::Pause: runPause(sc, false); return false;
	case CtrlType::SuperPause: runPause(sc, true); return false;
	case CtrlType::PlaySnd: {
		const Param *value = sc.find("value");
		if (!value) return false;
		int32_t group = 0, number = 0, channel = -1;
		paramI(sc, "value", 0, group);
		paramI(sc, "value", 1, number);
		paramI(sc, "channel", 0, channel);
		const SoundBank *bank = value->prefix == 'F' && world_ ? world_->commonSound : &sound_;
		if (bank) bank->play(group, number, channel);
		return false;
	}
	case CtrlType::StopSnd:
		i = -1;
		paramI(sc, "channel", 0, i);
		platform::stopSound(i);
		return false;
	case CtrlType::RemapPal: {
		int32_t group = -1, number = 0;
		paramI(sc, "dest", 0, group);
		paramI(sc, "dest", 1, number);
		const int mapped = group >= 0 ? sff_.paletteIndex((uint16_t)group, (uint16_t)number) : -1;
		if (mapped >= 0) {
			palette_ = mapped;
			renderer_->setPalette(mapped);
		}
		return false;
	}
	case CtrlType::FallEnvShake:
		if (world_ && world_->effects && ghv_.fallEnvShakeTime > 0) {
			world_->effects->startShake(ghv_.fallEnvShakeTime, ghv_.fallEnvShakeFreq, (float)ghv_.fallEnvShakeAmpl,
				ghv_.fallEnvShakePhase, ghv_.fallEnvShakeMul, ghv_.fallEnvShakeDir, ghv_.fallEnvShakeDirAdd,
				ghv_.fallEnvShakeDecay);
			ghv_.fallEnvShakeTime = 0;
		}
		return false;
	case CtrlType::HitVelSet:
		// The get-hit velocity the hit gave, per axis asked for.
		if (paramI(sc, "x", 0, i) && i)
			vx = ghv_.xvel * facing;
		if (paramI(sc, "y", 0, i) && i)
			vy = ghv_.yvel;
		return false;
	case CtrlType::HitFallVel:
		// The fall velocity the hit gave, once a fall begins.
		if (moveType == 'H') {
			if (isSet(ghv_.fallXvel))
				vx = ghv_.fallXvel;
			vy = ghv_.fallYvel;
		}
		return false;
	case CtrlType::HitFallSet:
		if (paramI(sc, "value", 0, i)) {
			if (i >= 0)
				ghv_.fallflag = i != 0;
			if (!targetedBy_)
				return false;
		}
		if (paramF(sc, "xvel", 0, f))
			ghv_.fallXvel = f;
		if (paramF(sc, "yvel", 0, f))
			ghv_.fallYvel = f;
		return false;
	case CtrlType::HitFallDamage:
		if (moveType == 'H') {
			const int32_t dmg = computeDamage(ghv_.fallDamage, ghv_.fallKill, 1);
			life = std::max(0, life - dmg);
			ghv_.fallDamage = 0;
		}
		return false;
	case CtrlType::DefenceMulSet: {
		// MUGEN characters: value divides damage, and only counts once hit.
		f = 1;
		paramF(sc, "value", 0, f);
		i = 0;
		paramI(sc, "multype", 0, i);
		customDefense_ = i != 0 ? f : (f != 0 ? 1 / f : 1);
		i = 1;
		paramI(sc, "onhit", 0, i);
		defenseMulDelay_ = i != 0;
		return false;
	}
	case CtrlType::HitBy:
	case CtrlType::NotHitBy: runHitBy(sc, sc.type == CtrlType::NotHitBy); return false;
	case CtrlType::AssertSpecial: runAssertSpecial(sc); return false;
	case CtrlType::TargetBind:
	case CtrlType::TargetState:
	case CtrlType::TargetLifeAdd:
	case CtrlType::TargetFacing:
	case CtrlType::TargetVelSet:
	case CtrlType::TargetVelAdd:
	case CtrlType::TargetPowerAdd:
	case CtrlType::TargetDrop: return runTargetController(sc);
	case CtrlType::Null: return false;
	default:
		// Accepted, not done yet: sound, effects, KO. They come with the rest of milestone 6.
		unsupported_[sc.typeName]++;
		return false;
	}
}

void Character::runEnvShake(const StateController &sc)
{
	if (!world_ || !world_->effects)
		return;
	int32_t time = 0, amplitude = -4;
	float frequency = 60, phase = notSetF(), multiplier = 1, direction = 0, directionAdd = 0, decay = 1;
	paramI(sc, "time", 0, time);
	paramI(sc, "ampl", 0, amplitude);
	paramF(sc, "freq", 0, frequency);
	paramF(sc, "phase", 0, phase);
	paramF(sc, "mul", 0, multiplier);
	paramF(sc, "dir", 0, direction);
	paramF(sc, "diradd", 0, directionAdd);
	paramF(sc, "decay", 0, decay);
	world_->effects->startShake(time, frequency, amplitude / localScale, phase, multiplier, direction, directionAdd, decay);
}

void Character::runMakeDust(const StateController &sc)
{
	if (!world_ || !world_->effects)
		return;
	int32_t spacing = 3;
	paramI(sc, "spacing", 0, spacing);
	spacing = std::max(1, spacing);
	if (makeDustSpacing_ < spacing)
		return;
	makeDustSpacing_ = 0;
	auto dust = [&](const char *parameter, bool required) {
		if (!required && !sc.find(parameter))
			return;
		float px = 0, py = 0;
		paramF(sc, parameter, 0, px);
		paramF(sc, parameter, 1, py);
		FightEffects::Spawn spawn;
		spawn.animNo = 120;
		spawn.shared = true;
		spawn.owner = playerNo;
		spawn.x = x + px / localScale * facing;
		spawn.y = y + py / localScale;
		spawn.facing = facing;
		spawn.layer = 1;
		spawn.priority = INT32_MAX;
		world_->effects->spawnExplod(spawn);
	};
	dust("pos", true);
	dust("pos2", false);
}

void Character::runPalFX(const StateController &sc)
{
	palfx_ = PaletteEffect{};
	paramI(sc, "time", 0, palfx_.time);
	for (int k = 0; k < 3; k++) {
		paramI(sc, "add", k, palfx_.add[k]);
		paramI(sc, "mul", k, palfx_.mul[k]);
		paramI(sc, "sinadd", k, palfx_.sinAdd[k]);
	}
	paramI(sc, "sinadd", 3, palfx_.cycle);
	palfx_.cycle = std::abs(palfx_.cycle);
	if (!traceLabel.empty())
		logf("%s frame %u: palfx %d ticks", traceLabel.c_str(), world_ ? world_->gameTime : 0, palfx_.time);
}

void Character::runAfterImage(const StateController &sc)
{
	afterImage_ = AfterImageState{};
	afterImage_.time = 1;
	paramI(sc, "time", 0, afterImage_.time);
	paramI(sc, "length", 0, afterImage_.length);
	paramI(sc, "timegap", 0, afterImage_.timeGap);
	paramI(sc, "framegap", 0, afterImage_.frameGap);
	afterImage_.length = std::clamp(afterImage_.length, 0, 60);
	afterImage_.timeGap = std::max(1, afterImage_.timeGap);
	afterImage_.frameGap = std::max(1, afterImage_.frameGap);
	for (int k = 0; k < 3; k++) {
		paramI(sc, "palbright", k, afterImage_.bright[k]);
		paramI(sc, "palcontrast", k, afterImage_.contrast[k]);
		paramI(sc, "paladd", k, afterImage_.add[k]);
		paramF(sc, "palmul", k, afterImage_.mul[k]);
	}
	if (const std::string *value = sc.param("trans")) {
		const std::string trans = text::toLower(text::trim(*value));
		afterImage_.trans = trans == "sub" ? Trans::Sub : Trans::Add;
	}
	if (!traceLabel.empty())
		logf("%s frame %u: afterimage length %d", traceLabel.c_str(), world_ ? world_->gameTime : 0, afterImage_.length);
}

void Character::runPause(const StateController &sc, bool superPause)
{
	if (!world_ || !world_->effects)
		return;
	int32_t time = superPause ? 30 : 0, moveTime = 0, pauseBackground = 1;
	paramI(sc, "time", 0, time);
	paramI(sc, "movetime", 0, moveTime);
	paramI(sc, "pausebg", 0, pauseBackground);
	world_->effects->startPause(playerNo, time, moveTime, pauseBackground != 0, superPause);
	if (!traceLabel.empty())
		logf("%s frame %u: %spause %d ticks", traceLabel.c_str(), world_ ? world_->gameTime : 0,
			superPause ? "super" : "", time);
	if (!superPause)
		return;
	int32_t powerAdd = 0;
	paramI(sc, "poweradd", 0, powerAdd);
	power = std::clamp(power + powerAdd, 0, powerMax());
	const Param *animParam = sc.find("anim");
	int32_t effectAnim = 100;
	if (animParam)
		paramI(sc, "anim", 0, effectAnim);
	FightEffects::Spawn spawn;
	spawn.animNo = effectAnim;
	spawn.shared = !animParam || animParam->prefix != 'S';
	spawn.owner = playerNo;
	float px = 0, py = 0;
	paramF(sc, "pos", 0, px);
	paramF(sc, "pos", 1, py);
	spawn.x = x + px / localScale * facing;
	spawn.y = y + py / localScale;
	spawn.facing = facing;
	spawn.layer = 1;
	world_->effects->spawnExplod(spawn);
	if (const Param *sound = sc.find("sound")) {
		int32_t group = -1, number = 0;
		paramI(sc, "sound", 0, group);
		paramI(sc, "sound", 1, number);
		const SoundBank *bank = sound->prefix == 'S' ? &sound_ : world_->commonSound;
		if (bank && group >= 0)
			bank->play(group, number);
	}
}

// MUGEN's Explod controller, for the character and shared animations KFM uses. Positions are converted from the
// creator's local coordinates to the stage; front/back/left/right remain fixed to the screen like Ikemen's setPos.
void Character::runExplod(const StateController &sc)
{
	if (!world_ || !world_->effects)
		return;
	const Param *animParam = sc.find("anim");
	int32_t effectAnim = -1;
	if (!animParam || !paramI(sc, "anim", 0, effectAnim))
		return;

	FightEffects::Spawn spawn;
	spawn.animNo = effectAnim;
	spawn.shared = animParam->prefix == 'F';
	spawn.animations = &anims_;
	spawn.renderer = renderer_.get();
	spawn.owner = playerNo;
	paramF(sc, "velocity", 0, spawn.velocityX);
	paramF(sc, "velocity", 1, spawn.velocityY);
	paramF(sc, "accel", 0, spawn.accelX);
	paramF(sc, "accel", 1, spawn.accelY);
	spawn.velocityX /= localScale;
	spawn.velocityY /= localScale;
	spawn.accelX /= localScale;
	spawn.accelY /= localScale;
	paramF(sc, "scale", 0, spawn.scaleX);
	paramF(sc, "scale", 1, spawn.scaleY);
	paramI(sc, "removetime", 0, spawn.removeTime);
	paramI(sc, "sprpriority", 0, spawn.priority);
	paramI(sc, "layerno", 0, spawn.layer);

	float px = 0, py = 0, relativeFacing = 1;
	paramF(sc, "pos", 0, px);
	paramF(sc, "pos", 1, py);
	paramF(sc, "facing", 0, relativeFacing);
	px /= localScale;
	py /= localScale;
	const std::string postype = text::toLower(text::trim(sc.param("postype") ? *sc.param("postype") : "p1"));
	const Character *anchor = this;
	if (postype == "p2" && p2_)
		anchor = p2_;
	if (postype == "p1" || postype == "p2") {
		spawn.x = anchor->x + px * anchor->facing;
		spawn.y = anchor->y + py;
		spawn.moveFacing = anchor->facing;
		spawn.facing = anchor->facing * (relativeFacing < 0 ? -1 : 1);
	} else if (postype == "none") {
		spawn.x = px;
		spawn.y = py;
		spawn.facing = relativeFacing < 0 ? -1 : 1;
	} else {
		spawn.screen = true;
		spawn.y = py;
		if (postype == "left")
			spawn.x = -world_->halfWidth + px;
		else if (postype == "right")
			spawn.x = world_->halfWidth + px;
		else if (postype == "back")
			spawn.x = -facing * world_->halfWidth - px;
		else
			spawn.x = facing * world_->halfWidth + px; // front, and MUGEN's fallback
		spawn.moveFacing = postype == "back" ? facing : 1;
		const int baseFacing = postype == "back" ? facing : 1;
		spawn.facing = baseFacing * (relativeFacing < 0 ? -1 : 1);
	}

	if (!world_->effects->spawnExplod(spawn)) {
		logf("char: %s: no %s action %s%d for Explod (state %d)", name.c_str(), spawn.shared ? "shared" : "character",
			spawn.shared ? "F" : "", effectAnim, stateNo);
	} else if (!traceLabel.empty()) {
		logf("%s frame %u: state %d explod %s%d at %.2f,%.2f", traceLabel.c_str(), world_->gameTime, stateNo,
			spawn.shared ? "F" : "", effectAnim, spawn.x, spawn.y);
	}
}

// --- A frame ---------------------------------------------------------------------------------------------------------

void Character::turnAndInput(const InputState &in)
{
	// Characters with control turn to face the opponent while standing, crouching, walking, or landing.
	if (ctrl && (stateNo == 0 || stateNo == 11 || stateNo == 20 || (stateNo == 52 && anim.animTime() == 0)))
		autoTurn();
	// At time over and before the win poses the controller is ignored.
	if (world_ && world_->inputWait) {
		commands.reset();
		commands.update(InputState{}, facing);
	} else {
		commands.update(in, facing);
	}
}

// MUGEN's built-in moves, the ones no state file asks for: guard, jump, crouch, stand up, walk, stop walking.
void Character::actionPrepare()
{
	// State changes made here wait until actionRun (Ikemen runs this phase as "minus 3").
	minus_ = 3;
	const InputBuffer &b = commands.buffer();
	auto held = [&](Key k) { return b.state(k, false, false, true) > 0; };
	if (ctrl) {
		if (guarding_ && inGuardDist_ && !inGuardState() && stateType != 'L' && held(Key::B)) {
			changeState(120, -1, -1);
		} else if (stateType == 'S' && held(Key::U)) {
			if (stateNo != 40)
				changeState(40, -1, -1);
		} else if (stateType == 'S' && held(Key::D)) {
			if (stateNo != 10) {
				if (stateNo != 100)
					vx = 0;
				changeState(10, -1, -1);
			}
		} else if (stateType == 'C' && !held(Key::D)) {
			if (stateNo != 12)
				changeState(12, -1, -1);
		} else if (!(special_ & Special::NoWalk) && stateType == 'S' &&
			held(Key::F) != (!inGuardDist_ && held(Key::B))) {
			// Holding back near an attack doesn't walk away: it waits to guard.
			if (stateNo != 20)
				changeState(20, -1, -1);
		}
	}
	// Walking stops when neither or both of back and forward are held, control or not.
	if (stateNo == 20 && held(Key::B) == held(Key::F))
		changeState(0, -1, -1);
	if (stateType != 'A')
		airJumpCount_ = 0;
	// What controllers set for one frame lasts through a hitpause.
	if (!hitPause()) {
		special_ = 0;
		posFreeze_ = false;
		screenBound_ = true;
		playerPush_ = true;
		widthFront_ = widthBack_ = 0;
		for (HitBySlot &s : hitBy_) {
			if (s.time > 0 && --s.time == 0)
				s = HitBySlot{};
		}
		for (HitOverride &s : hitOverride_) {
			if (s.time > 0 && --s.time == 0)
				s = HitOverride{};
		}
	}
	if (unhittableTime_ > 0)
		unhittableTime_--;
	// The opponent stops being a target once it's no longer being hit.
	if (targetP2_ && p2_ && p2_->moveType != 'H' && !p2_->stchtmp_) {
		if (p2_->ghv_.type != HitType::None)
			p2_->ghv_.hitid = -1;
		targetP2_ = false;
	}
	minus_ = 0;
}

void Character::actionRun()
{
	for (int32_t neg : { -3, -2, -1 }) {
		minus_ = neg;
		if (const StateDef *s = states_.find(neg))
			runState(*s, negPersist_[neg]);
	}
	minus_ = 0;
	stateChange2();
	runState(*state_, persist_);

	// Guarding: holding back with control (or already guarding), where the state type allows it. Close to an attack,
	// that starts the guard states (common1.cns 120).
	const bool canGuardType = (stateType == 'S' && !(special_ & Special::NoStandGuard)) ||
		(stateType == 'C' && !(special_ & Special::NoCrouchGuard)) ||
		(stateType == 'A' && !(special_ & Special::NoAirGuard));
	guarding_ = (((ctrl || stateNo == 52) && moveType == 'I') || inGuardState()) && backHeld() && canGuardType;
	if (ctrl && inGuardDist_ && guarding_ && !inGuardState())
		changeState(120, -1, -1);

	// A hit freezes movement and time too.
	if (!hitPause()) {
		// Lying down (5110) gets up once the down time runs out, after at least a frame.
		if (stateNo == 5110 && stateTime >= 1 && ghv_.downRecovertime <= 0 && life > 0)
			changeState(5120, -1, -1);
		// A guard's end (140) returns to standing, crouching or the air once its animation is done.
		while (stateNo == 140 && stateTime >= anim.totaltime) {
			const int32_t before = stateNo;
			changeState(stateType == 'C' ? 11 : stateType == 'A' ? 51 : 0, -1, -1);
			if (stateNo == before)
				break;
		}
		posUpdate();
		// Touching down from the air lands (state 52).
		if (physics == 'A' && vy > 0 && y >= 0 && stateNo != 105)
			changeState(52, -1, -1);
		setFacing(p1facing_);
		p1facing_ = 0;
		stateTime++;
		if (moveContactTime_ > 0)
			moveContactTime_++;
	}

	// Damage taken last frame lands now, once the get-hit state has begun.
	if (ghv_.damage != 0) {
		if (moveType == 'H' || ghv_.keepstate)
			life = std::max(0, life - ghv_.damage);
		ghv_.damage = 0;
	}
	ghv_.keepstate = false;
	if (!hitPause()) {
		if (moveType == 'H') {
			if (ghv_.hitshaketime > 0)
				ghv_.hitshaketime--;
			if (ghv_.fallflag)
				ghv_.fallTime++;
		} else {
			// Back on its feet: the combo is over.
			if (hitState_ > 0)
				hitState_ = 0;
			ghv_.hitshaketime = 0;
			ghv_.fallflag = false;
			ghv_.fallcount = 0;
			ghv_.hitid = ghv_.hitid < 0 ? -1 : 0;
			ghv_.guardcount = 0;
			ghv_.downRecovertime = liedownTime_;
			fallDefenseMul_ = 1;
		}
		if (ghv_.hitshaketime <= 0 && ghv_.hittime >= 0)
			ghv_.hittime--;
		if (ghv_.downRecovertime > 0 && stateNo == 5110)
			ghv_.downRecovertime--;
	}
	ranFree_ = !hitPause();

	// Falls (Ikemen's actionFinish): each landing from a fall (5070 tripped, 5100 hitting the ground) raises defence
	// and counts; landing again halves the time spent lying down, and a short one is invincible.
	if (moveType == 'H') {
		hitState_ = ghv_.fallflag ? 2 : 1;
		if (ranFree_ && (stateNo == 5070 || stateNo == 5100) && stateTime == 1) {
			fallDefenseMul_ *= fallDefenceMul_;
			ghv_.fallcount++;
			if (ghv_.fallcount > 1 && stateNo == 5100) {
				if (ghv_.downRecovertime > 0)
					ghv_.downRecovertime /= 2;
				if (ghv_.downRecovertime <= 10) {
					// Field by field: as one struct constant GCC builds it with rldic, which Xenia skips.
					HitBySlot &s = hitBy_[0];
					s.notHitBy = true;
					s.attrAttack = 0;
					s.time = 180;
					s.attrState = 1 | 2 | 4;
				}
			}
		}
	}

	// Power from this frame's hits.
	if (powerGain_ != 0) {
		power = std::clamp(power + powerGain_, 0, powerMax());
		powerGain_ = 0;
	}

	// Whoever this character holds follows it.
	if (Character *t = target(); t && t->bindTo_ == this)
		t->updateBinding();

	// KO (Ikemen's actionFinish): out of life, it falls (5030) unless already being hit, which ends in a fall anyway.
	if (!hitPause() && !ko_ && life <= 0 && !(special_ & Special::NoKo)) {
		ko_ = true;
		ctrl = false;
		if (!traceLabel.empty())
			logf("%s frame %u: KO", traceLabel.c_str(), world_ ? world_->gameTime : 0);
		if (!stchtmp_ && moveType != 'H') {
			ghv_.fallflag = true;
			minus_ = 1;
			changeState(5030, -1, -1, this);
			minus_ = 0;
			stateTime = 1;
		}
	}
	// Done for the round: lying KO'd (5150), until it's back up with life in a round still going.
	if (!ko_ && life > 0 && !(world_ && world_->roundEnded))
		overAlive_ = overKo_ = false;
	if (stateNo == 5150)
		overKo_ = true;
}

void Character::setFacing(int f)
{
	if (f != 0 && f != facing) {
		facing = -facing;
		vx = -vx;
	}
}

void Character::posUpdate()
{
	// Corner push moves the attacker away while the target is pinned to the screen edge. PosFreeze does not suppress
	// it in MUGEN; the stored velocity keeps decaying even after the target leaves the corner.
	if (cornerPushVel_ != 0) {
		const bool targetAtCorner = world_ && p2_ && p2_->screenBound_ &&
			(facing > 0 ? p2_->x >= world_->camX + world_->screenEdge - 0.5f
						: p2_->x <= world_->camX - world_->screenEdge + 0.5f);
		if (targetAtCorner && !(special_ & Special::NoCornerPush))
			x += cornerPushVel_ / localScale;
		cornerPushVel_ *= cornerPushMul_;
		if (std::fabs(cornerPushVel_) < 1.0f / localScale)
			cornerPushVel_ = 0;
	}
	if (!posFreeze_) {
		x += vx * facing;
		y += vy;
	}
	switch (physics) {
	case 'S':
		vx *= standFriction_;
		if (std::fabs(vx) < 1)
			vx = 0;
		break;
	case 'C': vx *= crouchFriction_; break;
	case 'A': vy += yaccel_; break;
	default: break;
	}
}

void Character::tick()
{
	makeDustSpacing_++;
	if (palfx_.time > 0) {
		palfx_.time--;
		palfx_.elapsed++;
	}
	if (afterImage_.time != 0) {
		if (afterImage_.recordGap <= 0) {
			afterImage_.frames.insert(afterImage_.frames.begin(), AfterImageFrame{ anim, x, y, facing });
			if ((int32_t)afterImage_.frames.size() > afterImage_.length)
				afterImage_.frames.resize((size_t)afterImage_.length);
			afterImage_.recordGap = afterImage_.timeGap;
		}
		afterImage_.recordGap--;
		if (afterImage_.time > 0)
			afterImage_.time--;
		if (afterImage_.time == 0)
			afterImage_.frames.clear();
	}
	// The frame a hit lands still animates: the pause starts next frame.
	if (ranFree_ || !hitPause())
		anim.action();
	// A HitDef that hits once is spent on contact.
	if (hitdefContact_) {
		if (hitdef_.hitonce != 0)
			hitdef_.active = false;
		hitdefContact_ = false;
	}
	if (moveContactTime_ < 0) {
		moveContactTime_ = 1;
		if (moveContactType_ == 'H')
			hitCount_ += hitdef_.numhits;
		else
			guardCount_ += hitdef_.numhits;
	}
	// A hold runs out, or ends when the holder is hit: the held character falls (5050).
	if (bindTime_ > 0) {
		if (isTargetBound()) {
			if (bindTo_->gotHit_) {
				minus_ = 1;
				changeState(5050, -1, -1, this);
				minus_ = 0;
				resetBind();
			} else {
				bindTime_--;
			}
		} else if (--bindTime_ == 0) {
			resetBind();
		}
	}
	// Hit: into the get-hit states, which wait for actionRun (common1.cns 5000 standing, 5010 crouching, 5020 in the
	// air, 5070 tripped, 5080 lying down).
	if (gotHit_ && !ghv_.keepstate) {
		moveType = 'H';
		hitPauseTime_ = 0;
		// After actionRun, as in Ikemen: the change waits for the next frame and doesn't turn the character.
		minus_ = 1;
		const bool customState = stchtmp_;
		if (customState) {
			// Hit into the attacker's state (p2stateno): MUGEN reports where a plain hit would have gone.
			prevStateNo = stateType == 'L' && y == 0 ? 5080
				: ghv_.type == HitType::Trip ? 5070
				: stateType == 'S'           ? 5000
				: stateType == 'C'           ? 5010
											 : 5020;
		} else if (ghv_.guarded && ghv_.damage < life) {
			// Blocked: the guard shake (150 standing, 152 crouching, 154 in the air).
			changeState(stateType == 'S' ? 150 : stateType == 'C' ? 152 : 154, -1, 0, this);
		} else if (stateType == 'L' && y == 0) {
			changeState(5080, -1, 0, this);
		} else if (ghv_.type == HitType::Trip) {
			changeState(5070, -1, 0, this);
		} else {
			if (ghv_.forcestand && stateType == 'C')
				stateType = 'S';
			else if (ghv_.forcecrouch && stateType == 'S')
				stateType = 'C';
			// A KO knocks a crouching character up standing.
			changeState(stateType == 'S' || (stateType == 'C' && ghv_.damage >= life) ? 5000
					: stateType == 'C'                                                  ? 5010
																						: 5020,
				-1, 0, this);
		}
		minus_ = 0;
	}
	if (hitPauseTime_ > 0)
		hitPauseTime_--;
}

void Character::body(float &left, float &right, float &top) const
{
	const bool air = stateType == 'A';
	const float front = ((air ? airFront_ : groundFront_) + widthFront_) / localScale;
	const float back = ((air ? airBack_ : groundBack_) + widthBack_) / localScale;
	left = facing > 0 ? x - back : x - front;
	right = facing > 0 ? x + front : x + back;
	top = y - height_ / localScale;
}

void Character::draw(const View &view, float groundY, int shadowIntensity, float shadowYScale)
{
	if ((!renderer_ && !animShared_) || (special_ & Special::Invisible))
		return;
	auto drawAnim = [&](float px, float py, SpriteDraw how = {}) {
		if (animShared_) {
			if (world_ && world_->effects)
				world_->effects->drawSharedAnimation(anim, px, py, view.scale / localScale, facing, how);
		} else {
			renderer_->drawAnimation(anim, px, py, view.scale / localScale, facing, how);
		}
	};
	// Shaking from a hit: a pixel back every other frame.
	const float shake = ghv_.hitshaketime > 0 && (stateTime & 1) ? -(float)facing : 0;
	const float sx = view.screenX(x + shake - view.camX);
	if (shadowIntensity > 0 && !(special_ & Special::NoShadow)) {
		// The shadow lies on the ground under the character, the sprite flattened by the stage's shadow yscale: a
		// positive one falls toward the viewer (drawn upside down), a negative one into the screen.
		SpriteDraw shadow;
		shadow.shadow = shadowIntensity;
		shadow.scaleY = -shadowYScale;
		drawAnim(sx, view.screenY(groundY - view.camY), shadow);
	}
	// MUGEN 1.0 afterimages sample old animation and position frames. Each older visible sample receives the next
	// PalBright/PalContrast step, as in Ikemen's AfterImage setup.
	if (!animShared_) {
		int visible = 0;
		for (size_t n = (size_t)afterImage_.frameGap - 1; n < afterImage_.frames.size(); n += afterImage_.frameGap) {
			const AfterImageFrame &old = afterImage_.frames[n];
			SpriteDraw how;
			how.trans = afterImage_.trans;
			how.srcAlpha = 255;
			for (int k = 0; k < 3; k++) {
				int add = afterImage_.bright[k];
				float mul = (float)afterImage_.contrast[k];
				for (int step = 0; step < visible; step++) {
					add += afterImage_.add[k];
					mul *= afterImage_.mul[k];
				}
				(&how.addR)[k] = (int16_t)std::clamp(add, -255, 255);
				(&how.mulR)[k] = (uint16_t)std::clamp((int)mul, 0, 1024);
			}
			const float oldX = view.screenX(old.x - view.camX);
			const float oldY = view.screenY(groundY + old.y - view.camY);
			renderer_->drawAnimation(old.anim, oldX, oldY, view.scale / localScale, old.facing, how);
			visible++;
		}
	}
	SpriteDraw colour;
	if (palfx_.time > 0) {
		const float wave = palfx_.cycle > 0
			? std::sin(6.28318530718f * palfx_.elapsed / palfx_.cycle)
			: 0;
		for (int k = 0; k < 3; k++) {
			(&colour.addR)[k] = (int16_t)std::clamp(palfx_.add[k] + (int)std::lround(palfx_.sinAdd[k] * wave), -255, 255);
			(&colour.mulR)[k] = (uint16_t)std::clamp(palfx_.mul[k], 0, 1024);
		}
	}
	drawAnim(sx, view.screenY(groundY + y - view.camY), colour);
}

void Character::drawBoxes(const View &view, float groundY) const
{
	for (int kind : { 2, 1 }) {
		const std::vector<Clsn> *list = boxes(kind);
		if (!list)
			continue;
		// Attack boxes red, hurt boxes blue; brighter while a HitDef is live.
		const uint32_t rgb = kind == 1 ? (hitdef_.active ? 0xFF2020 : 0x802020) : 0x3060FF;
		for (const Clsn &b : *list) {
			float l = b[0], r = b[2];
			if (facing < 0) {
				l = -b[2];
				r = -b[0];
			}
			const float x0 = view.screenX(x + std::min(l, r) / localScale - view.camX);
			const float x1 = view.screenX(x + std::max(l, r) / localScale - view.camX);
			const float y0 = view.screenY(groundY + y + std::min(b[1], b[3]) / localScale - view.camY);
			const float y1 = view.screenY(groundY + y + std::max(b[1], b[3]) / localScale - view.camY);
			const float w = std::max(1.0f, x1 - x0), h = std::max(1.0f, y1 - y0);
			platform::fillRect(x0, y0, w, h, 0x30000000 | rgb);
			platform::fillRect(x0, y0, w, 1, 0xE0000000 | rgb);
			platform::fillRect(x0, y0 + h - 1, w, 1, 0xE0000000 | rgb);
			platform::fillRect(x0, y0, 1, h, 0xE0000000 | rgb);
			platform::fillRect(x0 + w - 1, y0, 1, h, 0xE0000000 | rgb);
		}
	}
}

// --- Hits ------------------------------------------------------------------------------------------------------------

namespace {

// hitflag / guardflag letters (Ikemen's hitFlagMask).
int32_t hitFlagMask(const std::string &s)
{
	int32_t f = 0;
	for (char c : s) {
		switch (std::tolower((unsigned char)c)) {
		case 'h': f |= HitFlag::High; break;
		case 'l': f |= HitFlag::Low; break;
		case 'm': f |= HitFlag::High | HitFlag::Low; break;
		case 'a': f |= HitFlag::Air; break;
		case 'f': f |= HitFlag::Fall; break;
		case 'd': f |= HitFlag::Down; break;
		case 'p': f |= HitFlag::P; break;
		case '-': f |= HitFlag::Minus; break;
		case '+': f |= HitFlag::Plus; break;
		default: break;
		}
	}
	return f;
}

// "S, NA, SA": the state types, then attack classes, as bits (see ExprContext::hitDefAttr). A HitDef takes one state
// type (the last given); HitBy and NotHitBy take all.
void parseAttr(const std::string &s, int32_t &state, int32_t &attack, bool hitdef)
{
	state = attack = 0;
	const std::vector<std::string> parts = text::split(s, ',');
	if (parts.empty())
		return;
	for (char c : text::toLower(text::trim(parts[0]))) {
		const int32_t bit = c == 's' ? 1 : c == 'c' ? 2 : c == 'a' ? 4 : 0;
		if (bit)
			state = hitdef ? bit : state | bit;
	}
	for (size_t i = 1; i < parts.size(); i++) {
		const std::string a = text::toLower(text::trim(parts[i]));
		if (a.empty())
			continue;
		// "N", "S", "H" (or "A", hyper) alone: every kind of that class.
		const int cls = a[0] == 'n' ? 0 : a[0] == 's' ? 1 : a[0] == 'h' ? 2 : a[0] == 'a' ? (a.size() == 1 ? 2 : 3) : -1;
		if (cls < 0)
			continue;
		const int kind = a.size() < 2 ? -1 : a[1] == 'a' ? 0 : a[1] == 't' ? 1 : a[1] == 'p' ? 2 : -2;
		if (kind == -2)
			continue;
		for (int c = 0; c < 3; c++) {
			if (cls != 3 && cls != c)
				continue;
			for (int k = 0; k < 3; k++) {
				if (kind < 0 || kind == k)
					attack |= 1 << (c * 3 + k);
			}
		}
	}
}

Reaction reactionOf(const std::string *s, Reaction fallback)
{
	const std::string t = s ? text::toLower(text::trim(*s)) : "";
	if (t.empty())
		return fallback;
	switch (t[0]) {
	case 'l': return Reaction::Light;
	case 'm': return Reaction::Medium;
	case 'h': return Reaction::Hard;
	case 'b': return Reaction::Back;
	case 'u': return Reaction::Up;
	case 'd': return Reaction::Diagup;
	default: return fallback;
	}
}

HitType hitTypeOf(const std::string *s, HitType fallback)
{
	const std::string t = s ? text::toLower(text::trim(*s)) : "";
	if (t.empty())
		return fallback;
	switch (t[0]) {
	case 'h': return HitType::High;
	case 'l': return HitType::Low;
	case 't': return HitType::Trip;
	case 'n': return HitType::None;
	default: return fallback;
	}
}

// "S5, 0" or "5": the numbers, and 'S' for the character's own files or 'F' for the shared ones.
void resource(const std::string *s, int32_t *out, size_t count, char &prefix)
{
	if (!s)
		return;
	char p = 0;
	const std::vector<std::string> parts = text::split(resourcePrefix(*s, p), ',');
	prefix = p == 'S' ? 'S' : 'F';
	for (size_t i = 0; i < count && i < parts.size(); i++) {
		if (!text::trim(parts[i]).empty())
			out[i] = text::atoi(parts[i]);
	}
}

} // namespace

// The HitDef controller: a fresh HitDef from its parameters, ready to hit (Ikemen's hitDef.Run, runSub).
void Character::runHitDef(const StateController &sc)
{
	HitDef hd;
	hd.guardDist[0] = hitdef_.guardDist[0];
	hd.guardDist[1] = hitdef_.guardDist[1];
	hd.sparkno = (int32_t)constF("data.sparkno", 2);
	hd.guardSparkno = (int32_t)constF("data.guard.sparkno", 40);

	if (const std::string *attr = sc.param("attr"))
		parseAttr(*attr, hd.attrState, hd.attrAttack, true);
	if (const std::string *f = sc.param("hitflag"))
		hd.hitflag = hitFlagMask(*f);
	if (const std::string *f = sc.param("guardflag"))
		hd.guardflag = hitFlagMask(*f);
	hd.groundType = hitTypeOf(sc.param("ground.type"), hd.groundType);
	hd.airType = hitTypeOf(sc.param("air.type"), hd.airType);
	hd.animtype = reactionOf(sc.param("animtype"), hd.animtype);
	hd.airAnimtype = reactionOf(sc.param("air.animtype"), hd.airAnimtype);
	hd.fallAnimtype = reactionOf(sc.param("fall.animtype"), hd.fallAnimtype);
	if (const std::string *p = sc.param("priority")) {
		// "3, Hit": a number, then Hit / Miss / Dodge.
		const std::vector<std::string> parts = text::split(*p, ',');
		if (!parts.empty())
			hd.priority = text::atoi(parts[0]);
		if (parts.size() > 1) {
			const std::string t = text::toLower(text::trim(parts[1]));
			if (!t.empty() && std::strchr("hmd", t[0]))
				hd.priorityType = (char)std::toupper((unsigned char)t[0]);
		}
	}
	resource(sc.param("sparkno"), &hd.sparkno, 1, hd.sparkPrefix);
	resource(sc.param("guard.sparkno"), &hd.guardSparkno, 1, hd.guardSparkPrefix);
	resource(sc.param("hitsound"), hd.hitsound, 2, hd.hitsoundPrefix);
	resource(sc.param("guardsound"), hd.guardsound, 2, hd.guardsoundPrefix);

	int32_t i = 0;
	float f = 0;
	auto setI = [&](const char *n, int32_t &out) {
		if (paramI(sc, n, 0, i))
			out = i;
	};
	auto setB = [&](const char *n, bool &out) {
		if (paramI(sc, n, 0, i))
			out = i != 0;
	};
	auto setF = [&](const char *n, float &out) {
		if (paramF(sc, n, 0, f))
			out = f;
	};
	auto setI2 = [&](const char *n, int32_t *out) {
		for (size_t k = 0; k < 2; k++) {
			if (paramI(sc, n, k, i))
				out[k] = i;
		}
	};
	auto setF2 = [&](const char *n, float *out) {
		for (size_t k = 0; k < 2; k++) {
			if (paramF(sc, n, k, f))
				out[k] = f;
		}
	};
	setI("id", hd.id);
	if (hd.id < 0)
		hd.id = 0;
	setI("chainid", hd.chainid);
	setB("kill", hd.kill);
	setB("guard.kill", hd.guardKill);
	setB("fall.kill", hd.fallKill);
	setB("keepstate", hd.keepstate);
	if (paramI(sc, "hitonce", 0, i))
		hd.hitonce = i != 0 ? 1 : 0;
	setI("damage", hd.hitDamage);
	if (paramI(sc, "damage", 1, i))
		hd.guardDamage = i;
	setI("numhits", hd.numhits);
	// "getpower = hit, guard", "givepower = hit, guard".
	for (const auto &[n, hit, guard] : { std::tuple{ "getpower", &hd.hitGetPower, &hd.guardGetPower },
			 std::tuple{ "givepower", &hd.hitGivePower, &hd.guardGivePower } }) {
		if (const std::string *t = sc.param(n)) {
			const std::vector<std::string> parts = text::split(*t, ',');
			if (!parts.empty() && !text::trim(parts[0]).empty())
				*hit = text::atoi(parts[0]);
			if (parts.size() > 1 && !text::trim(parts[1]).empty())
				*guard = text::atoi(parts[1]);
		}
	}
	setI("p1stateno", hd.p1stateno);
	if (paramI(sc, "p2stateno", 0, i)) {
		hd.p2stateno = i;
		hd.p2getp1state = true;
	}
	if (const std::string *t = sc.param("p2getp1state"))
		hd.p2getp1state = text::atoi(*t) != 0;
	if (!sc.find("p1sprpriority"))
		setI("sprpriority", hd.p1sprpriority);
	setI("p1sprpriority", hd.p1sprpriority);
	setI("p2sprpriority", hd.p2sprpriority);
	if (paramI(sc, "forcestand", 0, i))
		hd.forcestand = i != 0;
	if (paramI(sc, "forcecrouch", 0, i))
		hd.forcecrouch = i != 0;
	setB("forcenofall", hd.forcenofall);
	setI("fall.damage", hd.fallDamage);
	setF("fall.xvelocity", hd.fallXvel);
	setF("fall.yvelocity", hd.fallYvel);
	setB("fall.recover", hd.fallRecover);
	setI("fall.recovertime", hd.fallRecovertime);
	setI("fall.envshake.time", hd.fallEnvShakeTime);
	setI("fall.envshake.ampl", hd.fallEnvShakeAmpl);
	setF("fall.envshake.freq", hd.fallEnvShakeFreq);
	setF("fall.envshake.phase", hd.fallEnvShakePhase);
	setF("fall.envshake.mul", hd.fallEnvShakeMul);
	setF("fall.envshake.dir", hd.fallEnvShakeDir);
	setF("fall.envshake.diradd", hd.fallEnvShakeDirAdd);
	setF("fall.envshake.decay", hd.fallEnvShakeDecay);
	setI("envshake.time", hd.envShakeTime);
	setI("envshake.ampl", hd.envShakeAmpl);
	setF("envshake.freq", hd.envShakeFreq);
	setF("envshake.phase", hd.envShakePhase);
	setF("envshake.mul", hd.envShakeMul);
	setF("envshake.dir", hd.envShakeDir);
	setF2("sparkxy", hd.sparkxy);
	setI("down.hittime", hd.downHittime);
	setI("p1facing", hd.p1facing);
	setI("p1getp2facing", hd.p1getp2facing);
	setF2("mindist", hd.mindist);
	setF2("maxdist", hd.maxdist);
	setI("p2facing", hd.p2facing);
	setI("air.hittime", hd.airHittime);
	setB("fall", hd.groundFall);
	if (paramI(sc, "air.fall", 0, i))
		hd.airFall = i != 0;
	setB("down.bounce", hd.downBounce);
	setF2("down.velocity", hd.downVel);
	setI("ground.hittime", hd.groundHittime);
	setI("guard.hittime", hd.guardHittime);
	// MUGEN ignores a negative guard distance rather than clamping it.
	for (const char *n : { "guard.dist", "guard.dist.width" }) {
		for (size_t k = 0; k < 2; k++) {
			if (paramF(sc, n, k, f) && f >= 0)
				hd.guardDist[k] = f;
		}
	}
	setI2("pausetime", hd.pausetime);
	setI2("guard.pausetime", hd.guardPausetime);
	setF2("air.velocity", hd.airVel);
	setF2("airguard.velocity", hd.airguardVel);
	setI("ground.slidetime", hd.groundSlidetime);
	setI("guard.slidetime", hd.guardSlidetime);
	setI("guard.ctrltime", hd.guardCtrltime);
	setI("airguard.ctrltime", hd.airguardCtrltime);
	setF2("ground.velocity", hd.groundVel);
	setF2("guard.velocity", hd.guardVel);
	setF("ground.cornerpush.veloff", hd.groundCornerpushVeloff);
	setF("air.cornerpush.veloff", hd.airCornerpushVeloff);
	setF("down.cornerpush.veloff", hd.downCornerpushVeloff);
	setF("guard.cornerpush.veloff", hd.guardCornerpushVeloff);
	setF("airguard.cornerpush.veloff", hd.airguardCornerpushVeloff);
	setF("ground.cornerpush.velmul", hd.groundCornerpushVelmul);
	setF("air.cornerpush.velmul", hd.airCornerpushVelmul);
	setF("down.cornerpush.velmul", hd.downCornerpushVelmul);
	setF("guard.cornerpush.velmul", hd.guardCornerpushVelmul);
	setF("airguard.cornerpush.velmul", hd.airguardCornerpushVelmul);
	setF("xaccel", hd.xaccel);
	setF("yaccel", hd.yaccel);
	setI2("unhittabletime", hd.unhittabletime);

	hd.active = true;
	hd.finalize(mugen1_);
	hitdef_ = hd;
}

// ReversalDef is a HitDef whose attack boxes catch matching enemy attack boxes. KFM uses only the original MUGEN
// parameters here; the result changes P1's state without putting the attacker into a get-hit state.
void Character::runReversalDef(const StateController &sc)
{
	HitDef hd;
	hd.sparkno = (int32_t)constF("data.sparkno", 2);
	if (const std::string *attr = sc.param("reversal.attr"))
		parseAttr(*attr, hd.reversalAttrState, hd.reversalAttrAttack, false);
	int32_t i = 0;
	float f = 0;
	for (size_t k = 0; k < 2; k++) {
		if (paramI(sc, "pausetime", k, i)) hd.pausetime[k] = i;
		if (paramF(sc, "sparkxy", k, f)) hd.sparkxy[k] = f;
	}
	resource(sc.param("sparkno"), &hd.sparkno, 1, hd.sparkPrefix);
	resource(sc.param("hitsound"), hd.hitsound, 2, hd.hitsoundPrefix);
	if (paramI(sc, "p1stateno", 0, i)) hd.p1stateno = i;
	if (paramI(sc, "p1sprpriority", 0, i)) hd.p1sprpriority = i;
	if (paramI(sc, "p2sprpriority", 0, i)) hd.p2sprpriority = i;
	hd.hitonce = 1;
	hd.active = hd.reversalAttrAttack != 0;
	hd.finalize(mugen1_);
	hitdef_ = hd;
	if (!traceLabel.empty())
		logf("%s frame %u: state %d reversal %s", traceLabel.c_str(), world_ ? world_->gameTime : 0, stateNo,
			hd.active ? "armed" : "cleared");
}

// HitOverride chooses one of eight slots. An empty attr leaves a live timer that matches nothing, which is how KFM
// explicitly clears its blocking window.
void Character::runHitOverride(const StateController &sc)
{
	int32_t slot = 0, time = 1, stateno = -1;
	paramI(sc, "slot", 0, slot);
	paramI(sc, "time", 0, time);
	paramI(sc, "stateno", 0, stateno);
	if (slot < 0 || slot >= 8)
		slot = 0;
	if (time < -1 || time == 0)
		time = 1;
	HitOverride &out = hitOverride_[slot];
	out = HitOverride{};
	out.time = time;
	out.stateno = stateno;
	if (const std::string *attr = sc.param("attr"))
		parseAttr(*attr, out.attrState, out.attrAttack, false);
	if (!traceLabel.empty())
		logf("%s frame %u: state %d hitoverride slot %d %s", traceLabel.c_str(), world_ ? world_->gameTime : 0,
			stateNo, slot, out.attrAttack ? "armed" : "cleared");
}

const std::vector<Clsn> *Character::boxes(int kind) const
{
	const AnimFrame *f = anim.curFrame();
	if (!f)
		return nullptr;
	const std::vector<Clsn> &b = kind == 1 ? f->clsn1 : f->clsn2;
	return b.empty() ? nullptr : &b;
}

bool Character::clsnHits(const Character &g) const
{
	return clsnTouch(g, 1, 2);
}

bool Character::reversalMatches(const Character &attacker) const
{
	if (!hitdef_.active || !hitdef_.isReversal() || !attacker.hitdef_.active || attacker.hitdef_.isReversal())
		return false;
	return hitdef_.reverses(attacker.stateType, attacker.hitdef_.attrAttack);
}

// Whether boxes of this character's touch the other's, in stage space (Ikemen's clsnCheckSingle and clsnOverlap;
// touching edges count).
bool Character::clsnTouch(const Character &g, int myKind, int theirKind) const
{
	const std::vector<Clsn> *a = boxes(myKind), *b = g.boxes(theirKind);
	if (!a || !b)
		return false;
	auto place = [](const Character &c, const Clsn &box, float out[4]) {
		float l = box[0], r = box[2];
		if (c.facing < 0) {
			l = -box[2];
			r = -box[0];
		}
		out[0] = c.x + std::min(l, r) / c.localScale;
		out[2] = c.x + std::max(l, r) / c.localScale;
		out[1] = c.y + std::min(box[1], box[3]) / c.localScale;
		out[3] = c.y + std::max(box[1], box[3]) / c.localScale;
	};
	for (const Clsn &boxA : *a) {
		float ra[4];
		place(*this, boxA, ra);
		for (const Clsn &boxB : *b) {
			float rb[4];
			place(g, boxB, rb);
			if (std::min(ra[2], rb[2]) - std::max(ra[0], rb[0]) >= 0 &&
				std::min(ra[3], rb[3]) - std::max(ra[1], rb[1]) >= 0)
				return true;
		}
	}
	return false;
}

// Whether this character can be hit by that HitDef at all, in its state now (Ikemen's attrCheck).
bool Character::attrCheck(const HitDef &hd, char attackerStateType) const
{
	if (unhittableTime_ > 0 || !hd.active)
		return false;
	if (hd.chainid >= 0 && ghv_.hitid != hd.chainid)
		return false;
	const int32_t f = hd.hitflag;
	if ((!(f & HitFlag::High) && stateType == 'S') || (!(f & HitFlag::Low) && stateType == 'C') ||
		(!(f & HitFlag::Air) && stateType == 'A') || (!(f & HitFlag::Down) && stateType == 'L'))
		return false;
	// F: may hit someone falling; -: only someone not being hit; +: only someone being hit.
	if (!(f & HitFlag::Fall) && hitState_ >= 2)
		return false;
	if ((f & HitFlag::Minus) && hitState_ > 0)
		return false;
	if ((f & HitFlag::Plus) && (hitState_ <= 0 || inGuardState()))
		return false;
	// Invincibility (HitBy / NotHitBy): MUGEN checks the attacker's actual state type, not the HitDef's.
	const int32_t attackerType = attackerStateType == 'S' ? 1 : attackerStateType == 'C' ? 2
		: attackerStateType == 'A'                         ? 4
		: attackerStateType == 'L'                         ? 8
														   : hd.attrState;
	return hitBySlotsAllow(attackerType, hd.attrAttack);
}

bool Character::hitBySlotsAllow(int32_t attrState, int32_t attrAttack) const
{
	for (const HitBySlot &s : hitBy_) {
		if (s.time == 0)
			continue;
		const bool stateMatch = (s.attrState & attrState) != 0, attackMatch = (s.attrAttack & attrAttack) != 0;
		// NotHitBy: anything it names gets through no more. HitBy: only what it names in full gets through.
		if (s.notHitBy ? (stateMatch || attackMatch) : !(stateMatch && attackMatch))
			return false;
	}
	return true;
}

bool Character::inGuardState() const
{
	return stateNo == 120 || (stateNo >= 130 && stateNo <= 132) || stateNo == 140 ||
		(stateNo >= 150 && stateNo <= 155);
}

bool Character::backHeld() const
{
	return commands.buffer().state(Key::B, false, false, true) > 0;
}

// Whether the attacker's HitDef wins against this character: always when this one isn't attacking, else by priority,
// equal priorities trading (Ikemen's hittableByChar).
bool Character::hittableBy(const Character &attacker) const
{
	const HitDef &theirs = attacker.hitdef_;
	if (!attrCheck(theirs, attacker.stateType))
		return false;
	if (hitdef_.hitOpponent)
		return true;
	if (moveType == 'I' || !hitdef_.active || stateType == 'L')
		return true;
	// Whether this character's own attack would land on the attacker too.
	auto counter = [&] {
		return !theirs.hitOpponent && attacker.attrCheck(hitdef_, stateType) && clsnHits(attacker);
	};
	if (hitdef_.priority < theirs.priority)
		return true;
	if (hitdef_.priority > theirs.priority)
		return !counter();
	if (hitdef_.priorityType == 'D' || theirs.priorityType == 'D' || theirs.priorityType == 'M')
		return !counter();
	return true;
}

Reaction Character::gethitAnimtype() const
{
	if (ghv_.fallflag)
		return ghv_.fallAnimtype;
	if (stateType == 'A')
		return ghv_.airAnimtype;
	// Back, up and diagonal-up only send someone flying; on the ground they're a hard hit.
	if (ghv_.groundAnimtype >= Reaction::Back && ghv_.yvel == 0)
		return Reaction::Hard;
	return ghv_.groundAnimtype;
}

int32_t Character::computeDamage(int32_t damageIn, bool kill, float attackMul, bool absolute) const
{
	if (damageIn == 0 || (!absolute && attackMul == 0))
		return 0;
	// Defence: the constant, DefenceMulSet's (once hit, for MUGEN characters), and what falls have added.
	const float custom = !defenseMulDelay_ || moveType == 'H' ? customDefense_ : 1;
	const double defence = constF("data.defence", 100) / 100.0 * custom * fallDefenseMul_;
	double damage = absolute ? damageIn : damageIn * (double)attackMul / defence;
	// However strong the defence, a hit takes at least 1.
	if (damage > 0 && damage < 1)
		damage = 1;
	if (damage > life)
		damage = life;
	if (!kill && damage >= life && life > 0)
		damage = life - 1;
	return (int32_t)std::lround(damage);
}

// This character's HitDef has touched the getter: hit (1), guarded (2), negative for a hit of type None, or 0 for
// no hit after all. Fills the getter's get-hit values and damage (Ikemen's hitResultCheck).
int Character::hitResultCheck(Character &g)
{
	const HitDef &hd = hitdef_;
	// The getter is already changing state (into another attacker's p2stateno, or of its own accord).
	if (g.stchtmp_ && (g.gotHit_ ? hd.p2stateno >= 0 : g.ranFree_))
		return 0;
	if (hd.p1stateno >= 0 && (gotHit_ || stchtmp_))
		return 0;
	// Thrown already this frame.
	if (g.gotHit_ && (g.ghv_.attrAttack & (1 << 1 | 1 << 4 | 1 << 7)))
		return 0;
	const bool inCombo = (g.moveType == 'H' || g.gotHit_) && !g.ghv_.guarded;
	const float attackMul = constF("data.attack", 100) / 100;

	// Blocked (2) when guarding the right way (guardflag: high standing, low crouching, air in the air), and the
	// guard damage won't KO; else hit (1).
	int result = 1;
	g.ghv_.kill = hd.kill;
	const bool canGuard = !(special_ & Special::Unguardable) && g.guarding_ && (!g.gotHit_ || g.ghv_.guarded);
	if (canGuard &&
		(((hd.guardflag & HitFlag::High) && g.stateType == 'S') || ((hd.guardflag & HitFlag::Low) && g.stateType == 'C') ||
			((hd.guardflag & HitFlag::Air) && g.stateType == 'A'))) {
		g.ghv_.kill = hd.guardKill;
		if (g.life > g.computeDamage(hd.guardDamage, hd.guardKill, attackMul))
			result = 2;
	}
	if (hd.isReversal())
		result = -result;
	else if (result > 0 && (g.stateType == 'A' ? hd.airType : hd.groundType) == HitType::None)
		result = -result;
	// Once a hit this frame will KO, later ones can't spare it.
	if (g.ghv_.damage >= g.life)
		g.ghv_.kill = true;

	// HitOverride takes priority over p2stateno and the common get-hit states.
	int overrideState = -1;
	for (const HitOverride &slot : g.hitOverride_) {
		if (slot.matches(stateType, hd.attrAttack)) {
			overrideState = slot.stateno;
			break;
		}
	}
	// p2stateno: the getter goes into a state of the attacker's files (p2getp1state, the default) or its own.
	bool p2s = false;
	if ((!g.stchtmp_ || !g.gotHit_) && overrideState >= 0) {
		g.minus_ = 1;
		g.stateChange1(overrideState, &g);
		g.minus_ = 0;
		g.ctrl = false;
		p2s = true;
	} else if ((!g.stchtmp_ || !g.gotHit_) && std::abs(result) == 1 && hd.p2stateno >= 0 && !hd.keepstate) {
		g.minus_ = 1;
		g.stateChange1(hd.p2stateno, hd.p2getp1state ? stateOwner_ : &g);
		g.minus_ = 0;
		g.moveType = 'H';
		g.ctrl = false;
		p2s = true;
	}
	hitdef_.hitOpponent = true;

	const bool ghvset = !g.gotHit_ || !g.stchtmp_ || p2s;
	GetHitVars &v = g.ghv_;
	if (ghvset) {
		v.hitid = hd.id;
		v.keepstate = hd.keepstate;
		v.groundType = hd.groundType;
		v.airType = hd.airType;
		v.type = g.stateType == 'A' ? v.airType : v.groundType;
		if (hd.p1sprpriority != NOT_SET)
			sprPriority = hd.p1sprpriority;
		g.sprPriority = hd.p2sprpriority;
	}
	// Which way the hit pushes: away from the attacker, unless it turns.
	int byf = facing;
	if (result == 1) {
		if (hd.p1getp2facing != 0)
			byf = hd.p1getp2facing < 0 ? -g.facing : g.facing;
		else if (hd.p1facing < 0)
			byf = -byf;
	}

	if (result > 0) {
		// Hitting someone this character holds lets them go.
		if (g.bindTo_ == this)
			g.resetBind();
		if (hd.keepstate && ghvset) {
			v.hitshaketime = std::max(0, result == 2 ? hd.guardPausetime[1] : hd.pausetime[1]);
		} else if (ghvset) {
			const float ratio = localScale / g.localScale;
			v.newHit();
			v.attrState = hd.attrState;
			v.attrAttack = hd.attrAttack;
			v.hitid = hd.id;
			v.xaccel = hd.xaccel * ratio * -byf;
			v.yaccel = hd.yaccel * ratio;
			v.groundType = hd.groundType;
			v.airType = hd.airType;
			if (hd.forcenofall)
				v.fallflag = false;
			v.type = g.stateType == 'A' ? v.airType : v.groundType;
			if (result == 2) {
				v.guarded = true;
				v.hitshaketime = std::max(0, hd.guardPausetime[1]);
				v.hittime = std::max(0, hd.guardHittime);
				v.slidetime = hd.guardSlidetime;
				if (g.stateType == 'A') {
					v.ctrltime = hd.airguardCtrltime;
					v.xvel = hd.airguardVel[0] * ratio * -byf;
					v.yvel = hd.airguardVel[1] * ratio;
				} else {
					v.ctrltime = hd.guardCtrltime;
					v.xvel = hd.guardVel[0] * ratio * -byf;
					v.yvel = hd.guardVel[1] * ratio;
				}
				v.fallflag = false;
				v.guardcount++;
			} else {
				v.guarded = false;
				v.hitshaketime = std::max(0, hd.pausetime[1]);
				v.slidetime = hd.groundSlidetime;
				v.p2getp1state = hd.p2getp1state;
				v.forcestand = hd.forcestand != 0;
				v.forcecrouch = hd.forcecrouch != 0;
				if (hd.unhittabletime[1] >= 0)
					g.unhittableTime_ = hd.unhittabletime[1];
				v.fallXvel = hd.fallXvel * ratio;
				v.fallYvel = hd.fallYvel * ratio;
				v.fallRecover = hd.fallRecover;
				v.fallRecovertime = hd.fallRecovertime;
				v.fallDamage = hd.fallDamage;
				v.fallKill = hd.fallKill;
				v.fallEnvShakeTime = hd.fallEnvShakeTime;
				v.fallEnvShakeFreq = hd.fallEnvShakeFreq;
				v.fallEnvShakeAmpl = (int32_t)(hd.fallEnvShakeAmpl * ratio);
				v.fallEnvShakePhase = hd.fallEnvShakePhase;
				v.fallEnvShakeMul = hd.fallEnvShakeMul;
				v.fallEnvShakeDir = hd.fallEnvShakeDir;
				v.fallEnvShakeDirAdd = hd.fallEnvShakeDirAdd;
				v.fallEnvShakeDecay = hd.fallEnvShakeDecay;
				if (g.stateType == 'A') {
					v.hittime = hd.airHittime;
					v.ctrltime = hd.airHittime;
					v.xvel = hd.airVel[0] * ratio * -byf;
					v.yvel = hd.airVel[1] * ratio;
					v.fallflag = v.fallflag || hd.airFall != 0;
				} else if (g.stateType == 'L') {
					v.hittime = hd.downHittime;
					v.ctrltime = hd.downHittime;
					v.fallflag = v.fallflag || hd.groundFall;
					const float *vel = g.y == 0 ? hd.downVel : hd.airVel;
					v.xvel = vel[0] * ratio * -byf;
					v.yvel = vel[1] * ratio;
					if (g.y == 0 && !hd.downBounce && v.yvel != 0) {
						v.fallXvel = notSetF();
						v.fallYvel = 0;
					}
				} else {
					v.ctrltime = hd.groundHittime;
					v.xvel = hd.groundVel[0] * ratio * -byf;
					v.yvel = hd.groundVel[1] * ratio;
					v.fallflag = v.fallflag || hd.groundFall;
					// MUGEN's own workaround: a fall always leaves the ground.
					if (v.fallflag && v.yvel == 0)
						v.yvel = -0.001f * ratio;
					v.hittime = v.yvel != 0 ? hd.airHittime : hd.groundHittime;
				}
				if (v.hittime < 0)
					v.hittime = 0;
				// MUGEN characters can guard one frame later than Ikemen's.
				if (v.hittime > 0)
					v.hittime++;
				v.hitcount = inCombo ? v.hitcount + 1 : 1;
				if (g.stateType != 'L' || v.downRecovertime > g.liedownTime_)
					v.downRecovertime = g.liedownTime_;
			}
			v.groundAnimtype = hd.animtype;
			v.airAnimtype = hd.airAnimtype;
			v.fallAnimtype = hd.fallAnimtype;
			v.animtype = g.gethitAnimtype();
			v.priority = hd.priority;
			// mindist / maxdist: snap the getter into range in front of the attacker.
			float snapX = notSetF();
			if (isSet(hd.mindist[0]) && (byf < 0 ? g.x > x - hd.mindist[0] : g.x < x + hd.mindist[0]))
				snapX = x + hd.mindist[0] * byf;
			if (isSet(hd.maxdist[0]) && (byf < 0 ? g.x < x - hd.maxdist[0] : g.x > x + hd.maxdist[0]))
				snapX = x + hd.maxdist[0] * byf;
			if (isSet(snapX))
				v.xoff = snapX * ratio - g.x;
		}
		if (!p2s && !g.gotHit_)
			g.stchtmp_ = false;
		g.gotHit_ = true;
		if (result == 1)
			v.damage += g.computeDamage(hd.hitDamage, v.kill, attackMul);
		else
			v.damage += g.computeDamage(hd.guardDamage, v.kill, attackMul);
		// A KO hit knocks down.
		if (ghvset && v.damage >= g.life) {
			if (v.kill || g.life <= 0) {
				v.fallflag = true;
				v.animtype = g.gethitAnimtype();
			} else {
				v.damage = g.life - 1;
			}
		}
	}

	// Power for both, on hit or guard.
	if (result > 0) {
		powerGain_ += std::abs(result) == 1 ? hd.hitGetPower : hd.guardGetPower;
		g.powerGain_ += std::abs(result) == 1 ? hd.hitGivePower : hd.guardGivePower;
		if (world_ && world_->effects && hd.envShakeTime > 0)
			world_->effects->startShake(hd.envShakeTime, hd.envShakeFreq, hd.envShakeAmpl / localScale,
				hd.envShakePhase, hd.envShakeMul, hd.envShakeDir);
	}
	// Each is now on the other's list: the getter is this character's target, with juggle points against it.
	targetP2_ = true;
	if (!g.targetedBy_) {
		g.targetedBy_ = true;
		g.juggleLeft_ = airJuggle_;
	}
	if (!ghvset)
		return result;
	g.p1facing_ = 0;
	if (std::abs(result) == 1) {
		if ((hd.p1getp2facing != 0 || hd.p1facing < 0) && facing != byf)
			p1facing_ = byf;
		if (hd.p2facing < 0)
			g.p1facing_ = byf;
		else if (hd.p2facing > 0)
			g.p1facing_ = -byf;
		if (g.p1facing_ == g.facing)
			g.p1facing_ = 0;
		if (hd.p1stateno >= 0) {
			minus_ = 1;
			stateChange1(hd.p1stateno);
			minus_ = 0;
			ctrl = false;
		}
		// Hitting someone falling, before or after this hit, costs this attack's juggle points.
		if (g.prevFallflag_ || g.ghv_.fallflag) {
			if (!(special_ & Special::NoJuggleCheck)) {
				g.juggleLeft_ -= juggle_;
				if (!traceLabel.empty())
					logf("%s frame %u: juggle cost %d, %d left", traceLabel.c_str(), world_ ? world_->gameTime : 0,
						juggle_, g.juggleLeft_);
			}
			juggle_ = 0;
		}
	}
	if (result > 0) {
		if (result == 2) {
			cornerPushVel_ = (g.stateType == 'A' ? hd.airguardCornerpushVeloff : hd.guardCornerpushVeloff) * facing;
			cornerPushMul_ = g.stateType == 'A' ? hd.airguardCornerpushVelmul : hd.guardCornerpushVelmul;
		} else if (g.stateType == 'A') {
			cornerPushVel_ = hd.airCornerpushVeloff * facing;
			cornerPushMul_ = hd.airCornerpushVelmul;
		} else if (g.stateType == 'L') {
			cornerPushVel_ = hd.downCornerpushVeloff * facing;
			cornerPushMul_ = hd.downCornerpushVelmul;
		} else {
			cornerPushVel_ = hd.groundCornerpushVeloff * facing;
			cornerPushMul_ = hd.groundCornerpushVelmul;
		}
		if (!traceLabel.empty() && cornerPushVel_ != 0)
			logf("%s frame %u: cornerpush %.2f x %.2f", traceLabel.c_str(), world_ ? world_->gameTime : 0,
				cornerPushVel_, cornerPushMul_);
	}
	return result;
}

bool Character::resolveReversal(Character &attacker)
{
	if (!reversalMatches(attacker) || !clsnTouch(attacker, 1, 1))
		return false;
	const int result = hitResultCheck(attacker);
	if (result == 0)
		return false;
	spawnHitSpark(attacker, result);
	const SoundBank *soundBank = hitdef_.hitsoundPrefix == 'F' && world_ ? world_->commonSound : &sound_;
	if (soundBank && hitdef_.hitsound[0] >= 0)
		soundBank->play(hitdef_.hitsound[0], hitdef_.hitsound[1]);
	hitPauseTime_ = std::max(1, hitdef_.pausetime[0] + (mugen1_ ? 1 : 0));
	moveContactType_ = 'H';
	moveContactTime_ = -1;
	hitdefContact_ = true;
	attacker.hitdef_.hitOpponent = true;
	if (!traceLabel.empty())
		logf("%s frame %u: state %d reverses p%d state %d", traceLabel.c_str(), world_ ? world_->gameTime : 0,
			stateNo, attacker.playerNo + 1, attacker.stateNo);
	return true;
}

// Hit and guard sparks use Ikemen's regular HitDef placement: x is offset from the target's facing edge, while y is
// relative to the attacker. KFM's shared sparks use the fightfx files (the HitDef resource prefix is F).
void Character::spawnHitSpark(Character &g, int result)
{
	if (!world_ || !world_->effects)
		return;
	const HitDef &hd = hitdef_;
	const int32_t sparkAnimNo = std::abs(result) == 1 ? hd.sparkno : hd.guardSparkno;
	const char prefix = std::abs(result) == 1 ? hd.sparkPrefix : hd.guardSparkPrefix;
	if (sparkAnimNo < 0 || prefix != 'F')
		return;
	const bool air = g.stateType == 'A';
	const float front = (air ? g.airFront_ : g.groundFront_) / g.localScale;
	const float back = (air ? g.airBack_ : g.groundBack_) / g.localScale;
	const float edge = facing != g.facing ? front : back;
	const float sparkX = g.x + edge * g.facing - hd.sparkxy[0] * facing / localScale;
	const float sparkY = y + hd.sparkxy[1] / localScale;
	world_->effects->spawnSpark(sparkAnimNo, sparkX, sparkY, facing);
}

void Character::beginHitDetection()
{
	gotHit_ = false;
	inGuardDist_ = false;
	prevFallflag_ = ghv_.fallflag;
	// No longer being hit: off the attacker's target list, out of its hold.
	if ((moveType != 'H' || stateNo == 5150) && targetedBy_) {
		if (p2_)
			p2_->targetP2_ = false;
		if (isTargetBound())
			resetBind();
		targetedBy_ = false;
	}
}

void Character::hitDetection()
{
	Character *g = p2_;
	if (!g || moveType == 'I')
		return;
	// Any attack (movetype A) close enough lets the opponent start guarding: guard.dist in front, a little behind.
	if (moveType == 'A' && !g->inGuardDist_) {
		const float dist = (g->x - x) * facing;
		if (dist < hitdef_.guardDist[0] && dist > -hitdef_.guardDist[1])
			g->inGuardDist_ = true;
	}
	// Only a live HitDef hits, and a fall can only be juggled while juggle points last (MUGEN: once it's a target).
	if (!hitdef_.active || hitdef_.hitonce < 0 || hitdef_.hitOpponent)
		return;
	// ReversalDef gets first claim on an incoming attack and checks attack boxes against attack boxes.
	if (g->resolveReversal(*this))
		return;
	const bool canJuggle = (special_ & Special::NoJuggleCheck) ||
		juggle_ <= (g->targetedBy_ ? g->juggleLeft_ : airJuggle_) || !targetP2_;
	if (!canJuggle) {
		if (!traceLabel.empty() && clsnHits(*g))
			logf("%s frame %u: state %d juggle rejected, cost %d, %d left", traceLabel.c_str(),
				world_ ? world_->gameTime : 0, stateNo, juggle_, g->juggleLeft_);
		return;
	}
	if (!g->hittableBy(*this) || !clsnHits(*g))
		return;
	const int result = hitResultCheck(*g);
	if (result == 0)
		return;
	spawnHitSpark(*g, result);
	const int32_t *hitSound = std::abs(result) == 1 ? hitdef_.hitsound : hitdef_.guardsound;
	const char soundPrefix = std::abs(result) == 1 ? hitdef_.hitsoundPrefix : hitdef_.guardsoundPrefix;
	const SoundBank *soundBank = soundPrefix == 'F' && world_ ? world_->commonSound : &sound_;
	if (soundBank && hitSound[0] >= 0)
		soundBank->play(hitSound[0], hitSound[1]);
	// MUGEN 1.x attackers pause one frame longer than WinMUGEN ones (Ikemen's hpFix).
	const int fix = mugen1_ ? 1 : 0;
	if (std::abs(result) == 1) {
		moveContactType_ = 'H';
		moveContactTime_ = -1;
		if (!gotHit_ && (g->stateType == 'A' ? hitdef_.airType : hitdef_.groundType) != HitType::None)
			hitPauseTime_ = std::max(1, hitdef_.pausetime[0] + fix);
		uniqHitCount_++;
	} else {
		moveContactType_ = 'G';
		moveContactTime_ = -1;
		if (!gotHit_)
			hitPauseTime_ = std::max(1, hitdef_.guardPausetime[0] + fix);
	}
	if (hitdef_.hitonce > 0)
		hitdef_.hitonce = -1;
	hitdefContact_ = true;
	if (hitdef_.unhittabletime[0] >= 0)
		unhittableTime_ = hitdef_.unhittabletime[0];
	if (!traceLabel.empty()) {
		const GetHitVars &v = g->ghv_;
		logf("%s frame %u: state %d %s: damage %d, pause %d, shake %d, hittime %d, slide %d, vel %.2f,%.2f, "
			 "animtype %d",
			traceLabel.c_str(), world_ ? world_->gameTime : 0, stateNo, result == 2 ? "guarded" : "hits", v.damage,
			hitPauseTime_, v.hitshaketime, v.hittime, v.slidetime, v.xvel, v.yvel, (int)v.animtype);
	}
}

// HitBy / NotHitBy: value fills the first slot, value2 the second, for time frames (1 by default, so a state
// asserts it every frame).
void Character::runHitBy(const StateController &sc, bool notHitBy)
{
	int32_t time = 1;
	paramI(sc, "time", 0, time);
	for (int slot = 0; slot < 2; slot++) {
		const std::string *value = sc.param(slot == 0 ? "value" : "value2");
		if (!value)
			continue;
		HitBySlot &s = hitBy_[slot];
		s = HitBySlot{};
		s.notHitBy = notHitBy;
		s.time = time;
		parseAttr(*value, s.attrState, s.attrAttack, false);
	}
}

void Character::runAssertSpecial(const StateController &sc)
{
	static const std::pair<const char *, uint32_t> names[] = { { "nowalk", Special::NoWalk },
		{ "noautoturn", Special::NoAutoTurn }, { "nostandguard", Special::NoStandGuard },
		{ "nocrouchguard", Special::NoCrouchGuard }, { "noairguard", Special::NoAirGuard },
		{ "invisible", Special::Invisible }, { "noshadow", Special::NoShadow },
		{ "nojugglecheck", Special::NoJuggleCheck }, { "unguardable", Special::Unguardable },
		{ "noko", Special::NoKo }, { "intro", Special::Intro }, { "roundnotover", Special::RoundNotOver },
		{ "nobardisplay", Special::NoBarDisplay }, { "nofallhitflag", Special::NoFallHitFlag },
		{ "timerfreeze", Special::TimerFreeze }, { "nocornerpush", Special::NoCornerPush } };
	for (const char *param : { "flag", "flag2", "flag3" }) {
		const std::string *v = sc.param(param);
		if (!v)
			continue;
		const std::string flag = text::toLower(text::trim(*v));
		bool known = false;
		for (const auto &[n, bit] : names) {
			if (flag == n) {
				special_ |= bit;
				known = true;
			}
		}
		if (!known)
			unsupported_["assertspecial " + flag]++;
	}
}

void Character::resetBind()
{
	bindTo_ = nullptr;
	bindTime_ = 0;
}

// Held by another character: placed relative to it, moving with it (Ikemen's updateBinding, bindToPlayer).
void Character::updateBinding()
{
	if (bindTime_ == 0 && bindTo_)
		resetBind();
	if (!bindTo_)
		return;
	const Character &bt = *bindTo_;
	if (bt.target() == this) {
		vx = facing * bt.facing * bt.vx;
		vy = bt.vy;
	}
	x = bt.x + bt.facing * bindPos_[0];
	y = bt.y + bindPos_[1];
	ghv_.xoff = ghv_.yoff = 0;
}

void Character::dropTarget()
{
	if (Character *t = target()) {
		if (t->isTargetBound())
			t->resetBind();
		t->targetedBy_ = false;
	}
	targetP2_ = false;
}

// The Target* controllers act on whoever this character has hit and still has as a target (here: the opponent,
// while it's being hit); "id" picks targets hit by HitDefs of that id.
bool Character::runTargetController(const StateController &sc)
{
	Character *t = target();
	int32_t id = -1;
	if (paramI(sc, "id", 0, id) && t && id >= 0 && t->ghv_.hitid != id)
		t = nullptr;
	if (!t)
		return false;
	const float ratio = localScale / t->localScale;
	float f = 0;
	int32_t i = 0;
	switch (sc.type) {
	case CtrlType::TargetBind: {
		int32_t time = 1;
		paramI(sc, "time", 0, time);
		float px = 0, py = 0;
		paramF(sc, "pos", 0, px);
		paramF(sc, "pos", 1, py);
		t->bindTo_ = this;
		t->bindTime_ = time;
		if (time == 0)
			t->resetBind();
		t->bindPos_[0] = px * ratio;
		t->bindPos_[1] = py * ratio;
		break;
	}
	case CtrlType::TargetState:
		// Into a state of this character's files (whoever's it's running), or its own from state -2.
		if (paramI(sc, "value", 0, i) && i >= 0) {
			t->ctrl = false;
			t->stateChange1(i, minus_ == -2 ? this : stateOwner_);
		}
		break;
	case CtrlType::TargetLifeAdd: {
		int32_t value = 0, kill = 1, absolute = 0;
		paramI(sc, "value", 0, value);
		paramI(sc, "kill", 0, kill);
		paramI(sc, "absolute", 0, absolute);
		if (value == 0)
			break;
		// As damage: a negative value hurts, through the target's defence unless absolute.
		const int32_t dmg = t->computeDamage(-value, kill != 0, 1, absolute != 0);
		t->life = std::clamp(t->life - dmg, 0, (int32_t)t->constF("data.life", 1000));
		t->ghv_.kill = kill != 0;
		break;
	}
	case CtrlType::TargetFacing:
		if (paramI(sc, "value", 0, i) && i != 0)
			t->setFacing(i < 0 ? -facing : facing);
		break;
	case CtrlType::TargetVelSet:
		if (paramF(sc, "x", 0, f))
			t->vx = f * ratio;
		if (paramF(sc, "y", 0, f))
			t->vy = f * ratio;
		break;
	case CtrlType::TargetVelAdd:
		if (paramF(sc, "x", 0, f))
			t->vx += f * ratio;
		if (paramF(sc, "y", 0, f))
			t->vy += f * ratio;
		break;
	case CtrlType::TargetPowerAdd:
		if (paramI(sc, "value", 0, i))
			t->power = std::clamp(t->power + i, 0, (int32_t)t->constF("data.power", 3000));
		break;
	case CtrlType::TargetDrop: {
		// Drops the targets not hit by HitDef excludeid (with one target, keepone changes nothing).
		int32_t exclude = -1;
		paramI(sc, "excludeid", 0, exclude);
		if (exclude >= 0 && t->ghv_.hitid != exclude)
			dropTarget();
		break;
	}
	default: break;
	}
	return false;
}

// --- Triggers --------------------------------------------------------------------------------------------------------

Value Character::trigger(Trig t, Value arg)
{
	const float camX = world_ ? world_->camX : 0, camY = world_ ? world_->camY : 0;
	const float halfW = world_ ? world_->halfWidth : 160;
	const float xmin = camX - halfW, xmax = camX + halfW;
	auto frontEdgeDist = [&] { return facing > 0 ? xmax - x : x - xmin; };
	auto backEdgeDist = [&] { return facing > 0 ? x - xmin : xmax - x; };
	switch (t) {
	case Trig::Time:
	case Trig::StateTime: return Value::I(stateTime);
	case Trig::AnimTime: return Value::I(anim.animTime());
	case Trig::Anim: return Value::I(animNo);
	case Trig::StateNo: return Value::I(stateNo);
	case Trig::PrevStateNo: return Value::I(prevStateNo);
	case Trig::Ctrl: return Value::B(ctrl);
	case Trig::Facing: return Value::I(facing);
	case Trig::Power: return Value::I(power);
	case Trig::PowerMax: return Value::I((int32_t)constF("data.power", 3000));
	case Trig::Life: return Value::I(life);
	case Trig::LifeMax: return Value::I((int32_t)constF("data.life", 1000));
	case Trig::Alive: return Value::B(!ko_);
	case Trig::HitOver: return Value::B(ghv_.hittime < 0);
	case Trig::HitShakeOver: return Value::B(ghv_.hitshaketime <= 0);
	// Frames since this character's attack made contact, 0 for none.
	case Trig::MoveContact: return Value::I(std::abs(moveContactTime_));
	case Trig::MoveHit: return Value::I(moveContactType_ == 'H' ? std::abs(moveContactTime_) : 0);
	case Trig::MoveGuarded: return Value::I(moveContactType_ == 'G' ? std::abs(moveContactTime_) : 0);
	case Trig::HitFall: return Value::B(ghv_.fallflag);
	case Trig::HitCount: return Value::I(hitCount_);
	case Trig::UniqHitCount: return Value::I(uniqHitCount_);
	// A fall can be recovered from once fall.recovertime has passed, if the hit allows it.
	case Trig::CanRecover: return Value::B(ghv_.fallRecover && ghv_.fallTime >= ghv_.fallRecovertime);
	case Trig::InGuardDist: return Value::B(inGuardDist_);
	case Trig::MoveReversed:
	case Trig::IsBound: return Value::I(0);
	case Trig::RoundState: return Value::I(world_ ? world_->roundState : 2);
	case Trig::RoundNo: return Value::I(world_ ? world_->roundNo : 1);
	case Trig::RoundsExisted: return Value::I(world_ ? world_->roundsExisted : 0);
	case Trig::MatchOver: return Value::B(world_ && world_->matchOver);
	case Trig::Win: return Value::B(world_ && world_->winTeam == playerNo);
	case Trig::Lose: return Value::B(world_ && world_->winTeam == 1 - playerNo);
	case Trig::DrawGame: return Value::B(world_ && world_->roundState >= 3 && world_->winTeam < 0);
	case Trig::AiLevel: return Value::I(aiLevel);
	case Trig::NumExplod: return Value::I(world_ && world_->effects ? world_->effects->countExplods(playerNo) : 0);
	case Trig::IsHelper:
	case Trig::NumProj:
	case Trig::NumHelper:
	case Trig::NumTarget:
	case Trig::NumPartner: return Value::I(0);
	case Trig::NumEnemy: return Value::I(p2_ ? 1 : 0);
	case Trig::PalNo: return Value::I(palette_ >= 0 ? palette_ + 1 : 1);
	case Trig::GameTime: return Value::I(world_ ? (int32_t)world_->gameTime : 0);
	case Trig::TeamSide: return Value::I(playerNo + 1);
	case Trig::Random:
		random_ = random_ * 1103515245u + 12345u;
		return Value::I((int32_t)((random_ >> 16) % 1000));
	case Trig::FrontEdgeDist: return Value::F(frontEdgeDist());
	case Trig::BackEdgeDist: return Value::F(backEdgeDist());
	// MUGEN characters measure from the edge of the body, half a pixel more in the air.
	case Trig::FrontEdgeBodyDist: return Value::F(frontEdgeDist() - widthFront_ - (stateType == 'A' ? 0.5f : 0));
	case Trig::BackEdgeBodyDist: return Value::F(backEdgeDist() - widthBack_ - (stateType == 'A' ? 0.5f : 0));
	case Trig::Vel: return Value::F(arg.i == 0 ? vx : arg.i == 1 ? vy : 0);
	case Trig::Pos: return Value::F(arg.i == 0 ? x - camX : arg.i == 1 ? y : 0);
	case Trig::P2Dist:
		if (!p2_)
			return Value::I(0);
		return Value::F(arg.i == 0 ? (p2_->x - x) * facing : arg.i == 1 ? p2_->y - y : 0);
	case Trig::P2BodyDist: {
		if (!p2_)
			return Value::I(0);
		if (arg.i != 0)
			return Value::F(arg.i == 1 ? p2_->y - y : 0);
		// The gap between the two bodies: my front, and whichever side of theirs faces me.
		const float dist = (p2_->x - x) * facing;
		const bool facingEachOther = p2_->facing != facing;
		const float myFront = (stateType == 'A' ? airFront_ : groundFront_) + widthFront_;
		const float theirs = (p2_->stateType == 'A') ? (facingEachOther ? p2_->airFront_ : p2_->airBack_)
													 : (facingEachOther ? p2_->groundFront_ : p2_->groundBack_);
		return Value::F(dist - myFront - theirs);
	}
	case Trig::ScreenPos: return Value::F(arg.i == 0 ? x - xmin : arg.i == 1 ? y - camY : 0);
	case Trig::CameraPos: return Value::F(arg.i == 0 ? camX : arg.i == 1 ? camY : 0);
	case Trig::HitVel: return Value::F(arg.i == 0 ? ghv_.xvel * facing : arg.i == 1 ? ghv_.yvel : 0);
	case Trig::AnimElemTime: return Value::I(anim.animElemTime(arg.toI()));
	case Trig::AnimElemNo: return Value::I(anim.animElemNo(arg.toI()));
	case Trig::SelfAnimExist:
	case Trig::AnimExist: return Value::B(anims_.find(arg.toI()) != nullptr);
	// A value written for 1280x720, in this character's units.
	case Trig::Const720p: return Value::F(arg.toF() * (320.0f / localScale) / 1280.0f);
	case Trig::ProjHitTime:
	case Trig::ProjContactTime:
	case Trig::ProjGuardedTime: return Value::I(-1);
	default: return Value::I(0);
	}
}

char Character::flag(Flag f)
{
	switch (f) {
	case Flag::StateType: return stateType;
	case Flag::MoveType: return moveType;
	case Flag::P2StateType: return p2_ ? p2_->stateType : 'S';
	case Flag::P2MoveType: return p2_ ? p2_->moveType : 'I';
	}
	return 'S';
}

bool Character::command(const std::string &n)
{
	return commands.active(n);
}

bool Character::constant(const std::string &n, Value &out)
{
	const auto it = constants_.find(n);
	if (it == constants_.end())
		return false;
	out = it->second;
	return true;
}

bool Character::getHitVar(const std::string &n, Value &out)
{
	const GetHitVars &v = ghv_;
	auto f = [](float x) { return Value::F(isSet(x) ? x : 0); };
	static const std::pair<const char *, int> names[] = { { "animtype", 0 }, { "groundtype", 1 }, { "airtype", 2 },
		{ "type", 3 }, { "damage", 4 }, { "hitcount", 5 }, { "guardcount", 6 }, { "fallcount", 7 },
		{ "hitshaketime", 8 }, { "hittime", 9 }, { "slidetime", 10 }, { "ctrltime", 11 }, { "recovertime", 12 },
		{ "xvel", 13 }, { "yvel", 14 }, { "xaccel", 15 }, { "yaccel", 16 }, { "chainid", 17 }, { "hitid", 17 },
		{ "guarded", 18 }, { "isbound", 19 }, { "fall", 20 }, { "fall.damage", 21 }, { "fall.xvel", 22 },
		{ "fall.yvel", 23 }, { "fall.recover", 24 }, { "fall.time", 25 }, { "fall.recovertime", 26 },
		{ "fall.kill", 27 }, { "xoff", 28 }, { "yoff", 29 }, { "priority", 30 } };
	int id = -1;
	for (const auto &[key, i] : names) {
		if (n == key) {
			id = i;
			break;
		}
	}
	switch (id) {
	case 0: out = Value::I((int)v.animtype); break;
	case 1: out = Value::I((int)v.groundType); break;
	case 2: out = Value::I((int)v.airType); break;
	case 3: out = Value::I((int)v.type); break;
	case 4: out = Value::I(v.damage); break;
	case 5: out = Value::I(v.hitcount); break;
	case 6: out = Value::I(v.guardcount); break;
	case 7: out = Value::I(v.fallcount); break;
	case 8: out = Value::I(v.hitshaketime); break;
	case 9: out = Value::I(v.hittime); break;
	case 10: out = Value::I(v.slidetime); break;
	case 11: out = Value::I(v.ctrltime); break;
	case 12: out = Value::I(v.downRecovertime); break;
	case 13: out = f(v.xvel); break;
	case 14: out = f(v.yvel); break;
	case 15: out = f(v.xaccel); break;
	case 16: out = f(v.yaccel); break;
	case 17: out = Value::I(v.hitid); break;
	case 18: out = Value::B(v.guarded); break;
	case 19: out = Value::B(isTargetBound()); break;
	case 20: out = Value::B(v.fallflag); break;
	case 21: out = Value::I(v.fallDamage); break;
	case 22: out = f(v.fallXvel); break;
	case 23: out = f(v.fallYvel); break;
	case 24: out = Value::B(v.fallRecover); break;
	case 25: out = Value::I(v.fallTime); break;
	case 26: out = Value::I(v.fallRecovertime); break;
	case 27: out = Value::B(v.fallKill); break;
	case 28: out = f(v.xoff); break;
	case 29: out = f(v.yoff); break;
	case 30: out = Value::I(v.priority); break;
	default: out = Value::I(0); return false;
	}
	return true;
}

Value Character::var(VarKind kind, int32_t index)
{
	switch (kind) {
	case VarKind::Var: return index >= 0 && index < 60 ? Value::I(vars_[index]) : Value::I(0);
	case VarKind::FVar: return index >= 0 && index < 40 ? Value::F(fvars_[index]) : Value::F(0);
	case VarKind::SysVar: return index >= 0 && index < 5 ? Value::I(sysvars_[index]) : Value::I(0);
	case VarKind::SysFVar: return index >= 0 && index < 5 ? Value::F(sysfvars_[index]) : Value::F(0);
	}
	return Value::I(0);
}

void Character::setVar(VarKind kind, int32_t index, Value v)
{
	switch (kind) {
	case VarKind::Var:
		if (index >= 0 && index < 60)
			vars_[index] = v.toI();
		break;
	case VarKind::FVar:
		if (index >= 0 && index < 40)
			fvars_[index] = v.toF();
		break;
	case VarKind::SysVar:
		if (index >= 0 && index < 5)
			sysvars_[index] = v.toI();
		break;
	case VarKind::SysFVar:
		if (index >= 0 && index < 5)
			sysfvars_[index] = v.toF();
		break;
	}
}

bool Character::hitDefAttr(int32_t stateTypes, int32_t attacks)
{
	// The HitDef set in this state, spent or not, while attacking.
	return moveType == 'A' && (hitdef_.attrState & stateTypes) != 0 && (hitdef_.attrAttack & attacks) != 0;
}

ExprContext *Character::redirect(Redirect r, int32_t)
{
	switch (r) {
	case Redirect::P2:
	case Redirect::Enemy:
	case Redirect::EnemyNear: return p2_;
	case Redirect::Root: return this;
	default: return nullptr;
	}
}

} // namespace m360
