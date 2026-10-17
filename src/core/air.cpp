#include "core/air.h"

#include <algorithm>
#include <cctype>

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

bool isNumeric(std::string_view s)
{
	s = text::trim(s);
	if (s.empty())
		return false;
	size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
	bool digits = false, dot = false;
	for (; i < s.size(); i++) {
		if (s[i] >= '0' && s[i] <= '9')
			digits = true;
		else if (s[i] == '.' && !dot)
			dot = true;
		else
			return false;
	}
	return digits;
}

// "AS128D64": the source alpha from start, then 'd' and the destination's.
void parseAlpha(const std::string &a, size_t i, uint8_t defSrc, uint8_t defDst, uint8_t &src, uint8_t &dst)
{
	src = defSrc;
	dst = defDst;
	int alp = 0;
	for (; i < a.size() && a[i] >= '0' && a[i] <= '9'; i++)
		alp = std::min(alp * 10 + (a[i] - '0'), 0x3FFF);
	src = (uint8_t)std::min(alp, 255);
	if (i < a.size() && a[i] == 'd') {
		i++;
		if (i < a.size() && a[i] >= '0' && a[i] <= '9') {
			alp = 0;
			for (; i < a.size() && a[i] >= '0' && a[i] <= '9'; i++)
				alp = std::min(alp * 10 + (a[i] - '0'), 0x3FFF);
			dst = (uint8_t)std::min(alp, 255);
		}
	}
}

Clsn normalize(Clsn r)
{
	if (r[0] > r[2])
		std::swap(r[0], r[2]);
	if (r[1] > r[3])
		std::swap(r[1], r[3]);
	return r;
}

} // namespace

bool readAnimFrame(const std::string &line, AnimFrame &af, std::string &error)
{
	if (line.empty() || ((line[0] < '0' || line[0] > '9') && line[0] != '-'))
		return false;
	std::vector<std::string> ary = text::split(line, ',', 10);
	if (ary.size() < 5) {
		error = "invalid animation element definition";
		return false;
	}
	af = AnimFrame{};
	af.group = text::atoi(ary[0]);
	af.number = text::atoi(ary[1]);
	af.xoffset = text::atoi(ary[2]);
	af.yoffset = text::atoi(ary[3]);
	af.time = text::atoi(ary[4]);

	if (ary.size() >= 6 && !ary[5].empty()) {
		for (char ch : ary[5]) {
			switch (ch) {
			case 'H':
			case 'h':
				af.hflip = -1;
				af.xoffset = -af.xoffset;
				break;
			case 'V':
			case 'v':
				af.vflip = -1;
				af.yoffset = -af.yoffset;
				break;
			default: error = std::string("invalid flip flag: ") + ch; break;
			}
		}
	}

	if (ary.size() >= 7 && !ary[6].empty()) {
		std::string a = ary[6];
		const size_t ia = a.find_first_of("ASas");
		if (ia != std::string::npos)
			a = a.substr(ia);
		a = text::toLower(a);
		if (a == "a1") {
			af.trans = Trans::Add;
			af.srcAlpha = 255;
			af.dstAlpha = 128;
		} else if (text::startsWith(a, "sas")) {
			af.trans = Trans::SubAdd;
			parseAlpha(a, 3, 255, 255, af.srcAlpha, af.dstAlpha);
		} else if (text::startsWith(a, "sa")) {
			af.trans = Trans::SubAdd;
			af.srcAlpha = af.dstAlpha = 255;
		} else if (text::startsWith(a, "as")) {
			af.trans = Trans::Add;
			parseAlpha(a, 2, 255, 0, af.srcAlpha, af.dstAlpha);
		} else if (text::startsWith(a, "ss")) {
			af.trans = Trans::Sub;
			parseAlpha(a, 2, 255, 255, af.srcAlpha, af.dstAlpha);
		} else if (!a.empty() && a[0] == 'a') {
			af.trans = Trans::Add;
			af.srcAlpha = af.dstAlpha = 255;
		} else if (!a.empty() && a[0] == 's') {
			af.trans = Trans::Sub;
			af.srcAlpha = af.dstAlpha = 255;
		} else {
			error = "invalid transparency: " + a;
		}
	}

	auto optionalFloat = [&](size_t index, float &out, const char *what) {
		if (ary.size() > index && !ary[index].empty()) {
			if (isNumeric(ary[index]))
				out = (float)text::atof(ary[index]);
			else
				error = std::string("invalid ") + what + ": " + ary[index];
		}
	};
	optionalFloat(7, af.xscale, "x-scale");
	optionalFloat(8, af.yscale, "y-scale");
	optionalFloat(9, af.angle, "angle");
	return true;
}

