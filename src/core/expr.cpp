#include "core/expr.h"

#include <cmath>
#include <cstring>
#include <map>

#include "core/text.h"

namespace m360 {

// --- The tree --------------------------------------------------------------------------------------------------------

enum class NodeKind : uint8_t {
	Num, Neg, Not, BitNot, Binary, AndAlso, OrElse, Range, Command, FlagIs, FlagValue, AnimElem, TimeMod,
	HitDefAttr, Const, GetHitVar, Var, VarSet, Trigger, Math1, Math2, IfElse, Redirect, TextIs, ProjFlag, StageVar
};

enum class Op : uint8_t { Add, Sub, Mul, Div, Mod, Pow, Eq, Ne, Lt, Le, Gt, Ge, BitAnd, BitOr, BitXor, BoolXor };

enum class MathFn : uint8_t { Abs, Floor, Ceil, Sin, Cos, Tan, Asin, Acos, Atan, Exp, Ln, Sign, Log, Min, Max };

struct ExprNode {
	NodeKind kind = NodeKind::Num;
	Value value;                                   // Num
	Op op = Op::Add;                               // Binary; AnimElem's comparison
	bool negate = false;                           // != forms
	bool lowOpen = false, highOpen = false;        // Range: ( and ) ends
	bool isRange = false;                          // AnimElem with ", = [lo, hi]"
	int32_t hi = 0;
	std::string name;                              // Command, Const, GetHitVar, FlagIs letters
	std::string key;                               // StageVar's key
	int32_t a = 0, b = 0;                          // AnimElem element & time; TimeMod; HitDefAttr masks
	Trig trig = Trig::Time;                        // Trigger
	Flag flag = Flag::StateType;                   // FlagIs / FlagValue
	VarKind var = VarKind::Var;                    // Var / VarSet
	MathFn fn = MathFn::Abs;                       // Math1 / Math2
	Redirect redirect = Redirect::P2;              // Redirect
	bool hasCompare = false;                       // AnimElem with ", op t"
	TextTrig text = TextTrig::Name;                // TextIs (name in name)
	std::vector<std::unique_ptr<ExprNode>> kids;
};

namespace {

using Node = std::unique_ptr<ExprNode>;

Node make(NodeKind k)
{
	auto n = std::make_unique<ExprNode>();
	n->kind = k;
	return n;
}

// --- Tokens ----------------------------------------------------------------------------------------------------------

struct Token {
	enum Kind { End, Num, Ident, Str, Sym } kind = End;
	std::string text;
};

const char *const SPECIAL = " !=<>()|&+-*/%,[]^:;{}#\"\t\r\n";

// Splits as Ikemen's tokenizer does: operators, numbers ("1.5", "2e3"), quoted strings kept as written, and anything
// else up to the next special symbol as one lowercase word ("velocity.walk.fwd.x").
bool tokenize(const std::string &s, std::vector<Token> &out, std::string &error)
{
	size_t i = 0;
	while (true) {
		while (i < s.size() && std::strchr(" \t\r\n", s[i]))
			i++;
		if (i >= s.size())
			break;
		const char c = s[i];
		const char n = i + 1 < s.size() ? s[i + 1] : '\0';
		Token t;
		if (c == '"') {
			const size_t close = s.find('"', i + 1);
			if (close == std::string::npos) {
				error = "unclosed \"";
				return false;
			}
			t.kind = Token::Str;
			t.text = s.substr(i + 1, close - i - 1);
			i = close + 1;
		} else if ((c == ':' && n == '=') || (c == '!' && n == '=') || (c == '>' && n == '=') || (c == '<' && n == '=') ||
			(c == '&' && n == '&') || (c == '|' && n == '|') || (c == '^' && n == '^') || (c == '*' && n == '*')) {
			t.kind = Token::Sym;
			t.text = s.substr(i, 2);
			i += 2;
		} else if (std::strchr("=!<>~&|^+-*/%,()[]:", c)) {
			t.kind = Token::Sym;
			t.text = std::string(1, c);
			i++;
		} else if ((c >= '0' && c <= '9') || c == '.') {
			size_t j = i;
			bool dot = false;
			while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || (s[j] == '.' && !dot))) {
				dot = dot || s[j] == '.';
				j++;
			}
			if (j > i && j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
				const size_t start = ++j;
				while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || (j == start && (s[j] == '-' || s[j] == '+'))))
					j++;
			}
			t.kind = Token::Num;
			t.text = s.substr(i, j - i);
			i = j;
		} else {
			size_t j = i;
			while (j < s.size() && !std::strchr(SPECIAL, s[j]))
				j++;
			if (j == i) {
				error = std::string("unexpected '") + c + "'";
				return false;
			}
			t.kind = Token::Ident;
			t.text = text::toLower(s.substr(i, j - i));
			i = j;
		}
		out.push_back(std::move(t));
	}
	out.push_back(Token{});
	return true;
}

// --- Names -----------------------------------------------------------------------------------------------------------

// OptArg: "numhelper" or "numhelper(1000)" (without the argument, -1: any).
enum class Shape { Plain, Axis, OneArg, OptArg };

struct TrigInfo {
	Trig trig;
	Shape shape;
};

