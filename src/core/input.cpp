#include "core/input.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "core/file.h"
#include "core/ini.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

constexpr int K(Key k)
{
	return (int)k;
}

bool isDirection(Key k)
{
	return k <= Key::N;
}

bool isButton(Key k)
{
	return k >= Key::a && k < Key::Count;
}

// The single directions a key is made of: UF is U and F.
std::vector<Key> components(Key k)
{
	switch (k) {
	case Key::UB: return { Key::U, Key::B };
	case Key::UF: return { Key::U, Key::F };
	case Key::DB: return { Key::D, Key::B };
	case Key::DF: return { Key::D, Key::F };
	case Key::UL: return { Key::U, Key::L };
	case Key::UR: return { Key::U, Key::R };
	case Key::DL: return { Key::D, Key::L };
	case Key::DR: return { Key::D, Key::R };
	default: return { k };
	}
}

// The four directions a key is judged against: up, down and back / forward, or left / right for L and R keys.
std::vector<Key> axes(Key k)
{
	switch (k) {
	case Key::L:
	case Key::R:
	case Key::UL:
	case Key::UR:
	case Key::DL:
	case Key::DR: return { Key::U, Key::D, Key::L, Key::R };
	default: return { Key::U, Key::D, Key::B, Key::F };
	}
}

// The axes' directions that aren't part of the key: holding any of them spoils a plain direction.
std::vector<Key> conflicts(Key k)
{
	std::vector<Key> out;
	const std::vector<Key> c = components(k);
	for (Key a : axes(k)) {
		if (std::find(c.begin(), c.end(), a) == c.end())
			out.push_back(a);
	}
	return out;
}

int32_t clamp32(int64_t v)
{
	return (int32_t)std::clamp<int64_t>(v, INT32_MIN, INT32_MAX);
}

} // namespace

// --- InputBuffer -----------------------------------------------------------------------------------------------------

void InputBuffer::reset()
{
	std::fill(std::begin(cur_), std::end(cur_), 0);
	std::fill(std::begin(prev_), std::end(prev_), 0);
}

void InputBuffer::update(const InputState &in, int facing)
{
	std::copy(std::begin(cur_), std::end(cur_), std::begin(prev_));
	// Opposite directions together cancel out, as Ikemen's default SOCD resolution does.
	bool up = in.up, down = in.down, left = in.left, right = in.right;
	if (up && down)
		up = down = false;
	if (left && right)
		left = right = false;
	const bool back = facing >= 0 ? left : right;
	const bool fwd = facing >= 0 ? right : left;
	auto tick = [this](Key k, bool held) {
		int32_t &t = cur_[K(k)];
		if (held != (t > 0))
			t = held ? 1 : -1;
		else
			t += held ? 1 : -1;
	};
	tick(Key::U, up);
	tick(Key::D, down);
	tick(Key::L, left);
	tick(Key::R, right);
	tick(Key::B, back);
	tick(Key::F, fwd);
	tick(Key::N, !(up || down || left || right));
	tick(Key::a, in.a);
	tick(Key::b, in.b);
	tick(Key::c, in.c);
	tick(Key::x, in.x);
	tick(Key::y, in.y);
	tick(Key::z, in.z);
	tick(Key::s, in.s);
	tick(Key::d, in.d);
	tick(Key::w, in.w);
	tick(Key::m, in.m);
}