// --- Parsing ---------------------------------------------------------------------------------------------------------

namespace {

class AirParser {
public:
	AirParser(const std::string &filename, const std::string &body) : filename_(filename), lines_(text::splitLines(body)) {}

	void run(std::map<int32_t, Animation> &table)
	{
		while (i_ < lines_.size()) {
			int32_t no = 0;
			Animation a;
			if (!readAction(no, a)) {
				i_++;
				continue;
			}
			// The first of duplicate action numbers wins.
			if (table.count(no))
				continue;
			// An action with no frames takes the next one's, as MUGEN does.
			while (a.frames.empty() && a.copyAction < 0 && i_ < lines_.size()) {
				int32_t nextNo = 0;
				Animation next;
				if (readAction(nextNo, next)) {
					if (!table.count(nextNo))
						table[nextNo] = next;
					a = next;
					break;
				}
				i_++;
			}
			table[no] = a;
		}
	}

private:
	bool readAction(int32_t &no, Animation &a)
	{
		std::string name, rest;
		for (; i_ < lines_.size(); i_++) {
			text::sectionName(lines_[i_], name, rest);
			if (!name.empty())
				break;
		}
		if (name != "begin ")
			return false;
		const size_t space = rest.find(' ');
		if (space == std::string::npos || text::toLower(rest.substr(0, space + 1)) != "action ")
			return false;
		i_++;
		no = text::atoi(rest.substr(space + 1));
		action_ = no;
		readAnimation(a);
		return true;
	}

	void readAnimation(Animation &a)
	{
		int32_t ols = 0;
		std::vector<Clsn> clsn1, clsn1d, clsn2, clsn2d;
		bool def1 = true, def2 = true;
		for (; i_ < lines_.size(); i_++) {
			if (!lines_[i_].empty() && lines_[i_][0] == '[')
				break;
			const std::string line = text::toLower(text::stripComment(lines_[i_]));
			if (text::startsWith(line, "copy action ")) {
				const std::string num(text::trim(std::string_view(line).substr(12)));
				a.copyAction = num.empty() ? -1 : text::atoi(num);
				return;
			}
			AnimFrame af;
			std::string error;
			const bool isFrame = readAnimFrame(line, af, error);
			if (!error.empty())
				warn(error);
			if (isFrame) {
				ols = a.loopstart;
				if (def1)
					clsn1 = clsn1d;
				if (def2)
					clsn2 = clsn2d;
				af.clsn1 = clsn1;
				af.clsn2 = clsn2;
				a.frames.push_back(af);
				def1 = def2 = true;
			} else if (text::startsWith(line, "loopstart")) {
				a.loopstart = (int32_t)a.frames.size();
			} else if (text::startsWith(line, "interpolate offset")) {
				a.interpolateOffset.push_back((int32_t)a.frames.size());
			} else if (text::startsWith(line, "interpolate scale")) {
				a.interpolateScale.push_back((int32_t)a.frames.size());
			} else if (text::startsWith(line, "interpolate angle")) {
				a.interpolateAngle.push_back((int32_t)a.frames.size());
			} else if (text::startsWith(line, "interpolate blend")) {
				a.interpolateBlend.push_back((int32_t)a.frames.size());
			} else if (line.size() >= 5 && text::startsWith(line, "clsn")) {
				const size_t colon = line.find(':');
				if (colon == std::string::npos)
					continue;
				const int32_t size = text::atoi(std::string_view(line).substr(colon + 1));
				if (size < 0)
					continue;
				const bool isDefault = line.size() >= 12 && line.compare(5, 7, "default") == 0;
				if (line[4] == '1') {
					clsn1 = readClsn('1', size);
					if (isDefault)
						clsn1d = clsn1;
					def1 = false;
				} else if (line[4] == '2') {
					clsn2 = readClsn('2', size);
					if (isDefault)
						clsn2d = clsn2;
					def2 = false;
				}
			}
		}

		if ((size_t)a.loopstart >= a.frames.size())
			a.loopstart = ols;
		if (a.frames.empty()) {
		} else if (a.frames.back().time == -1) {
			a.totaltime = -1;
		} else {
			int32_t tmp = 0;
			for (size_t f = 0; f < a.frames.size(); f++) {
				const int32_t t = a.frames[f].time;
				if (t == -1) {
					a.totaltime = 0;
					a.looptime = -tmp;
					a.prelooptime = 0;
				}
				a.totaltime += t;
				if ((int32_t)f < a.loopstart) {
					a.prelooptime += t;
					tmp += t;
				} else {
					a.looptime += t;
				}
			}
			if (a.totaltime == -1)
				a.prelooptime = 0;
		}
	}