const std::map<std::string, TrigInfo> &triggers()
{
	static const std::map<std::string, TrigInfo> t = {
		{ "time", { Trig::Time, Shape::Plain } }, { "animtime", { Trig::AnimTime, Shape::Plain } },
		{ "anim", { Trig::Anim, Shape::Plain } }, { "stateno", { Trig::StateNo, Shape::Plain } },
		{ "prevstateno", { Trig::PrevStateNo, Shape::Plain } }, { "ctrl", { Trig::Ctrl, Shape::Plain } },
		{ "facing", { Trig::Facing, Shape::Plain } }, { "power", { Trig::Power, Shape::Plain } },
		{ "powermax", { Trig::PowerMax, Shape::Plain } }, { "life", { Trig::Life, Shape::Plain } },
		{ "lifemax", { Trig::LifeMax, Shape::Plain } }, { "alive", { Trig::Alive, Shape::Plain } },
		{ "hitover", { Trig::HitOver, Shape::Plain } }, { "hitshakeover", { Trig::HitShakeOver, Shape::Plain } },
		{ "movecontact", { Trig::MoveContact, Shape::Plain } }, { "movehit", { Trig::MoveHit, Shape::Plain } },
		{ "moveguarded", { Trig::MoveGuarded, Shape::Plain } }, { "movereversed", { Trig::MoveReversed, Shape::Plain } },
		{ "hitfall", { Trig::HitFall, Shape::Plain } }, { "hitcount", { Trig::HitCount, Shape::Plain } },
		{ "uniqhitcount", { Trig::UniqHitCount, Shape::Plain } }, { "canrecover", { Trig::CanRecover, Shape::Plain } },
		{ "inguarddist", { Trig::InGuardDist, Shape::Plain } }, { "roundstate", { Trig::RoundState, Shape::Plain } },
		{ "roundno", { Trig::RoundNo, Shape::Plain } }, { "roundsexisted", { Trig::RoundsExisted, Shape::Plain } },
		{ "matchover", { Trig::MatchOver, Shape::Plain } }, { "palno", { Trig::PalNo, Shape::Plain } },
		{ "isbound", { Trig::IsBound, Shape::Plain } }, { "gametime", { Trig::GameTime, Shape::Plain } },
		{ "teamside", { Trig::TeamSide, Shape::Plain } }, { "numproj", { Trig::NumProj, Shape::Plain } },
		{ "numhelper", { Trig::NumHelper, Shape::OptArg } }, { "numtarget", { Trig::NumTarget, Shape::OptArg } },
		{ "numexplod", { Trig::NumExplod, Shape::OptArg } }, { "random", { Trig::Random, Shape::Plain } },
		{ "win", { Trig::Win, Shape::Plain } }, { "lose", { Trig::Lose, Shape::Plain } },
		{ "drawgame", { Trig::DrawGame, Shape::Plain } }, { "ishelper", { Trig::IsHelper, Shape::OptArg } },
		{ "ailevel", { Trig::AiLevel, Shape::Plain } }, { "numenemy", { Trig::NumEnemy, Shape::Plain } },
		{ "numpartner", { Trig::NumPartner, Shape::Plain } },
		{ "frontedgebodydist", { Trig::FrontEdgeBodyDist, Shape::Plain } },
		{ "backedgebodydist", { Trig::BackEdgeBodyDist, Shape::Plain } },
		{ "frontedgedist", { Trig::FrontEdgeDist, Shape::Plain } },
		{ "backedgedist", { Trig::BackEdgeDist, Shape::Plain } }, { "statetime", { Trig::StateTime, Shape::Plain } },
		{ "vel", { Trig::Vel, Shape::Axis } }, { "pos", { Trig::Pos, Shape::Axis } },
		{ "p2dist", { Trig::P2Dist, Shape::Axis } }, { "p2bodydist", { Trig::P2BodyDist, Shape::Axis } },
		{ "parentdist", { Trig::ParentDist, Shape::Axis } }, { "rootdist", { Trig::RootDist, Shape::Axis } },
		{ "screenpos", { Trig::ScreenPos, Shape::Axis } }, { "camerapos", { Trig::CameraPos, Shape::Axis } },
		{ "hitvel", { Trig::HitVel, Shape::Axis } },
		{ "animelemtime", { Trig::AnimElemTime, Shape::OneArg } }, { "animelemno", { Trig::AnimElemNo, Shape::OneArg } },
		{ "selfanimexist", { Trig::SelfAnimExist, Shape::OneArg } }, { "animexist", { Trig::AnimExist, Shape::OneArg } },
		{ "const720p", { Trig::Const720p, Shape::OneArg } }, { "const240p", { Trig::Const240p, Shape::OneArg } },
		{ "const480p", { Trig::Const480p, Shape::OneArg } }, { "winko", { Trig::WinKO, Shape::Plain } },
		{ "wintime", { Trig::WinTime, Shape::Plain } }, { "winperfect", { Trig::WinPerfect, Shape::Plain } },
		{ "loseko", { Trig::LoseKO, Shape::Plain } }, { "losetime", { Trig::LoseTime, Shape::Plain } },
		{ "camerazoom", { Trig::CameraZoom, Shape::Plain } }, { "projhittime", { Trig::ProjHitTime, Shape::OneArg } },
		{ "projcontacttime", { Trig::ProjContactTime, Shape::OneArg } },
		{ "projguardedtime", { Trig::ProjGuardedTime, Shape::OneArg } },
		{ "hitpausetime", { Trig::HitPauseTime, Shape::Plain } }, { "id", { Trig::ID, Shape::Plain } },
		{ "matchno", { Trig::MatchNo, Shape::Plain } }, { "tickspersecond", { Trig::TicksPerSecond, Shape::Plain } },
		{ "ishometeam", { Trig::IsHomeTeam, Shape::Plain } }, { "p2life", { Trig::P2Life, Shape::Plain } },
		{ "p2stateno", { Trig::P2StateNo, Shape::Plain } }, { "p2ctrl", { Trig::P2Ctrl, Shape::Plain } },
		{ "leftedge", { Trig::LeftEdge, Shape::Plain } }, { "rightedge", { Trig::RightEdge, Shape::Plain } },
		{ "topedge", { Trig::TopEdge, Shape::Plain } }, { "bottomedge", { Trig::BottomEdge, Shape::Plain } },
		{ "frontedge", { Trig::FrontEdge, Shape::Plain } }, { "backedge", { Trig::BackEdge, Shape::Plain } },
		{ "screenwidth", { Trig::ScreenWidth, Shape::Plain } }, { "screenheight", { Trig::ScreenHeight, Shape::Plain } },
		{ "gamewidth", { Trig::GameWidth, Shape::Plain } }, { "gameheight", { Trig::GameHeight, Shape::Plain } },
		{ "numprojid", { Trig::NumProjID, Shape::OneArg } }, { "projcanceltime", { Trig::ProjCancelTime, Shape::OneArg } },
		{ "playeridexist", { Trig::PlayerIDExist, Shape::OneArg } },
	};
	return t;
}