int32_t InputBuffer::state(Key key, bool tilde, bool dollarFlag, bool starFlag) const
{
	const bool dollar = dollarFlag && !starFlag, star = starFlag && !dollarFlag, simple = !dollar && !star;
	auto c = [this](Key k) { return cur_[K(k)]; };
	auto p = [this](Key k) { return prev_[K(k)]; };
	// Released, or held the frame before: a release that's happening now.
	auto recent = [&](Key k) { return c(k) < 0 || p(k) > 0; };

	if (isButton(key)) {
		if (!tilde)
			return c(key);
		return recent(key) ? -c(key) : 0;
	}
	if (key == Key::N) {
		if (dollar) {
			// "$N": any change at all.
			int32_t m = INT32_MAX;
			for (Key k : { Key::U, Key::D, Key::B, Key::F, Key::a, Key::b, Key::c, Key::x, Key::y, Key::z, Key::s,
					 Key::d, Key::w })
				m = std::min(m, std::abs(c(k)));
			return m;
		}
		if (simple)
			return tilde ? -c(Key::N) : c(Key::N);
		return 0;
	}

	const std::vector<Key> comps = components(key), confl = conflicts(key);
	// The plain hold: every part held, nothing else on the axes.
	auto strict = [&]() {
		int64_t intended = INT32_MAX, conflict = INT32_MIN;
		for (Key k : comps)
			intended = std::min<int64_t>(intended, c(k));
		for (Key k : confl)
			conflict = std::max<int64_t>(conflict, c(k));
		return clamp32(std::min<int64_t>(-conflict, intended));
	};
	const bool diagonal = comps.size() == 2;

	if (!tilde) {
		if (simple)
			return strict();
		if (star)
			return diagonal ? std::max(c(comps[0]), c(comps[1])) : c(comps[0]);
		// "$": the parts held whatever else is; how long since anything on the axes changed.
		for (Key k : comps) {
			if (c(k) <= 0)
				return 0;
		}
		int32_t m = INT32_MAX;
		for (Key k : axes(key))
			m = std::min(m, std::abs(c(k)));
		return m;
	}

	if (simple) {
		for (Key k : comps) {
			if (!recent(k))
				return 0;
		}
		return clamp32(-(int64_t)strict());
	}
	if (star) {
		if (!diagonal)
			return recent(comps[0]) ? -c(comps[0]) : 0;
		if (recent(comps[0]) || recent(comps[1]))
			return -std::max(c(comps[0]), c(comps[1]));
		return 0;
	}
	// "~$"
	for (Key k : comps) {
		if (!recent(k))
			return 0;
	}
	bool anyReleased = false;
	int32_t lowest = INT32_MAX;
	for (Key k : comps) {
		anyReleased = anyReleased || c(k) < 0;
		lowest = std::min(lowest, c(k));
	}
	if (anyReleased)
		return -lowest;
	int32_t m = INT32_MAX;
	for (Key k : confl)
		m = std::min(m, std::abs(c(k)));
	return m;
}

int32_t InputBuffer::charge(Key key, bool tilde, bool dollarFlag, bool starFlag) const
{
	const bool dollar = dollarFlag && !starFlag, star = starFlag && !dollarFlag, simple = !dollar && !star;
	auto c = [this](Key k) { return cur_[K(k)]; };
	auto p = [this](Key k) { return prev_[K(k)]; };
	if (isButton(key))
		return tilde ? p(key) : c(key);
	if (key == Key::N)
		return tilde ? p(Key::N) : c(Key::N);
	const std::vector<Key> comps = components(key), confl = conflicts(key);
	const bool diagonal = comps.size() == 2;
	if (!simple) {
		// "$" and "*": how long the direction itself was held, now or (for a release) up to the frame before.
		auto v = [&](Key k) { return tilde ? p(k) : c(k); };
		return diagonal ? std::max(v(comps[0]), v(comps[1])) : v(comps[0]);
	}
	// A direction just pressed doesn't spoil a charge it releases into.
	auto ignoreRecent = [](int32_t v) -> int64_t { return v == 1 ? INT32_MIN : v; };
	int64_t intended = INT32_MAX, conflict = INT32_MIN;
	for (Key k : comps)
		intended = std::min<int64_t>(intended, tilde ? p(k) : c(k));
	for (Key k : confl)
		conflict = std::max<int64_t>(conflict, tilde ? ignoreRecent(c(k)) : c(k));
	return clamp32(std::max<int64_t>(0, std::min<int64_t>(-conflict, intended)));
}

// --- Commands --------------------------------------------------------------------------------------------------------

bool CommandStep::isSingleDirection() const
{
	return keys.size() == 1 && isDirection(keys[0].key);
}

// Whether this step and the next can complete on the same frame: a motion ending as a button goes down.
bool CommandStep::isDirToButton(const CommandStep &next) const
{
	for (const CommandStepKey &k : next.keys) {
		if (k.slash)
			return false;
	}
	for (const CommandStepKey &k : keys) {
		if (isButton(k.key))
			return false;
	}
	for (const CommandStepKey &k : keys) {
		for (const CommandStepKey &n : next.keys) {
			if (k.key == n.key)
				return false;
		}
	}
	for (const CommandStepKey &n : next.keys) {
		if (isButton(n.key) && !n.tilde)
			return true;
	}
	for (const CommandStepKey &k : keys) {
		if (isDirection(k.key) && k.tilde) {
			for (const CommandStepKey &n : next.keys) {
				if (!(isDirection(n.key) && n.tilde))
					return true;
			}
		}
	}
	return false;
}

