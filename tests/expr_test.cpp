// Tests for the expression language (core/expr): run build\pc\mugen360_tests.exe from the mugen360 folder.
// Checks operators and MUGEN's special forms against a fake character, then parses every trigger in Kung Fu Man's
// files and common1.cns and lists any that don't parse.

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "core/expr.h"
#include "core/file.h"
#include "core/text.h"
#include "platform/platform.h"

using namespace m360;

int runStatesReport();
int run6dTests();
int run6eTests();
int run7Tests();
int run7eTests();
int run8aTests();
int run8bTests();
int run8cTests();
int run8dTests();
int run8eTests();
int run8fTests();
int run9Tests();
int run10aTests();

// The tests have no platform: the log goes to the console.
void m360::platform::logLine(const char *line)
{
	std::printf("      log: %s\n", line);
}

namespace {

int failures = 0, passes = 0;

// A character whose values the tests set.
class FakeChar : public ExprContext {
public:
	std::map<Trig, Value> values;
	std::map<int, int32_t> elemTimes; // animelemtime(n)
	char stateType = 'S', moveType = 'I';
	std::vector<std::string> commands;
	std::map<std::string, Value> constants;
	int32_t vars[60] = {};
	float fvars[40] = {};
	float velX = 2.5f, velY = -1.0f;
	FakeChar *p2 = nullptr;
	int hitTypes = 1, hitAttacks = 1 << 0; // SC NA

	Value trigger(Trig t, Value arg) override
	{
		switch (t) {
		case Trig::Vel: return Value::F(arg.i == 0 ? velX : velY);
		case Trig::AnimElemTime: {
			const auto it = elemTimes.find(arg.toI());
			return Value::I(it == elemTimes.end() ? -1 : it->second);
		}
		default: {
			const auto it = values.find(t);
			return it == values.end() ? Value::I(0) : it->second;
		}
		}
	}
	char flag(Flag f) override
	{
		if (f == Flag::StateType)
			return stateType;
		if (f == Flag::MoveType)
			return moveType;
		return p2 ? p2->flag(f == Flag::P2StateType ? Flag::StateType : Flag::MoveType) : 'S';
	}
	bool command(const std::string &name) override
	{
		for (const std::string &c : commands)
			if (c == name)
				return true;
		return false;
	}
	bool constant(const std::string &name, Value &out) override
	{
		const auto it = constants.find(name);
		if (it == constants.end())
			return false;
		out = it->second;
		return true;
	}
	bool getHitVar(const std::string &, Value &out) override
	{
		out = Value::I(0);
		return true;
	}
	Value var(VarKind k, int32_t i) override
	{
		if (k == VarKind::Var && i >= 0 && i < 60)
			return Value::I(vars[i]);
		if (k == VarKind::FVar && i >= 0 && i < 40)
			return Value::F(fvars[i]);
		return Value::I(0);
	}
	void setVar(VarKind k, int32_t i, Value v) override
	{
		if (k == VarKind::Var && i >= 0 && i < 60)
			vars[i] = v.toI();
		if (k == VarKind::FVar && i >= 0 && i < 40)
			fvars[i] = v.toF();
	}
	bool hitDefAttr(int32_t types, int32_t attacks) override { return (types & hitTypes) && (attacks & hitAttacks); }
	ExprContext *redirect(Redirect r, int32_t) override { return r == Redirect::P2 ? p2 : nullptr; }
};

void check(const char *src, FakeChar &c, double expected, bool expectFloat = false)
{
	Expr e;
	std::string error;
	if (!e.parse(src, error)) {
		std::printf("FAIL  %-48s parse error: %s\n", src, error.c_str());
		failures++;
		return;
	}
	const Value v = e.eval(c);
	const bool ok = v.isFloat == expectFloat && std::fabs(v.toF() - expected) < 1e-4;
	if (!ok) {
		std::printf("FAIL  %-48s got %s%g, want %s%g\n", src, v.isFloat ? "float " : "int ", v.toF(),
			expectFloat ? "float " : "int ", expected);
		failures++;
	} else {
		passes++;
	}
}

void checkError(const char *src)
{
	Expr e;
	std::string error;
	if (e.parse(src, error)) {
		std::printf("FAIL  %-48s should not parse\n", src);
		failures++;
	} else {
		passes++;
	}
}

// Every trigger line in a CNS / CMD file: how many parse, and the ones that don't.
void parseFile(const std::string &path, int &total, int &bad)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes)) {
		std::printf("FAIL  can't read %s\n", path.c_str());
		failures++;
		return;
	}
	int lineNo = 0;
	for (const std::string &raw : text::splitLines(std::string(bytes.begin(), bytes.end()))) {
		lineNo++;
		// The value after "triggerN =" / "triggerall =", comments removed (quotes kept: commands).
		std::string line = raw;
		bool quoted = false;
		for (size_t i = 0; i < line.size(); i++) {
			if (line[i] == '"')
				quoted = !quoted;
			else if (line[i] == ';' && !quoted) {
				line = line.substr(0, i);
				break;
			}
		}
		const std::string lower = text::toLower(text::trim(line));
		if (!text::startsWith(lower, "trigger"))
			continue;
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		const std::string value(text::trim(std::string_view(line).substr(eq + 1)));
		total++;
		Expr e;
		std::string error;
		if (!e.parse(value, error)) {
			bad++;
			std::printf("      %s:%d  %s\n        -> %s\n", path.c_str(), lineNo, value.c_str(), error.c_str());
		}
	}
}

} // namespace