const std::map<std::string, MathFn> &math1()
{
	static const std::map<std::string, MathFn> m = { { "abs", MathFn::Abs }, { "floor", MathFn::Floor },
		{ "ceil", MathFn::Ceil }, { "sin", MathFn::Sin }, { "cos", MathFn::Cos }, { "tan", MathFn::Tan },
		{ "asin", MathFn::Asin }, { "acos", MathFn::Acos }, { "atan", MathFn::Atan }, { "exp", MathFn::Exp },
		{ "ln", MathFn::Ln }, { "sign", MathFn::Sign } };
	return m;
}

// --- Parser ----------------------------------------------------------------------------------------------------------

struct ParseError {
	std::string what;
};

class Parser {
public:
	explicit Parser(std::vector<Token> tokens) : t_(std::move(tokens)) {}

	Node expression() { return boolOr(); }
	bool atEnd() const { return peek().kind == Token::End; }
	bool takeSym(const char *s)
	{
		if (isSym(s)) {
			pos_++;
			return true;
		}
		return false;
	}
	const Token &peek(size_t ahead = 0) const { return t_[std::min(pos_ + ahead, t_.size() - 1)]; }

private:
	[[noreturn]] void fail(const std::string &what) const
	{
		const Token &t = peek();
		throw ParseError{ what + (t.kind == Token::End ? " at the end" : " at '" + t.text + "'") };
	}
	bool isSym(const char *s) const { return peek().kind == Token::Sym && peek().text == s; }
	void expect(const char *s)
	{
		if (!takeSym(s))
			fail(std::string("expected '") + s + "'");
	}
	std::string ident()
	{
		if (peek().kind != Token::Ident)
			fail("expected a name");
		return t_[pos_++].text;
	}
	// A whole number, maybe negative, for animelem and the like.
	int32_t integer()
	{
		bool neg = false;
		while (isSym("-") || isSym("+"))
			neg = (t_[pos_++].text == "-") != neg;
		if (peek().kind != Token::Num)
			fail("expected a number");
		const int32_t v = text::atoi(t_[pos_++].text);
		return neg ? -v : v;
	}
	// "=" or "!="; true for "!=".
	// Ikemen's checkEquality with ignoreMostErrors, its default: "=" or "!=", else anything up to a symbol ending in
	// '=' is skipped and that symbol reads as "=" (so "AnimElem >= 5" is "AnimElem = 5", as MUGEN takes it).
	bool equality()
	{
		if (takeSym("="))
			return false;
		if (takeSym("!="))
			return true;
		for (size_t at = pos_; t_[at].kind != Token::End; at++) {
			if (t_[at].kind == Token::Sym && !t_[at].text.empty() && t_[at].text.back() == '=') {
				pos_ = at + 1;
				return false;
			}
		}
		fail("expected '=' or '!='");
	}

	Node binary(Op op, Node l, Node r)
	{
		Node n = make(NodeKind::Binary);
		n->op = op;
		n->kids.push_back(std::move(l));
		n->kids.push_back(std::move(r));
		return n;
	}

