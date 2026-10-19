#include "core/states.h"

#include <cctype>
#include <cstring>

#include "core/compat.h"
#include "core/file.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

const std::pair<const char *, CtrlType> CTRL_NAMES[] = {
	{ "afterimage", CtrlType::AfterImage }, { "afterimagetime", CtrlType::AfterImageTime },
	{ "allpalfx", CtrlType::AllPalFX }, { "angleadd", CtrlType::AngleAdd }, { "angledraw", CtrlType::AngleDraw },
	{ "anglemul", CtrlType::AngleMul }, { "angleset", CtrlType::AngleSet },
	{ "appendtoclipboard", CtrlType::AppendToClipboard }, { "assertspecial", CtrlType::AssertSpecial },
	{ "attackdist", CtrlType::AttackDist }, { "attackmulset", CtrlType::AttackMulSet }, { "bgpalfx", CtrlType::BGPalFX },
	{ "bindtoparent", CtrlType::BindToParent }, { "bindtoroot", CtrlType::BindToRoot },
	{ "bindtotarget", CtrlType::BindToTarget }, { "changeanim", CtrlType::ChangeAnim },
	{ "changeanim2", CtrlType::ChangeAnim2 }, { "changestate", CtrlType::ChangeState },
	{ "clearclipboard", CtrlType::ClearClipboard }, { "ctrlset", CtrlType::CtrlSet },
	{ "defencemulset", CtrlType::DefenceMulSet }, { "destroyself", CtrlType::DestroySelf },
	{ "displaytoclipboard", CtrlType::DisplayToClipboard }, { "envcolor", CtrlType::EnvColor },
	{ "envshake", CtrlType::EnvShake }, { "explod", CtrlType::Explod }, { "explodbindtime", CtrlType::ExplodBindTime },
	{ "fallenvshake", CtrlType::FallEnvShake }, { "forcefeedback", CtrlType::ForceFeedback },
	{ "gamemakeanim", CtrlType::GameMakeAnim }, { "gravity", CtrlType::Gravity }, { "helper", CtrlType::Helper },
	{ "hitadd", CtrlType::HitAdd }, { "hitby", CtrlType::HitBy }, { "hitdef", CtrlType::HitDef },
	{ "hitfalldamage", CtrlType::HitFallDamage }, { "hitfallset", CtrlType::HitFallSet },
	{ "hitfallvel", CtrlType::HitFallVel }, { "hitoverride", CtrlType::HitOverride },
	{ "hitvelset", CtrlType::HitVelSet }, { "lifeadd", CtrlType::LifeAdd }, { "lifeset", CtrlType::LifeSet },
	{ "makedust", CtrlType::MakeDust }, { "modifyexplod", CtrlType::ModifyExplod },
	{ "movehitreset", CtrlType::MoveHitReset }, { "nothitby", CtrlType::NotHitBy }, { "null", CtrlType::Null },
	{ "offset", CtrlType::Offset }, { "palfx", CtrlType::PalFX }, { "parentvaradd", CtrlType::ParentVarAdd },
	{ "parentvarset", CtrlType::ParentVarSet }, { "rootvaradd", CtrlType::RootVarAdd },
	{ "rootvarset", CtrlType::RootVarSet }, { "pause", CtrlType::Pause }, { "playerpush", CtrlType::PlayerPush },
	{ "playsnd", CtrlType::PlaySnd }, { "posadd", CtrlType::PosAdd }, { "posfreeze", CtrlType::PosFreeze },
	{ "posset", CtrlType::PosSet }, { "poweradd", CtrlType::PowerAdd }, { "powerset", CtrlType::PowerSet },
	{ "projectile", CtrlType::Projectile }, { "remappal", CtrlType::RemapPal },
	{ "removeexplod", CtrlType::RemoveExplod }, { "reversaldef", CtrlType::ReversalDef },
	{ "screenbound", CtrlType::ScreenBound }, { "selfstate", CtrlType::SelfState }, { "sndpan", CtrlType::SndPan },
	{ "sprpriority", CtrlType::SprPriority }, { "statetypeset", CtrlType::StateTypeSet },
	{ "stopsnd", CtrlType::StopSnd }, { "superpause", CtrlType::SuperPause }, { "targetbind", CtrlType::TargetBind },
	{ "targetdrop", CtrlType::TargetDrop }, { "targetfacing", CtrlType::TargetFacing },
	{ "targetlifeadd", CtrlType::TargetLifeAdd }, { "targetpoweradd", CtrlType::TargetPowerAdd },
	{ "targetstate", CtrlType::TargetState }, { "targetveladd", CtrlType::TargetVelAdd },
	{ "targetvelset", CtrlType::TargetVelSet }, { "trans", CtrlType::Trans }, { "turn", CtrlType::Turn },
	{ "varadd", CtrlType::VarAdd }, { "varrandom", CtrlType::VarRandom }, { "varrangeset", CtrlType::VarRangeSet },
	{ "varset", CtrlType::VarSet }, { "veladd", CtrlType::VelAdd }, { "velmul", CtrlType::VelMul },
	{ "velset", CtrlType::VelSet }, { "victoryquote", CtrlType::VictoryQuote }, { "width", CtrlType::Width },
};