bool Command::parse(const std::string &textIn, std::string &error)
{
	steps_.clear();
	const std::string all(text::trim(textIn));
	if (all.empty())
		return true;
	const std::vector<std::string> stepTexts = text::split(all, ',');
	for (size_t si = 0; si < stepTexts.size(); si++) {
		CommandStep cs;
		const std::string &stepText = stepTexts[si];
		if (si > 0 && stepText.empty()) {
			error = "empty command step";
			continue;
		}
		std::vector<std::string> parts;
		if (stepText.find('|') != std::string::npos) {
			cs.orLogic = true;
			parts = text::split(stepText, '|');
		} else {
			parts = text::split(stepText, '+');
		}
		for (std::string part : parts) {
			size_t i = 0;
			auto skipSpaces = [&]() {
				while (i < part.size() && (part[i] == ' ' || part[i] == '\t'))
					i++;
			};
			auto number = [&]() {
				int32_t n = 0;
				while (i < part.size() && part[i] >= '0' && part[i] <= '9')
					n = n * 10 + (part[i++] - '0');
				skipSpaces();
				return n;
			};
			CommandStepKey k;
			// Prefix symbols.
			for (bool more = true; more && i < part.size();) {
				switch (part[i]) {
				case '>': cs.greater = true, i++, skipSpaces(); break;
				case '~': {
					k.tilde = true, i++, skipSpaces();
					if (const int32_t n = number())
						k.chargetime = n;
					break;
				}
				case '/': {
					k.slash = true, i++, skipSpaces();
					if (const int32_t n = number())
						k.chargetime = n;
					break;
				}
				case '$': k.dollar = true, i++, skipSpaces(); break;
				case '*': k.star = true, i++, skipSpaces(); break;
				default: more = false; break;
				}
			}
			if (i >= part.size()) {
				error = "no key in command step";
				continue;
			}
			const char c0 = part[i];
			const char c1 = i + 1 < part.size() ? part[i + 1] : '\0';
			switch (c0) {
			case 'U':
			case 'D':
			case 'B':
			case 'F':
			case 'L':
			case 'R':
			case 'N': {
				static const std::pair<char, Key> single[] = { { 'U', Key::U }, { 'D', Key::D }, { 'B', Key::B },
					{ 'F', Key::F }, { 'L', Key::L }, { 'R', Key::R }, { 'N', Key::N } };
				for (const auto &[ch, key] : single) {
					if (ch == c0)
						k.key = key;
				}
				if ((c0 == 'U' || c0 == 'D') && (c1 == 'B' || c1 == 'F' || c1 == 'L' || c1 == 'R')) {
					const bool u = c0 == 'U';
					k.key = c1 == 'B' ? (u ? Key::UB : Key::DB)
						: c1 == 'F'   ? (u ? Key::UF : Key::DF)
						: c1 == 'L'   ? (u ? Key::UL : Key::DL)
									  : (u ? Key::UR : Key::DR);
				}
				break;
			}
			case 'a': k.key = Key::a; break;
			case 'b': k.key = Key::b; break;
			case 'c': k.key = Key::c; break;
			case 'x': k.key = Key::x; break;
			case 'y': k.key = Key::y; break;
			case 'z': k.key = Key::z; break;
			case 's': k.key = Key::s; break;
			case 'd': k.key = Key::d; break;
			case 'w': k.key = Key::w; break;
			case 'm': k.key = Key::m; break;
			default: error = std::string("invalid symbol '") + c0 + "'"; continue;
			}
			if (isButton(k.key))
				k.dollar = k.star = false;
			cs.keys.push_back(k);
		}
		steps_.push_back(cs);
	}
	autoGreaterExpand();
	completed_.assign(steps_.size(), false);
	stepTimers_.assign(steps_.size(), 0);

	// Steps are checked last to first, so one input can't complete two steps on one frame; a motion running into a
	// button ("isDirToButton") is checked first to last instead, so both can complete together.
	loopOrder_.clear();
	for (int i = (int)steps_.size() - 1; i >= 0;) {
		if (i > 0 && steps_[(size_t)i - 1].isDirToButton(steps_[(size_t)i])) {
			int start = i - 1;
			while (start > 0 && steps_[(size_t)start - 1].isDirToButton(steps_[(size_t)start]))
				start--;
			for (int j = start; j <= i; j++)
				loopOrder_.push_back((size_t)j);
			i = start - 1;
		} else {
			loopOrder_.push_back((size_t)i--);
		}
	}
	return error.empty();
}