	Node boolOr()
	{
		Node l = boolXor();
		while (takeSym("||")) {
			Node n = make(NodeKind::OrElse);
			n->kids.push_back(std::move(l));
			n->kids.push_back(boolXor());
			l = std::move(n);
		}
		return l;
	}
	Node boolXor()
	{
		Node l = boolAnd();
		while (takeSym("^^"))
			l = binary(Op::BoolXor, std::move(l), boolAnd());
		return l;
	}
	Node boolAnd()
	{
		Node l = bitOr();
		while (takeSym("&&")) {
			Node n = make(NodeKind::AndAlso);
			n->kids.push_back(std::move(l));
			n->kids.push_back(bitOr());
			l = std::move(n);
		}
		return l;
	}
	Node bitOr()
	{
		Node l = bitXor();
		while (takeSym("|"))
			l = binary(Op::BitOr, std::move(l), bitXor());
		return l;
	}
	Node bitXor()
	{
		Node l = bitAnd();
		while (takeSym("^"))
			l = binary(Op::BitXor, std::move(l), bitAnd());
		return l;
	}
	Node bitAnd()
	{
		Node l = eqne();
		while (takeSym("&"))
			l = binary(Op::BitAnd, std::move(l), eqne());
		return l;
	}
	// "=" and "!=", with MUGEN's interval forms: "x = [1, 3]", "x != (0, 5]".
	Node eqne()
	{
		Node l = grls();
		while (isSym("=") || isSym("!=")) {
			const bool ne = t_[pos_++].text == "!=";
			if (isSym("[") || isSym("(")) {
				if (Node r = range(l, ne)) {
					l = std::move(r);
					continue;
				}
			}
			l = binary(ne ? Op::Ne : Op::Eq, std::move(l), grls());
		}
		return l;
	}
	// An interval after "=": null when "(" turns out to open a plain bracketed value instead.
	Node range(Node &value, bool ne)
	{
		const size_t start = pos_;
		const bool lowOpen = t_[pos_++].text == "(";
		Node lo = boolOr();
		if (!takeSym(",")) {
			if (!lowOpen)
				fail("expected ',' in a range");
			pos_ = start;
			return nullptr;
		}
		Node hi = boolOr();
		bool highOpen;
		if (takeSym("]"))
			highOpen = false;
		else if (takeSym(")"))
			highOpen = true;
		else
			fail("expected ']' or ')'");
		Node n = make(NodeKind::Range);
		n->negate = ne;
		n->lowOpen = lowOpen;
		n->highOpen = highOpen;
		n->kids.push_back(std::move(value));
		n->kids.push_back(std::move(lo));
		n->kids.push_back(std::move(hi));
		return n;
	}
	Node grls()
	{
		Node l = adsb();
		while (true) {
			Op op;
			if (takeSym(">"))
				op = Op::Gt;
			else if (takeSym(">="))
				op = Op::Ge;
			else if (takeSym("<"))
				op = Op::Lt;
			else if (takeSym("<="))
				op = Op::Le;
			else
				return l;
			l = binary(op, std::move(l), adsb());
		}
	}
	Node adsb()
	{
		Node l = mldv();
		while (true) {
			if (takeSym("+"))
				l = binary(Op::Add, std::move(l), mldv());
			else if (takeSym("-"))
				l = binary(Op::Sub, std::move(l), mldv());
			else
				return l;
		}
	}
	Node mldv()
	{
		Node l = pow();
		while (true) {
			if (takeSym("*"))
				l = binary(Op::Mul, std::move(l), pow());
			else if (takeSym("/"))
				l = binary(Op::Div, std::move(l), pow());
			else if (takeSym("%"))
				l = binary(Op::Mod, std::move(l), pow());
			else
				return l;
		}
	}
	Node pow()
	{
		Node l = unary();
		while (takeSym("**"))
			l = binary(Op::Pow, std::move(l), unary());
		return l;
	}
	Node unary()
	{
		if (takeSym("-")) {
			Node n = make(NodeKind::Neg);
			n->kids.push_back(unary());
			return n;
		}
		if (takeSym("+"))
			return unary();
		if (takeSym("!")) {
			Node n = make(NodeKind::Not);
			n->kids.push_back(unary());
			return n;
		}
		if (takeSym("~")) {
			Node n = make(NodeKind::BitNot);
			n->kids.push_back(unary());
			return n;
		}
		return value();
	}

	Node number(const std::string &s)
	{
		Node n = make(NodeKind::Num);
		if (s.find_first_of(".eE") != std::string::npos) {
			n->value = Value::F((float)text::atof(s));
		} else {
			const double d = text::atof(s);
			n->value = Value::I(d > 2147483647.0 ? INT32_MAX : (int32_t)d);
		}
		return n;
	}

	// "(expr)" after a function name.
	Node arg()
	{
		expect("(");
		Node a = boolOr();
		expect(")");
		return a;
	}