CtrlType ctrlType(const std::string &lower)
{
	for (const auto &[name, type] : CTRL_NAMES) {
		if (lower == name)
			return type;
	}
	return CtrlType::Unknown;
}

// "trigger3" -> 3, "triggerall" -> 0; -1 when the name isn't a trigger.
int triggerNumber(const std::string &name)
{
	if (!text::startsWith(name, "trigger"))
		return -1;
	const std::string rest = name.substr(7);
	if (rest == "all")
		return 0;
	if (rest.empty() || rest.size() > 3)
		return -1;
	for (char c : rest) {
		if (c < '0' || c > '9')
			return -1;
	}
	const int n = text::atoi(rest);
	return n > 0 ? n : -1;
}

// The '=' that splits a parameter line, outside brackets: "var(1) = 5" splits at the second.
size_t topLevelEqual(const std::string &s)
{
	int depth = 0;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '(')
			depth++;
		else if (s[i] == ')' && depth > 0)
			depth--;
		else if (s[i] == '=' && depth == 0)
			return i;
	}
	return std::string::npos;
}

// A parameter line's name: lowercase, the first word, or a whole "var(...)" / "fvar(...)" / "sysvar(...)".
std::string paramName(std::string lhs)
{
	lhs = std::string(text::trim(lhs));
	const std::string lower = text::toLower(lhs);
	for (const char *fn : { "var", "fvar", "sysvar", "sysfvar" }) {
		const size_t n = std::strlen(fn);
		if (lower.compare(0, n, fn) == 0) {
			size_t i = n;
			while (i < lower.size() && lower[i] == ' ')
				i++;
			if (i < lower.size() && lower[i] == '(') {
				std::string out = fn;
				for (; i < lower.size(); i++) {
					if (lower[i] != ' ')
						out += lower[i];
				}
				return out;
			}
		}
	}
	const size_t space = lower.find_first_of(" \t");
	return space == std::string::npos ? lower : lower.substr(0, space);
}

// The text before ';', trimmed. (MUGEN takes ';' as a comment even inside quotes.)
std::string uncommented(const std::string &line)
{
	const size_t semi = line.find(';');
	return std::string(text::trim(semi == std::string::npos ? line : line.substr(0, semi)));
}

// "[Statedef 200]" -> true and 200; "[State 200, punch]" -> false.
bool statedefHeader(const std::string &lower, int32_t &no)
{
	if (lower.size() < 11 || lower.front() != '[' || lower.back() != ']' || lower.compare(1, 9, "statedef ") != 0)
		return false;
	no = text::atoi(std::string_view(lower).substr(10, lower.size() - 11));
	return true;
}

bool stateHeader(const std::string &lower)
{
	return lower.size() >= 8 && lower.front() == '[' && lower.back() == ']' && lower.compare(1, 6, "state ") == 0;
}