int main()
{
	FakeChar me, p2;
	me.p2 = &p2;
	p2.stateType = 'A';
	me.values[Trig::Time] = Value::I(10);
	me.values[Trig::StateNo] = Value::I(200);
	me.values[Trig::AnimTime] = Value::I(-3);
	me.elemTimes[2] = 0;
	me.elemTimes[3] = -4;
	me.elemTimes[1] = 6;
	me.commands = { "x", "holdfwd" };
	me.constants["velocity.walk.fwd.x"] = Value::F(2.4f);
	me.constants["data.life"] = Value::I(1000);
	p2.values[Trig::Life] = Value::I(750);

	std::printf("Operators\n");
	check("1 + 2 * 3", me, 7);
	check("(1 + 2) * 3", me, 9);
	check("7 / 2", me, 3);
	check("7 / 2.0", me, 3.5, true);
	check("7 % 3", me, 1);
	check("2 ** 10", me, 1024);
	check("-2 + 5", me, 3);
	check("10 / 0", me, 0);
	check("1 = 1 && 2 != 3", me, 1);
	check("0 || 0 || 1", me, 1);
	check("1 ^^ 1", me, 0);
	check("!0", me, 1);
	check("6 & 3", me, 2);
	check("6 | 3", me, 7);
	check("~0", me, -1);
	check("3 > 2 = 1", me, 1);
	check("1.5 = 1.5", me, 1);
	check("2 = 2.0", me, 1);

	std::printf("Ranges\n");
	check("time = [5, 10]", me, 1);
	check("time = [5, 10)", me, 0);
	check("time = (10, 20]", me, 0);
	check("time != [0, 5]", me, 1);
	check("time = (10)", me, 1);

	std::printf("Triggers\n");
	check("time", me, 10);
	check("stateno = 200 && time > 5", me, 1);
	check("animtime = 0", me, 0);
	check("vel x", me, 2.5, true);
	check("vel y < 0", me, 1);
	check("command = \"x\"", me, 1);
	check("command = \"y\"", me, 0);
	check("command != \"y\"", me, 1);
	check("Command = \"holdfwd\" && statetype = S", me, 1);
	check("statetype = S", me, 1);
	check("statetype != A", me, 1);
	check("statetype = A || statetype = C", me, 0);
	check("movetype = I", me, 1);
	check("p2statetype = A", me, 1);
	check("const(velocity.walk.fwd.x)", me, 2.4, true);
	check("const(data.life) / 2", me, 500);
	check("p2, life", me, 750);
	check("p2, life + 1", me, 751);
	check("ifelse(time > 5, 1, 2)", me, 1);
	check("abs(-4)", me, 4);
	check("floor(2.7)", me, 2);

	std::printf("Animation elements\n");
	check("animelem = 2", me, 1);
	check("animelem = 3", me, 0);
	check("animelem = 1, >= 5", me, 1);
	check("animelem = 1, < 5", me, 0);
	check("animelemtime(1) = 6", me, 1);
	check("animelem != 2", me, 0);
	check("animelem = 1, 6", me, 1);
	check("animelem = 3, -4", me, 1);
	// Unbracketed, ", 5" would read as the shorthand above, in Ikemen too.
	check("ifelse((animelem = 2), 5, 9)", me, 5);

	std::printf("Variables\n");
	check("var(3) := 7", me, 7);
	check("var(3)", me, 7);
	check("var(3) = 7 && var(4) := 2", me, 1);
	check("var(4)", me, 2);
	check("fvar(0) := 1.5", me, 1.5, true);

	std::printf("HitDefAttr and TimeMod\n");
	check("hitdefattr = SC, NA", me, 1);
	check("hitdefattr = SC, SA, HA", me, 0);
	check("hitdefattr = A, AA", me, 0);
	check("timemod = 5, 0", me, 1);
	check("timemod = 3, 1", me, 1);

	std::printf("Errors\n");
	checkError("1 +");
	checkError("(1");
	checkError("notatrigger > 3");
	checkError("command = x");

	std::printf("\nKung Fu Man's triggers\n");
	int total = 0, bad = 0;
	parseFile("data/chars/kfm/kfm.cns", total, bad);
	parseFile("data/chars/kfm/kfm.cmd", total, bad);
	parseFile("data/common1.cns", total, bad);
	std::printf("%d of %d trigger lines parse\n", total - bad, total);
	failures += bad;

	failures += runStatesReport();
	failures += run6dTests();
	failures += run6eTests();
	failures += run7Tests();
	failures += run7eTests();
	failures += run8aTests();
	failures += run8bTests();
	failures += run8cTests();
	failures += run8dTests();
	failures += run8eTests();
	failures += run8fTests();
	failures += run9Tests();
	failures += run10aTests();

	std::printf("\n%d passed, %d failed\n", passes, failures);
	return failures ? 1 : 0;
}