	Node value()
	{
		const Token &t = peek();
		if (t.kind == Token::Num) {
			pos_++;
			return number(t.text);
		}
		if (t.kind == Token::Str)
			fail("a string only goes after command =");
		if (takeSym("(")) {
			Node n = boolOr();
			expect(")");
			return n;
		}
		if (t.kind != Token::Ident)
			fail("expected a value");
		const std::string name = t_[pos_++].text;

		if (name == "command") {
			Node n = make(NodeKind::Command);
			n->negate = equality();
			if (peek().kind != Token::Str)
				fail("expected a quoted command name");
			n->name = t_[pos_++].text;
			return n;
		}
		if (name == "statetype" || name == "movetype" || name == "p2statetype" || name == "p2movetype") {
			const Flag f = name == "statetype" ? Flag::StateType
				: name == "movetype"          ? Flag::MoveType
				: name == "p2statetype"       ? Flag::P2StateType
											  : Flag::P2MoveType;
			if (!isSym("=") && !isSym("!=")) {
				Node n = make(NodeKind::FlagValue);
				n->flag = f;
				return n;
			}
			Node n = make(NodeKind::FlagIs);
			n->flag = f;
			n->negate = equality();
			n->name = peek().kind == Token::Str ? t_[pos_++].text : ident();
			for (char &c : n->name)
				c = (char)std::toupper((unsigned char)c);
			return n;
		}
		if (name == "animelem") {
			// "AnimElem = 2" is true as element 2 starts; "AnimElem = 2, >= 3" compares how long it's been showing.
			Node n = make(NodeKind::AnimElem);
			n->negate = equality();
			n->a = integer();
			if (isSym(",") && peek(1).kind == Token::Sym &&
				(peek(1).text == "=" || peek(1).text == "!=" || peek(1).text == "<" || peek(1).text == ">" ||
					peek(1).text == "<=" || peek(1).text == ">=")) {
				pos_++;
				const std::string op = t_[pos_++].text;
				n->op = op == "=" ? Op::Eq : op == "!=" ? Op::Ne : op == "<" ? Op::Lt : op == ">" ? Op::Gt
					: op == "<=" ? Op::Le : Op::Ge;
				if ((n->op == Op::Eq || n->op == Op::Ne) && (isSym("[") || isSym("("))) {
					// "AnimElem = 1, = [45, 55]": the time inside a range (Ikemen's evaluateComparison).
					n->lowOpen = t_[pos_++].text == "(";
					n->b = integer();
					expect(",");
					n->hi = integer();
					if (takeSym("]"))
						n->highOpen = false;
					else if (takeSym(")"))
						n->highOpen = true;
					else
						fail("expected ']' or ')'");
					n->isRange = true;
				} else {
					n->b = integer();
				}
				n->hasCompare = true;
			} else if (isSym(",") && (peek(1).kind == Token::Num || (peek(1).kind == Token::Sym && peek(1).text == "-" &&
										 peek(2).kind == Token::Num))) {
				// "AnimElem = 2, 1": no operator means "=" (Ikemen's evaluateComparison).
				pos_++;
				n->op = Op::Eq;
				n->b = integer();
				n->hasCompare = true;
			}
			return n;
		}
		static const std::map<std::string, TextTrig> texts = { { "name", TextTrig::Name },
			{ "p1name", TextTrig::P1Name }, { "p2name", TextTrig::P2Name }, { "p3name", TextTrig::P3Name },
			{ "p4name", TextTrig::P4Name }, { "authorname", TextTrig::AuthorName }, { "teammode", TextTrig::TeamMode } };
		if (const auto tt = texts.find(name); tt != texts.end()) {
			// "P2Name = \"Kung Fu Man\"", "TeamMode = single": compared without case.
			Node n = make(NodeKind::TextIs);
			n->text = tt->second;
			n->negate = equality();
			n->name = text::toLower(peek().kind == Token::Str ? t_[pos_++].text : ident());
			return n;
		}
		// Old projectile triggers with the id in the name: "ProjHit1200 = 1", "ProjContact = 1, < 15" (Ikemen turns
		// them into Proj*Time(id)). The first form is true on the tick of contact; 0 negates it.
		for (const auto &[prefix, trig] : { std::pair{ "projhit", Trig::ProjHitTime },
				 std::pair{ "projcontact", Trig::ProjContactTime }, std::pair{ "projguarded", Trig::ProjGuardedTime } }) {
			const std::string p(prefix);
			if (name.compare(0, p.size(), p) != 0 || name.size() > p.size() + 9 ||
				name.find_first_not_of("0123456789", p.size()) != std::string::npos || name.find("time") != std::string::npos)
				continue;
			Node n = make(NodeKind::ProjFlag);
			n->trig = trig;
			n->a = name.size() > p.size() ? text::atoi(name.substr(p.size())) : -1;
			n->negate = equality();
			if (integer() == 0)
				n->negate = !n->negate;
			if (isSym(",") && peek(1).kind == Token::Sym) {
				pos_++;
				const std::string op = t_[pos_++].text;
				n->op = op == "=" ? Op::Eq : op == "!=" ? Op::Ne : op == "<" ? Op::Lt : op == ">" ? Op::Gt
					: op == "<=" ? Op::Le : Op::Ge;
				n->b = integer();
				n->hasCompare = true;
			}
			return n;
		}
		if (name == "timemod") {
			// Old style: "TimeMod = 4, 0" is time % 4 = 0.
			Node n = make(NodeKind::TimeMod);
			n->negate = equality();
			n->a = integer();
			expect(",");
			n->b = integer();
			return n;
		}
		if (name == "hitdefattr") {
			// "HitDefAttr = SC, NA, SA": the state types, then attack classes.
			Node n = make(NodeKind::HitDefAttr);
			n->negate = equality();
			const std::string types = ident();
			for (char c : types) {
				n->a |= c == 's' ? 1 : c == 'c' ? 2 : c == 'a' ? 4 : 0;
			}
			while (isSym(",") && peek(1).kind == Token::Ident && peek(1).text.size() == 2 &&
				std::strchr("nsha", peek(1).text[0]) && std::strchr("atp", peek(1).text[1])) {
				pos_++;
				const std::string cls = t_[pos_++].text;
				const int kind = cls[1] == 'a' ? 0 : cls[1] == 't' ? 1 : 2;
				// "AA", "AT", "AP": any class of that kind.
				for (int c = 0; c < 3; c++) {
					if (cls[0] == 'a' || "nsh"[c] == cls[0])
						n->b |= 1 << (c * 3 + kind);
				}
			}
			return n;
		}
		if (name == "stagevar") {
			// StageVar(camera.boundleft); the text ones compare like Name: StageVar(info.name) = "Temple".
			Node n = make(NodeKind::StageVar);
			expect("(");
			n->key = ident();
			expect(")");
			if (n->key == "info.name" || n->key == "info.displayname" || n->key == "info.author") {
				n->negate = equality();
				n->name = text::toLower(peek().kind == Token::Str ? t_[pos_++].text : ident());
				n->hasCompare = true;
			}
			return n;
		}
		if (name == "const" || name == "gethitvar") {
			Node n = make(name == "const" ? NodeKind::Const : NodeKind::GetHitVar);
			expect("(");
			n->name = ident();
			expect(")");
			return n;
		}
		if (name == "var" || name == "fvar" || name == "sysvar" || name == "sysfvar") {
			const VarKind k = name == "var" ? VarKind::Var
				: name == "fvar"          ? VarKind::FVar
				: name == "sysvar"        ? VarKind::SysVar
										  : VarKind::SysFVar;
			Node index = arg();
			if (takeSym(":=")) {
				Node n = make(NodeKind::VarSet);
				n->var = k;
				n->kids.push_back(std::move(index));
				n->kids.push_back(eqne());
				return n;
			}
			Node n = make(NodeKind::Var);
			n->var = k;
			n->kids.push_back(std::move(index));
			return n;
		}
		if (name == "ifelse" || name == "cond") {
			Node n = make(NodeKind::IfElse);
			expect("(");
			n->kids.push_back(boolOr());
			expect(",");
			n->kids.push_back(boolOr());
			expect(",");
			n->kids.push_back(boolOr());
			expect(")");
			return n;
		}
		if (name == "pi" || name == "e") {
			Node n = make(NodeKind::Num);
			n->value = Value::F(name == "pi" ? 3.14159265f : 2.71828183f);
			return n;
		}
		if (const auto m = math1().find(name); m != math1().end()) {
			Node n = make(NodeKind::Math1);
			n->fn = m->second;
			n->kids.push_back(arg());
			return n;
		}
		if (name == "log" || name == "min" || name == "max") {
			Node n = make(NodeKind::Math2);
			n->fn = name == "log" ? MathFn::Log : name == "min" ? MathFn::Min : MathFn::Max;
			expect("(");
			n->kids.push_back(boolOr());
			expect(",");
			n->kids.push_back(boolOr());
			expect(")");
			return n;
		}
		static const std::map<std::string, std::pair<Redirect, bool>> redirects = {
			{ "p2", { Redirect::P2, false } }, { "parent", { Redirect::Parent, false } },
			{ "root", { Redirect::Root, false } }, { "target", { Redirect::Target, true } },
			{ "helper", { Redirect::Helper, true } }, { "enemy", { Redirect::Enemy, true } },
			{ "enemynear", { Redirect::EnemyNear, true } }, { "partner", { Redirect::Partner, true } },
			{ "playerid", { Redirect::PlayerId, true } },
		};
		if (const auto r = redirects.find(name); r != redirects.end()) {
			// "p2, life" or "helper(1000), stateno": the value after the comma is read from the other character.
			Node n = make(NodeKind::Redirect);
			n->redirect = r->second.first;
			if (r->second.second && isSym("("))
				n->kids.push_back(arg());
			else
				n->kids.push_back(nullptr);
			expect(",");
			n->kids.push_back(unary());
			return n;
		}
		if (const auto ti = triggers().find(name); ti != triggers().end()) {
			Node n = make(NodeKind::Trigger);
			n->trig = ti->second.trig;
			if (ti->second.shape == Shape::Axis) {
				const std::string axis = ident();
				Node a = make(NodeKind::Num);
				a->value = Value::I(axis == "x" ? 0 : axis == "y" ? 1 : axis == "z" ? 2 : -1);
				if (a->value.i < 0)
					fail("expected x, y or z after " + name);
				n->kids.push_back(std::move(a));
			} else if (ti->second.shape == Shape::OneArg) {
				n->kids.push_back(arg());
			} else if (ti->second.shape == Shape::OptArg) {
				if (isSym("(")) {
					n->kids.push_back(arg());
				} else {
					Node any = make(NodeKind::Num);
					any->value = Value::I(-1);
					n->kids.push_back(std::move(any));
				}
			}
			return n;
		}
		pos_--;
		fail("unknown trigger");
	}