// Parameters whose values aren't expressions: attribute and flag lists, sound and spark references, keywords.
bool isTextParam(CtrlType type, const std::string &p)
{
	static const char *const anywhere[] = { "attr", "hitflag", "guardflag", "animtype", "air.animtype",
		"fall.animtype", "ground.type", "air.type", "sparkno", "guard.sparkno", "hitsound", "guardsound",
		"reversal.attr", "postype", "trans", "waveform", "flag", "flag2", "flag3", "p2getp1state", "space",
		"getpower", "givepower", "text", "palfx.color", "projhitanim", "projremanim", "projcancelanim", "projsprpriority",
		// Words, or parameters a controller doesn't have that MUGEN characters write anyway (Ikemen ignores them):
		// "name" labels, helpertype on an Explod, statetype / movetype / physics on a ChangeState.
		"affectteam", "afterimage.trans", "helpertype", "name", "statetype", "movetype", "physics" };
	for (const char *n : anywhere) {
		if (p == n)
			return true;
	}
	// HitBy and NotHitBy take attributes in value / value2. PlaySnd stays an expression parameter: resourcePrefix
	// removes its optional F/S marker before the group and number expressions are compiled.
	if ((type == CtrlType::HitBy || type == CtrlType::NotHitBy) && (p == "value" || p == "value2"))
		return true;
	if (type == CtrlType::StopSnd && p == "value")
		return true;
	// StateTypeSet's letters, and HitDef's "priority = 3, Hit" (a number, then Hit / Miss / Dodge).
	if (type == CtrlType::StateTypeSet && (p == "statetype" || p == "movetype" || p == "physics"))
		return true;
	if (type == CtrlType::Helper && (p == "helpertype" || p == "name"))
		return true;
	if ((type == CtrlType::HitDef || type == CtrlType::ReversalDef || type == CtrlType::Projectile) && p == "priority")
		return true;
	// Null does nothing with what it's given; old StateTypeSet "value" isn't a parameter (Ikemen ignores both).
	if (type == CtrlType::Null || (type == CtrlType::StateTypeSet && p == "value"))
		return true;
	return false;
}

} // namespace

const char *ctrlTypeName(CtrlType t)
{
	for (const auto &[name, type] : CTRL_NAMES) {
		if (type == t)
			return name;
	}
	return "unknown";
}

bool isExpressionParam(CtrlType type, const std::string &param)
{
	return !isTextParam(type, param);
}

std::string resourcePrefix(const std::string &textIn, char &prefix)
{
	const std::string t(text::trim(textIn));
	prefix = 0;
	// "F72", "F(60 + x)", "S5": a letter right before a number or a bracket.
	if (t.size() >= 2 && (t[0] == 'F' || t[0] == 'f' || t[0] == 'S' || t[0] == 's') &&
		(std::isdigit((unsigned char)t[1]) || t[1] == '(' || t[1] == ' ')) {
		prefix = (char)std::toupper((unsigned char)t[0]);
		return t.substr(1);
	}
	return t;
}

const std::string *StateController::param(const std::string &name) const
{
	const Param *p = find(name);
	return p ? &p->text : nullptr;
}

const Param *StateController::find(const std::string &name) const
{
	for (const Param &p : params) {
		if (p.name == name)
			return &p;
	}
	return nullptr;
}

bool StateController::triggered(ExprContext &ctx) const
{
	for (const Expr &e : triggerAll) {
		if (!e.eval(ctx).toB())
			return false;
	}
	for (const std::vector<Expr> &group : triggers) {
		bool all = !group.empty();
		for (const Expr &e : group) {
			if (!e.eval(ctx).toB()) {
				all = false;
				break;
			}
		}
		if (all)
			return true;
	}
	return false;
}

namespace {

// --check's report (13b): what a load problem says mugen360 lacks. An unknown controller type or trigger is a missing
// feature; any other parameter that doesn't parse is an expression mugen360 can't read yet.
void noteMissing(const std::string &what)
{
	const std::string unknownType = "unknown controller type ", unknownTrigger = "unknown trigger at '";
	if (what.compare(0, unknownType.size(), unknownType) == 0) {
		compat::note(compat::Kind::Controller, text::toLower(what.substr(unknownType.size())));
		return;
	}
	const size_t trigger = what.find(unknownTrigger);
	if (trigger != std::string::npos) {
		const size_t start = trigger + unknownTrigger.size();
		compat::note(compat::Kind::Trigger, text::toLower(what.substr(start, what.find('\'', start) - start)));
		return;
	}
	const size_t eq = what.find(" = ");
	const size_t colon = eq == std::string::npos ? std::string::npos : what.find(": ", eq);
	if (colon != std::string::npos)
		compat::note(compat::Kind::Expression, what.substr(colon + 2));
	else if (what.find(" isn't one of ") != std::string::npos)
		compat::note(compat::Kind::Expression, what.substr(0, what.find(" = ")) + " value");
}

} // namespace