	std::vector<Clsn> readClsn(char type, int32_t size)
	{
		std::vector<Clsn> clsn((size_t)size, Clsn{ 0, 0, 0, 0 });
		if (size == 0)
			return clsn;
		std::vector<bool> filled((size_t)size, false);
		const size_t header = i_;
		i_++;
		int32_t count = 0;
		while (count < size && i_ < lines_.size()) {
			const std::string r = text::toLower(text::stripComment(lines_[i_]));
			if (r.empty()) {
				i_++;
				continue;
			}
			if (!text::startsWith(r, "clsn"))
				break;
			const size_t eq = r.find('=');
			if (eq == std::string::npos)
				break;
			int32_t index = -1;
			const size_t open = r.find('[');
			if (open != std::string::npos && open < eq) {
				const size_t close = r.find(']', open);
				if (close != std::string::npos) {
					const std::string idx(text::trim(std::string_view(r).substr(open + 1, close - open - 1)));
					if (!idx.empty() && std::all_of(idx.begin(), idx.end(), [](char c) { return c >= '0' && c <= '9'; }))
						index = text::atoi(idx);
				}
			}
			// An index that's absent, out of range or repeated takes the first free slot, so no box is lost.
			if (index < 0 || index >= size || filled[(size_t)index]) {
				index = -1;
				for (int32_t k = 0; k < size; k++) {
					if (!filled[(size_t)k]) {
						index = k;
						break;
					}
				}
				if (index < 0) {
					i_++;
					continue;
				}
			}
			const std::vector<std::string> coords = text::split(std::string_view(r).substr(eq + 1), ',');
			if (coords.size() < 4)
				break;
			clsn[(size_t)index] = normalize({ (float)text::atoi(coords[0]), (float)text::atoi(coords[1]),
				(float)text::atoi(coords[2]), (float)text::atoi(coords[3]) });
			filled[(size_t)index] = true;
			count++;
			i_++;
		}
		// The loop above stopped on the line after the last box; the caller's loop moves past it.
		i_--;
		if (count != size)
			warn("line " + std::to_string(header + 1) + ": Clsn" + type + " declares " + std::to_string(size) +
				" boxes but has " + std::to_string(count));
		return clsn;
	}

	void warn(const std::string &what) { logf("air: %s, action %d: %s", filename_.c_str(), action_, what.c_str()); }

	std::string filename_;
	std::vector<std::string> lines_;
	size_t i_ = 0;
	int32_t action_ = 0;
};

} // namespace

void AnimationTable::parse(const std::string &filename, const std::string &body)
{
	AirParser(filename, body).run(anims_);
	resolveCopyAction();
}