	std::vector<Token> t_;
	size_t pos_ = 0;
};

// --- Evaluation ------------------------------------------------------------------------------------------------------

Value arith(Op op, Value a, Value b)
{
	const bool fl = a.isFloat || b.isFloat;
	switch (op) {
	case Op::Add: return fl ? Value::F(a.toF() + b.toF()) : Value::I((int32_t)((uint32_t)a.i + (uint32_t)b.i));
	case Op::Sub: return fl ? Value::F(a.toF() - b.toF()) : Value::I((int32_t)((uint32_t)a.i - (uint32_t)b.i));
	case Op::Mul: return fl ? Value::F(a.toF() * b.toF()) : Value::I((int32_t)((int64_t)a.i * b.i));
	case Op::Div:
		// MUGEN gives "bottom", read as 0, for a division by zero.
		if (fl)
			return b.toF() == 0 ? Value::I(0) : Value::F(a.toF() / b.toF());
		if (b.i == 0 || (a.i == INT32_MIN && b.i == -1))
			return Value::I(0);
		return Value::I(a.i / b.i);
	case Op::Mod:
		if (fl)
			return b.toF() == 0 ? Value::I(0) : Value::F(std::fmod(a.toF(), b.toF()));
		if (b.i == 0 || (a.i == INT32_MIN && b.i == -1))
			return Value::I(0);
		return Value::I(a.i % b.i);
	case Op::Pow:
		// An int to a non-negative int power stays an int.
		if (!fl && b.i >= 0) {
			// Wraps like repeated 32-bit multiplication (square and multiply).
			uint32_t r = 1, base = (uint32_t)a.i;
			for (uint32_t e = (uint32_t)b.i; e; e >>= 1) {
				if (e & 1)
					r *= base;
				base *= base;
			}
			return Value::I((int32_t)r);
		}
		return Value::F(std::pow(a.toF(), b.toF()));
	case Op::Eq: return Value::B(fl ? a.toF() == b.toF() : a.i == b.i);
	case Op::Ne: return Value::B(fl ? a.toF() != b.toF() : a.i != b.i);
	case Op::Lt: return Value::B(fl ? a.toF() < b.toF() : a.i < b.i);
	case Op::Le: return Value::B(fl ? a.toF() <= b.toF() : a.i <= b.i);
	case Op::Gt: return Value::B(fl ? a.toF() > b.toF() : a.i > b.i);
	case Op::Ge: return Value::B(fl ? a.toF() >= b.toF() : a.i >= b.i);
	case Op::BitAnd: return Value::I(a.toI() & b.toI());
	case Op::BitOr: return Value::I(a.toI() | b.toI());
	case Op::BitXor: return Value::I(a.toI() ^ b.toI());
	case Op::BoolXor: return Value::B(a.toB() != b.toB());
	}
	return Value::I(0);
}