bool StateTable::load(const std::string &path, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes)) {
		error = "can't read " + path;
		return false;
	}
	const std::vector<std::string> lines = text::splitLines(std::string(bytes.begin(), bytes.end()));
	auto problem = [&](int line, const std::string &what) {
		stats_.problems.push_back(path + ":" + std::to_string(line) + ": " + what);
		noteMissing(what);
	};
	std::map<int32_t, bool> seenHere;

	size_t i = 0;
	while (i < lines.size()) {
		int32_t no = 0;
		const std::string header = text::toLower(uncommented(lines[i]));
		if (!statedefHeader(header, no)) {
			i++;
			continue;
		}
		const int headerLine = (int)i + 1;
		i++;
		if (seenHere[no]) {
			problem(headerLine, "state " + std::to_string(no) + " defined twice in this file; the second is skipped");
			continue;
		}
		seenHere[no] = true;

		StateDef def;
		def.no = no;
		def.file = path;
		def.line = headerLine;
		// The statedef's own parameters, up to the first [State].
		for (; i < lines.size(); i++) {
			const std::string line = uncommented(lines[i]);
			if (!line.empty() && line[0] == '[')
				break;
			const size_t eq = topLevelEqual(line);
			if (eq == std::string::npos || eq == 0)
				continue;
			const std::string name = paramName(line.substr(0, eq));
			const std::string value(text::trim(std::string_view(line).substr(eq + 1)));
			if (value.empty())
				continue;
			auto letter = [&](char &out, const char *allowed) {
				const char c = (char)std::toupper((unsigned char)value[0]);
				if (std::strchr(allowed, c))
					out = c;
				else
					problem((int)i + 1, name + " = " + value + " isn't one of " + allowed);
			};
			auto expr = [&](Expr &out) {
				std::string err;
				if (!out.parse(value, err)) {
					problem((int)i + 1, name + " = " + value + ": " + err);
					stats_.paramErrors++;
				}
			};
			if (name == "type")
				letter(def.type, "SCALU");
			else if (name == "movetype")
				letter(def.moveType, "IAHU");
			else if (name == "physics")
				letter(def.physics, "SCANU");
			else if (name == "anim")
				expr(def.anim);
			else if (name == "ctrl")
				expr(def.ctrl);
			else if (name == "poweradd")
				expr(def.poweradd);
			else if (name == "juggle")
				expr(def.juggle);
			else if (name == "facep2")
				expr(def.facep2);
			else if (name == "sprpriority")
				expr(def.sprpriority);
			else if (name == "hitdefpersist")
				expr(def.hitdefpersist);
			else if (name == "movehitpersist")
				expr(def.movehitpersist);
			else if (name == "hitcountpersist")
				expr(def.hitcountpersist);
			else if (name == "velset") {
				std::string err;
				def.velset = parseExprList(value, err);
				if (!err.empty())
					problem((int)i + 1, "velset = " + value + ": " + err);
			}
		}

		// Its controllers, each "[State ...]" up to the next statedef.
		while (i < lines.size()) {
			const std::string head = text::toLower(uncommented(lines[i]));
			if (head.empty() || head[0] != '[') {
				i++;
				continue;
			}
			if (!stateHeader(head)) {
				// Ikemen's SectionName: a "[" line without its "]" ("[State 1201, VarSet)") isn't a section; it and
				// its keys are skipped, and the state goes on at the next header.
				if (head.back() != ']') {
					for (i++; i < lines.size(); i++) {
						const std::string next = uncommented(lines[i]);
						if (!next.empty() && next[0] == '[')
							break;
					}
					continue;
				}
				break;
			}
			StateController sc;
			sc.file = path;
			sc.line = (int)i + 1;
			const std::string rawHead = uncommented(lines[i]);
			const size_t comma = rawHead.find(',');
			if (comma != std::string::npos)
				sc.label = std::string(text::trim(std::string_view(rawHead).substr(comma + 1, rawHead.size() - comma - 2)));
			i++;
			bool bad = false;
			std::vector<bool> groupSeen;
			for (; i < lines.size(); i++) {
				const std::string line = uncommented(lines[i]);
				if (!line.empty() && line[0] == '[')
					break;
				const size_t eq = topLevelEqual(line);
				if (eq == std::string::npos || eq == 0)
					continue;
				const std::string name = paramName(line.substr(0, eq));
				const std::string value(text::trim(std::string_view(line).substr(eq + 1)));
				const int tn = triggerNumber(name);
				if (tn >= 0) {
					if (value.empty()) {
						problem((int)i + 1, name + " is empty");
						bad = true;
						continue;
					}
					Expr e;
					std::string err;
					if (!e.parse(value, err)) {
						problem((int)i + 1, name + " = " + value + ": " + err);
						bad = true;
						continue;
					}
					if (tn == 0) {
						sc.triggerAll.push_back(std::move(e));
					} else {
						if ((size_t)tn > sc.triggers.size())
							sc.triggers.resize((size_t)tn);
						sc.triggers[(size_t)tn - 1].push_back(std::move(e));
					}
					continue;
				}
				if (name == "type") {
					// The first "type" wins, as every key in an Ikemen section.
					if (sc.typeName.empty()) {
						sc.typeName = text::toLower(value);
						sc.type = ctrlType(sc.typeName);
					}
				} else if (name == "persistent") {
					sc.persistent = text::atoi(value);
				} else if (name == "ignorehitpause") {
					sc.ignoreHitPause = text::atoi(value) != 0;
				} else {
					// The first of duplicate parameters wins.
					if (!sc.param(name)) {
						Param p;
						p.name = name;
						p.text = value;
						sc.params.push_back(std::move(p));
					}
				}
			}
			stats_.controllers++;
			if (sc.typeName.empty()) {
				problem(sc.line, "controller without a type");
				bad = true;
			} else if (sc.type == CtrlType::Unknown) {
				problem(sc.line, "unknown controller type " + sc.typeName);
			}
			if (sc.triggers.empty() || sc.triggers[0].empty()) {
				problem(sc.line, "no trigger1");
				bad = true;
			}
			// Groups after a gap (trigger3 without trigger2) are never read, as in MUGEN.
			for (size_t g = 0; g < sc.triggers.size(); g++) {
				if (sc.triggers[g].empty()) {
					sc.triggers.resize(g);
					break;
				}
			}
			// Parse the parameters that are expressions now, once, so problems show at load.
			for (Param &p : sc.params) {
				if (!isExpressionParam(sc.type, p.name))
					continue;
				std::string err;
				// BindToTarget's "pos = x, y, mid": the third is a word (foot / mid / head), read from the text.
				std::string source = p.text;
				if (sc.type == CtrlType::BindToTarget && p.name == "pos") {
					const size_t second = source.find(',', source.find(',') + 1);
					if (second != std::string::npos && source.find(',') != std::string::npos)
						source.resize(second);
				}
				p.exprs = parseExprList(resourcePrefix(source, p.prefix), err);
				if (!err.empty()) {
					problem(sc.line, sc.typeName + " " + p.name + " = " + p.text + ": " + err);
					stats_.paramErrors++;
				}
			}
			if (bad) {
				stats_.skipped++;
				continue;
			}
			stats_.byType[sc.typeName]++;
			def.ctrls.push_back(std::move(sc));
		}

		const auto existing = states_.find(no);
		if (existing == states_.end()) {
			stats_.states++;
			states_.emplace(no, std::move(def));
		} else if (no < 0) {
			// -1, -2 and -3 run every tick: each file's adds to the same state.
			for (StateController &sc : def.ctrls)
				existing->second.ctrls.push_back(std::move(sc));
		}
	}
	logf("states: %s: %zu states so far", path.c_str(), states_.size());
	return true;
}

const StateDef *StateTable::find(int32_t no) const
{
	const auto it = states_.find(no);
	return it == states_.end() ? nullptr : &it->second;
}

} // namespace m360