bool AnimationTable::load(const std::string &path, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes)) {
		error = "can't read " + path;
		return false;
	}
	anims_.clear();
	parse(path, std::string(bytes.begin(), bytes.end()));
	logf("air: %s: %zu actions", path.c_str(), anims_.size());
	return true;
}

void AnimationTable::resolveCopyAction()
{
	std::vector<int32_t> unresolved;
	for (auto &[no, a] : anims_) {
		for (int loops = 0; a.copyAction >= 0 && loops < 8; loops++) {
			const auto target = anims_.find(a.copyAction);
			if (target == anims_.end() || &target->second == &a)
				break;
			if (target->second.copyAction >= 0) {
				a.copyAction = target->second.copyAction;
				continue;
			}
			a = target->second;
			a.copyAction = -1;
			break;
		}
		if (a.copyAction >= 0)
			unresolved.push_back(no);
	}
	for (int32_t no : unresolved)
		anims_.erase(no);
}

const Animation *AnimationTable::find(int32_t no) const
{
	const auto it = anims_.find(no);
	return it == anims_.end() ? nullptr : &it->second;
}

bool AnimationTable::get(int32_t no, Animation &out) const
{
	const Animation *a = find(no);
	if (!a)
		return false;
	out = *a;
	out.reset();
	return true;
}

// --- Playback --------------------------------------------------------------------------------------------------------

void Animation::reset()
{
	curelem_ = drawidx_ = 0;
	curelemtime_ = curtime_ = 0;
	newframe_ = true;
	loopend_ = false;
	loopcount_ = 0;
	spriteGroup_ = spriteNumber_ = -1;
	updateSprite();
}

int32_t Animation::animElemTime(int32_t elem) const
{
	if ((size_t)elem > frames.size()) {
		const int32_t t = animTime();
		return t > 0 ? 0 : t;
	}
	const int32_t e = std::max(0, elem) - 1;
	int32_t t = curtime_;
	for (int32_t i = 0; i < e; i++)
		t -= std::max(0, frames[(size_t)i].time);
	return t;
}

int32_t Animation::animElemNo(int32_t time) const
{
	const int32_t n = (int32_t)frames.size();
	if (n > 0) {
		int32_t i = curelem_, oldt = 0;
		if (time <= 0) {
			time += curelemtime_;
			bool loop = false;
			for (;;) {
				if (time >= 0)
					return i + 1;
				i--;
				if (i < 0 || (curelem_ >= loopstart && i < loopstart)) {
					if (time == oldt)
						break;
					oldt = time;
					loop = true;
					i = n - 1;
				}
				time += std::max(0, frames[(size_t)i].time);
				if (loop && i == n - 1 && frames[(size_t)i].time == -1)
					return i + 1;
			}
		} else {
			time += curelemtime_;
			for (;;) {
				time -= std::max(0, frames[(size_t)i].time);
				if (time < 0 || (i == n - 1 && frames[(size_t)i].time == -1))
					return i + 1;
				if (++i >= n) {
					if (time == oldt)
						break;
					oldt = time;
					i = loopstart;
				}
			}
		}
	}
	return n;
}

void Animation::setAnimElem(int32_t elem, int32_t elemtime)
{
	if (frames.empty())
		return;
	curelem_ = std::max(0, elem - 1);
	// MUGEN sets an element past the last to the first.
	if ((size_t)curelem_ >= frames.size())
		curelem_ = 0;
	drawidx_ = curelem_;
	if (elemtime != 0) {
		const int32_t frametime = frames[(size_t)curelem_].time;
		if (elemtime < 0 || (frametime != -1 && elemtime >= frametime))
			elemtime = 0;
	}
	curelemtime_ = elemtime;
	newframe_ = true;
	loopend_ = false;
	loopcount_ = 0;
	updateSprite();
	curtime_ = 0;
	curtime_ = -animElemTime(curelem_ + 1) + curelemtime_;
}