Value evalNode(const ExprNode *n, ExprContext &ctx)
{
	if (!n)
		return Value::I(0);
	switch (n->kind) {
	case NodeKind::Num: return n->value;
	case NodeKind::Neg: {
		const Value v = evalNode(n->kids[0].get(), ctx);
		return v.isFloat ? Value::F(-v.f) : Value::I((int32_t)(0u - (uint32_t)v.i));
	}
	case NodeKind::Not: return Value::B(!evalNode(n->kids[0].get(), ctx).toB());
	case NodeKind::BitNot: return Value::I(~evalNode(n->kids[0].get(), ctx).toI());
	case NodeKind::Binary:
		return arith(n->op, evalNode(n->kids[0].get(), ctx), evalNode(n->kids[1].get(), ctx));
	case NodeKind::AndAlso:
		return Value::B(evalNode(n->kids[0].get(), ctx).toB() && evalNode(n->kids[1].get(), ctx).toB());
	case NodeKind::OrElse:
		return Value::B(evalNode(n->kids[0].get(), ctx).toB() || evalNode(n->kids[1].get(), ctx).toB());
	case NodeKind::Range: {
		const Value v = evalNode(n->kids[0].get(), ctx);
		const Value lo = evalNode(n->kids[1].get(), ctx), hi = evalNode(n->kids[2].get(), ctx);
		const bool aboveLo = n->lowOpen ? arith(Op::Gt, v, lo).i : arith(Op::Ge, v, lo).i;
		const bool belowHi = n->highOpen ? arith(Op::Lt, v, hi).i : arith(Op::Le, v, hi).i;
		return Value::B((aboveLo && belowHi) != n->negate);
	}
	case NodeKind::Command: return Value::B(ctx.command(n->name) != n->negate);
	case NodeKind::FlagIs: {
		const char c = ctx.flag(n->flag);
		return Value::B((n->name.find(c) != std::string::npos) != n->negate);
	}
	case NodeKind::FlagValue: return Value::I(ctx.flag(n->flag));
	case NodeKind::AnimElem: {
		const int32_t t = ctx.trigger(Trig::AnimElemTime, Value::I(n->a)).toI();
		bool r;
		if (n->isRange) {
			const bool inside = (n->lowOpen ? t > n->b : t >= n->b) && (n->highOpen ? t < n->hi : t <= n->hi);
			r = (n->op == Op::Eq) == inside;
		} else if (n->hasCompare) {
			r = arith(n->op, Value::I(t), Value::I(n->b)).i != 0;
		} else {
			r = t == 0;
		}
		return Value::B(r != n->negate);
	}
	case NodeKind::TimeMod: {
		const int32_t time = ctx.trigger(Trig::Time, Value::I(0)).toI();
		const bool r = n->a != 0 && time % n->a == n->b;
		return Value::B(r != n->negate);
	}
	case NodeKind::HitDefAttr: return Value::B(ctx.hitDefAttr(n->a, n->b) != n->negate);
	case NodeKind::TextIs: return Value::B((text::toLower(ctx.text(n->text)) == n->name) != n->negate);
	case NodeKind::StageVar: {
		Value v = Value::I(0);
		std::string s;
		ctx.stageVar(n->key, v, s);
		return n->hasCompare ? Value::B((text::toLower(s) == n->name) != n->negate) : v;
	}
	case NodeKind::ProjFlag: {
		const int32_t t = ctx.trigger(n->trig, Value::I(n->a)).toI();
		const bool r = n->hasCompare ? t >= 0 && arith(n->op, Value::I(t), Value::I(n->b)).i != 0 : t == 0;
		return Value::B(r != n->negate);
	}
	case NodeKind::Const: {
		Value v;
		return ctx.constant(n->name, v) ? v : Value::I(0);
	}
	case NodeKind::GetHitVar: {
		Value v;
		return ctx.getHitVar(n->name, v) ? v : Value::I(0);
	}
	case NodeKind::Var: return ctx.var(n->var, evalNode(n->kids[0].get(), ctx).toI());
	case NodeKind::VarSet: {
		const int32_t index = evalNode(n->kids[0].get(), ctx).toI();
		Value v = evalNode(n->kids[1].get(), ctx);
		// Int variables keep ints, float ones floats.
		if (n->var == VarKind::Var || n->var == VarKind::SysVar)
			v = Value::I(v.toI());
		else
			v = Value::F(v.toF());
		ctx.setVar(n->var, index, v);
		return v;
	}
	case NodeKind::Trigger:
		return ctx.trigger(n->trig, n->kids.empty() ? Value::I(0) : evalNode(n->kids[0].get(), ctx));
	case NodeKind::Math1: {
		const Value v = evalNode(n->kids[0].get(), ctx);
		switch (n->fn) {
		case MathFn::Abs: return v.isFloat ? Value::F(std::fabs(v.f)) : Value::I(v.i < 0 ? -v.i : v.i);
		case MathFn::Floor: return Value::I((int32_t)std::floor(v.toF()));
		case MathFn::Ceil: return Value::I((int32_t)std::ceil(v.toF()));
		case MathFn::Sin: return Value::F(std::sin(v.toF()));
		case MathFn::Cos: return Value::F(std::cos(v.toF()));
		case MathFn::Tan: return Value::F(std::tan(v.toF()));
		case MathFn::Asin: return Value::F(std::asin(v.toF()));
		case MathFn::Acos: return Value::F(std::acos(v.toF()));
		case MathFn::Atan: return Value::F(std::atan(v.toF()));
		case MathFn::Exp: return Value::F(std::exp(v.toF()));
		case MathFn::Ln: return v.toF() <= 0 ? Value::I(0) : Value::F(std::log(v.toF()));
		case MathFn::Sign: return Value::I(v.toF() > 0 ? 1 : v.toF() < 0 ? -1 : 0);
		default: return Value::I(0);
		}
	}
	case NodeKind::Math2: {
		const Value a = evalNode(n->kids[0].get(), ctx), b = evalNode(n->kids[1].get(), ctx);
		switch (n->fn) {
		case MathFn::Log:
			// log(base, value)
			return a.toF() <= 0 || b.toF() <= 0 || a.toF() == 1 ? Value::I(0)
																 : Value::F(std::log(b.toF()) / std::log(a.toF()));
		case MathFn::Min: return arith(Op::Lt, a, b).i ? a : b;
		case MathFn::Max: return arith(Op::Gt, a, b).i ? a : b;
		default: return Value::I(0);
		}
	}
	case NodeKind::IfElse:
		return evalNode(n->kids[0].get(), ctx).toB() ? evalNode(n->kids[1].get(), ctx) : evalNode(n->kids[2].get(), ctx);
	case NodeKind::Redirect: {
		// Without an argument: Target and Helper are any one (Ikemen's id -1), the others the first.
		const bool anyId = n->redirect == Redirect::Target || n->redirect == Redirect::Helper;
		const int32_t arg = n->kids[0] ? evalNode(n->kids[0].get(), ctx).toI() : anyId ? -1 : 0;
		ExprContext *other = ctx.redirect(n->redirect, arg);
		return other ? evalNode(n->kids[1].get(), *other) : Value::I(0);
	}
	}
	return Value::I(0);
}

} // namespace