// "F, F" becomes "F, >~F, >F": the second press must come after a release, with nothing else between.
void Command::autoGreaterExpand()
{
	if (steps_.size() < 2)
		return;
	auto same = [](const CommandStep &a, const CommandStep &b) { return a.greater == b.greater && a.keys == b.keys; };
	std::vector<CommandStep> out{ steps_[0] };
	for (size_t i = 1; i < steps_.size(); i++) {
		const CommandStep &prev = steps_[i - 1], &curr = steps_[i];
		if (prev.isSingleDirection() && curr.isSingleDirection() && same(prev, curr)) {
			CommandStep release;
			release.greater = true;
			CommandStepKey k;
			k.key = curr.keys[0].key;
			k.tilde = !curr.keys[0].tilde;
			k.dollar = curr.keys[0].dollar;
			k.star = curr.keys[0].star;
			release.keys = { k };
			CommandStep again = curr;
			again.greater = true;
			out.push_back(release);
			out.push_back(again);
		} else {
			out.push_back(curr);
		}
	}
	steps_ = out;
}

void Command::clear(bool resetBuffer)
{
	curtime_ = 0;
	if (resetBuffer)
		curbuftime = 0;
	std::fill(completed_.begin(), completed_.end(), false);
	std::fill(stepTimers_.begin(), stepTimers_.end(), 0);
}

// Whether a key pressed or released just now that the step doesn't name breaks a ">" step.
bool Command::greaterCheckFail(size_t i, const InputBuffer &buffer) const
{
	bool useLR = false;
	for (const CommandStepKey &sk : steps_[i].keys) {
		switch (sk.key) {
		case Key::L:
		case Key::R:
		case Key::UL:
		case Key::UR:
		case Key::DL:
		case Key::DR: useLR = true; break;
		default: break;
		}
	}
	auto check = [&](Key k) {
		for (bool tilde : { false, true }) {
			if (buffer.state(k, tilde, false, false) != 1)
				continue;
			bool allowed = false;
			for (const CommandStepKey &sk : steps_[i].keys)
				allowed = allowed || (sk.key == k && sk.tilde == tilde);
			if (!allowed)
				return true;
			if (!tilde)
				return false;
		}
		return false;
	};
	for (Key k : { Key::U, Key::D }) {
		if (check(k))
			return true;
	}
	if (useLR) {
		for (Key k : { Key::L, Key::R, Key::UL, Key::UR, Key::DL, Key::DR }) {
			if (check(k))
				return true;
		}
	} else {
		for (Key k : { Key::B, Key::F, Key::UF, Key::UB, Key::DF, Key::DB }) {
			if (check(k))
				return true;
		}
	}
	for (Key k : { Key::a, Key::b, Key::c, Key::x, Key::y, Key::z, Key::s, Key::d, Key::w, Key::m }) {
		if (check(k))
			return true;
	}
	return false;
}