void Animation::animSeek(int32_t elem)
{
	const int32_t n = (int32_t)frames.size();
	if (elem < 0)
		elem = 0;
	bool flip = true;
	for (;;) {
		curelem_ = elem;
		while (curelem_ < n && cur().time <= 0) {
			if (curelem_ == n - 1 && cur().time == -1)
				break;
			curelem_++;
		}
		if (curelem_ < n)
			break;
		flip = !flip;
		if (flip) {
			curelem_ = n - 1;
			break;
		}
	}
	curelem_ = std::clamp(curelem_, 0, n - 1);
}

void Animation::updateSprite()
{
	if (frames.empty())
		return;
	if (totaltime > 0) {
		// Back to the loop start when the action ends.
		if (curtime_ >= totaltime) {
			curelemtime_ = 0;
			newframe_ = true;
			curelem_ = loopstart;
		}
		animSeek(curelem_);
		if (prelooptime < 0 && curtime_ >= totaltime + prelooptime && curtime_ >= totaltime - looptime &&
			(curtime_ == totaltime + prelooptime || curtime_ == totaltime - looptime)) {
			curelemtime_ = 0;
			newframe_ = true;
			curelem_ = 0;
		}
	}
	if (newframe_ && cur().time != 0) {
		// MUGEN shows no sprite for group or number -1; other negatives wrap as unsigned.
		if (cur().group != -1 && cur().number != -1) {
			spriteGroup_ = (uint16_t)cur().group;
			spriteNumber_ = (uint16_t)cur().number;
		} else {
			spriteGroup_ = spriteNumber_ = -1;
		}
	}
	newframe_ = false;
	drawidx_ = curelem_;
	scaleX_ = frames[(size_t)drawidx_].xscale;
	scaleY_ = frames[(size_t)drawidx_].yscale;
	updateInterpolation();
}

void Animation::updateInterpolation()
{
	interpOffsetX_ = interpOffsetY_ = 0;
	const AnimFrame &now = frames[(size_t)drawidx_];
	const int32_t next = (size_t)drawidx_ >= frames.size() - 1 ? loopstart : drawidx_ + 1;
	const float progress = now.time > 0 ? (float)curelemtime_ / (float)cur().time : 0.0f;
	if (progress <= 0)
		return;
	const AnimFrame &to = frames[(size_t)next];
	auto has = [next](const std::vector<int32_t> &marks) { return std::find(marks.begin(), marks.end(), next) != marks.end(); };
	if (has(interpolateOffset)) {
		interpOffsetX_ = (float)(to.xoffset - now.xoffset) * progress;
		interpOffsetY_ = (float)(to.yoffset - now.yoffset) * progress;
	}
	if (has(interpolateScale)) {
		scaleX_ += (to.xscale - now.xscale) * progress;
		scaleY_ += (to.yscale - now.yscale) * progress;
	}
	// Angle and blend interpolation wait until the renderer can rotate and blend by amount.
}

void Animation::action()
{
	if (frames.empty()) {
		loopend_ = true;
		return;
	}
	updateSprite();
	const int32_t n = (int32_t)frames.size();
	auto next = [&]() {
		if (totaltime != -1 || curelem_ < n - 1) {
			curelemtime_ = 0;
			newframe_ = true;
			for (;;) {
				curelem_++;
				if ((totaltime == -1 && curelem_ == n - 1) || curelem_ >= n || cur().time > 0)
					break;
			}
		}
	};
	if (cur().time <= 0)
		next();
	if (curelem_ < n) {
		curelemtime_++;
		if (curelemtime_ >= cur().time) {
			next();
			if (curelem_ >= n)
				curelem_ = loopstart;
		}
	} else {
		curelem_ = loopstart;
	}
	if (totaltime != -1 && curtime_ >= totaltime) {
		curtime_ = totaltime - looptime;
		loopcount_++;
	}
	curtime_++;
	if (totaltime != -1 && curtime_ >= totaltime)
		loopend_ = true;
}

} // namespace m360