// --- Expr ------------------------------------------------------------------------------------------------------------

Expr::Expr() = default;
Expr::~Expr() = default;
Expr::Expr(Expr &&) noexcept = default;
Expr &Expr::operator=(Expr &&) noexcept = default;

bool Expr::parse(const std::string &textIn, std::string &error)
{
	root_.reset();
	source_ = textIn;
	std::vector<Token> tokens;
	if (!tokenize(textIn, tokens, error))
		return false;
	try {
		Parser p(std::move(tokens));
		Node n = p.expression();
		if (!p.atEnd()) {
			error = "unexpected '" + p.peek().text + "'";
			return false;
		}
		root_ = std::move(n);
		return true;
	} catch (const ParseError &e) {
		error = e.what;
		return false;
	}
}

Value Expr::eval(ExprContext &ctx) const
{
	return evalNode(root_.get(), ctx);
}

std::vector<Expr> parseExprList(const std::string &textIn, std::string &error)
{
	std::vector<Expr> out;
	std::vector<Token> tokens;
	if (!tokenize(textIn, tokens, error))
		return out;
	try {
		Parser p(std::move(tokens));
		while (!p.atEnd()) {
			Expr e;
			e.source_ = textIn;
			// An empty slot ("1,,3") keeps its place, empty.
			if (!(p.peek().kind == Token::Sym && p.peek().text == ","))
				e.root_ = p.expression();
			out.push_back(std::move(e));
			if (!p.takeSym(","))
				break;
		}
	} catch (const ParseError &e) {
		error = e.what;
	}
	return out;
}

} // namespace m360