void Command::step(const InputBuffer &buffer)
{
	if (curbuftime > 0)
		curbuftime--;
	completeframe = false;
	if (steps_.empty())
		return;

	// Completed steps age; one held too long (steptime) lapses.
	bool anyDone = false;
	for (size_t i = 0; i < steps_.size(); i++) {
		if (!completed_[i])
			continue;
		stepTimers_[i]++;
		if (maxsteptime > 0 && stepTimers_[i] > maxsteptime) {
			completed_[i] = false;
			stepTimers_[i] = 0;
			continue;
		}
		anyDone = true;
	}
	if (anyDone)
		curtime_++;
	else if (curtime_ > 0)
		clear(false);

	for (size_t i : loopOrder_) {
		if (i > 0 && !completed_[i - 1])
			continue;
		const CommandStep &s = steps_[i];
		auto keyOk = [&](const CommandStepKey &k) {
			const int32_t t = buffer.state(k.key, k.tilde, k.dollar, k.star);
			bool ok = k.slash ? t > 0 : t == 1;
			if (ok && k.chargetime > 1 && buffer.charge(k.key, k.tilde, k.dollar, k.star) < k.chargetime)
				ok = false;
			return ok;
		};
		bool matched;
		if (s.orLogic) {
			matched = std::any_of(s.keys.begin(), s.keys.end(), keyOk);
		} else {
			matched = !s.keys.empty() && std::all_of(s.keys.begin(), s.keys.end(), keyOk);
		}
		if (s.greater && i > 0 && completed_[i - 1] && !completed_[i] && greaterCheckFail(i, buffer)) {
			matched = false;
			completed_[i - 1] = false;
			stepTimers_[i - 1] = 0;
		}
		if (matched) {
			completed_[i] = true;
			stepTimers_[i] = 0;
			if (i > 0) {
				completed_[i - 1] = false;
				stepTimers_[i - 1] = 0;
			}
			if (i == 0)
				curtime_ = 0;
		}
	}

	completeframe = completed_.back();
	if (!completeframe && curtime_ < maxtime)
		return;
	// Done, or out of time.
	clear(false);
	if (completeframe)
		curbuftime = std::max(curbuftime, maxbuftime);
}

// --- CommandList -----------------------------------------------------------------------------------------------------

bool CommandList::load(const std::string &cmdPath, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(cmdPath, bytes)) {
		error = "can't read " + cmdPath;
		return false;
	}
	const std::vector<IniSection> sections = parseIni(std::string(bytes.begin(), bytes.end()));
	int32_t defTime = 15, defBufTime = 1, defStepTime = -1;
	if (const IniSection *d = findSection(sections, "defaults")) {
		d->readInt("command.time", defTime);
		d->readInt("command.steptime", defStepTime);
		int32_t bt = 0;
		if (d->readInt("command.buffer.time", bt))
			defBufTime = std::max(1, bt);
	}
	commands_.clear();
	byName_.clear();
	int problems = 0;
	for (const IniSection &s : sections) {
		if (!text::startsWith(s.lower, "command"))
			continue;
		Command cm;
		cm.name = s.text("name");
		cm.maxtime = defTime;
		cm.maxbuftime = defBufTime;
		cm.maxsteptime = defStepTime;
		s.readInt("time", cm.maxtime);
		s.readInt("steptime", cm.maxsteptime);
		if (cm.maxsteptime <= 0)
			cm.maxsteptime = cm.maxtime;
		int32_t bt = 0;
		if (s.readInt("buffer.time", bt))
			cm.maxbuftime = std::max(1, bt);
		std::string parseError;
		if (!cm.parse(s.get("command"), parseError)) {
			// MUGEN characters still run with a bad command; it just never comes out as written.
			logf("cmd: %s: \"%s\" = %s: %s", cmdPath.c_str(), cm.name.c_str(), s.get("command").c_str(),
				parseError.c_str());
			problems++;
		}
		byName_[cm.name].push_back(commands_.size());
		commands_.push_back(std::move(cm));
	}
	logf("cmd: %s: %zu commands, %zu names%s", cmdPath.c_str(), commands_.size(), byName_.size(),
		problems ? ", some with problems" : "");
	return true;
}

void CommandList::reset()
{
	buffer_.reset();
	for (Command &c : commands_)
		c.clear(true);
}

void CommandList::update(const InputState &in, int facing)
{
	buffer_.update(in, facing);
	std::vector<std::string> completed;
	for (Command &c : commands_) {
		c.step(buffer_);
		if (c.completeframe)
			completed.push_back(c.name);
	}
	// A command done stops its namesakes half way, so one motion doesn't fire the same move again with each button
	// ("piano" inputs).
	for (const std::string &name : completed) {
		for (size_t i : byName_[name]) {
			if (!commands_[i].completeframe && commands_[i].shared)
				commands_[i].clear(false);
		}
	}
}

bool CommandList::active(const std::string &name) const
{
	const auto it = byName_.find(name);
	if (it == byName_.end())
		return false;
	for (size_t i : it->second) {
		if (commands_[i].curbuftime > 0)
			return true;
	}
	return false;
}

std::vector<std::string> CommandList::activeNames() const
{
	std::vector<std::string> names;
	for (const auto &[name, list] : byName_) {
		if (active(name))
			names.push_back(name);
	}
	return names;
}

} // namespace m360
