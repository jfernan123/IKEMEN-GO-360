#include "core/screens/neural_select.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "core/render/debug_text.h"
#include "core/util/file.h"
#include "core/util/ini.h"
#include "core/util/log.h"
#include "core/util/text.h"

namespace m360 {

namespace {

constexpr uint32_t BACKGROUND = 0xFF101826, PANEL = 0xFF1C2A40, CURSOR = 0xFFFFD040, TEXT = 0xFFFFFFFF,
				   DIM = 0xFF8090A8, CPU = 0xFF60C0FF;

void frame(float x, float y, float width, float height, float thickness, uint32_t argb)
{
	platform::fillRect(x, y, width, thickness, argb);
	platform::fillRect(x, y + height - thickness, width, thickness, argb);
	platform::fillRect(x, y, thickness, height, argb);
	platform::fillRect(x + width - thickness, y, thickness, height, argb);
}

std::string upper(std::string s)
{
	for (char &c : s)
		c = (char)std::toupper((unsigned char)c);
	return s;
}

// Text centred on x.
void centred(const std::string &line, float x, float y, float scale, uint32_t argb)
{
	drawText(line.c_str(), x - (float)line.size() * 6.0f * scale / 2.0f, y, scale, argb);
}

} // namespace

bool NeuralSelect::loadEntry(const std::string &def, Entry &out)
{
	out.def = def;
	std::vector<uint8_t> bytes;
	if (!readFile(def, bytes)) {
		logf("neural select: can't read %s; left out", def.c_str());
		return false;
	}
	const std::vector<IniSection> sections = parseIni(std::string(bytes.begin(), bytes.end()));
	const IniSection *info = findSection(sections, "info");
	const IniSection *files = findSection(sections, "files");
	if (info) {
		out.name = text::trim(info->text("displayname"));
		if (out.name.empty())
			out.name = text::trim(info->text("name"));
	}
	if (out.name.size() >= 2 && out.name.front() == '"' && out.name.back() == '"')
		out.name = out.name.substr(1, out.name.size() - 2);
	if (out.name.empty())
		out.name = def;
	if (!files)
		return true;
	// The big portrait only, read from the sprite file in pieces (a character's sprites can be tens of MB).
	const std::string dir = directoryOf(def);
	const std::vector<std::pair<uint16_t, uint16_t>> keep = { { 9000, 1 } };
	out.sff = std::make_unique<Sff>();
	const std::string act(text::trim(files->text("pal1")));
	out.sff->setActPalettes(!act.empty());
	std::string error;
	if (!out.sff->loadSome(findBesideDef(dir, files->text("sprite")), keep, error, true)) {
		logf("neural select: %s", error.c_str());
		out.sff.reset();
		return true;
	}
	Palette palette;
	if (!act.empty() && readActPalette(findBesideDef(dir, act), palette))
		out.sff->setPalette(1, 1, palette);
	out.sff->keepOnly(keep);
	out.renderer = std::make_unique<SpriteRenderer>(*out.sff);
	return true;
}

bool NeuralSelect::load(const std::string &cpuDef, const std::vector<Opponent> &opponents, std::string &error)
{
	entries_.clear();
	if (!loadEntry(cpuDef, cpu_)) {
		error = "can't read the neural CPU's character " + cpuDef;
		return false;
	}
	for (const Opponent &o : opponents) {
		Entry e;
		if (!loadEntry(o.def, e))
			continue;
		e.trainedWins = o.trainedWins;
		entries_.push_back(std::move(e));
	}
	if (entries_.empty()) {
		error = "none of the neural CPU's opponents are in this game";
		return false;
	}
	reset();
	return true;
}

void NeuralSelect::reset()
{
	done_ = cancelled_ = false;
	time_ = 0;
	// Whatever is held when the screen opens doesn't count as a press.
	last_ = platform::Pad{};
	last_.a = last_.b = last_.x = last_.y = last_.rb = last_.rt = last_.start = last_.back = true;
}

void NeuralSelect::update(const platform::Pad &pad)
{
	time_++;
	const auto pressed = [&](bool now, bool before) { return now && !before; };
	const int count = (int)entries_.size();
	if (pressed(pad.left, last_.left))
		cursor_ = (cursor_ + count - 1) % count;
	if (pressed(pad.right, last_.right))
		cursor_ = (cursor_ + 1) % count;
	const bool confirm = pressed(pad.a, last_.a) || pressed(pad.b, last_.b) || pressed(pad.x, last_.x) ||
		pressed(pad.y, last_.y) || pressed(pad.rb, last_.rb) || pressed(pad.rt, last_.rt) || pressed(pad.start, last_.start);
	if (confirm)
		done_ = true;
	if (pressed(pad.back, last_.back))
		cancelled_ = true;
	last_ = pad;
}

void NeuralSelect::drawPortrait(Entry &entry, float x, float y, float width, float height, bool mirrored)
{
	const Sprite *sprite = entry.sff ? entry.sff->get(9000, 1) : nullptr;
	if (!sprite || sprite->width == 0 || sprite->height == 0) {
		centred(entry.name, x + width / 2, y + height / 2 - 7, 2, DIM);
		return;
	}
	const float scale = std::min(width / (float)sprite->width, height / (float)sprite->height);
	SpriteDraw how;
	how.scaleX = mirrored ? -scale : scale;
	how.scaleY = scale;
	const float left = x + (width - (float)sprite->width * scale) / 2;
	const float top = y + (height - (float)sprite->height * scale) / 2;
	// drawSprite places the sprite's axis at the point; a mirrored one runs leftwards from it.
	const float axisX = mirrored ? left + ((float)sprite->width - (float)sprite->axisX) * scale : left + (float)sprite->axisX * scale;
	entry.renderer->drawSprite(9000, 1, axisX, top + (float)sprite->axisY * scale, how);
}

void NeuralSelect::draw()
{
	const float w = (float)platform::SCREEN_WIDTH;
	platform::fillRect(0, 0, w, (float)platform::SCREEN_HEIGHT, BACKGROUND);
	centred("VS NEURAL CAP", w / 2, 30, 6, CURSOR);
	centred("A CAPTAIN AMERICA TRAINED BY REINFORCEMENT LEARNING", w / 2, 82, 2, DIM);

	// The computer, on the left as it trained.
	platform::fillRect(40, 130, 360, 440, PANEL);
	drawPortrait(cpu_, 50, 140, 340, 380, false);
	centred("CPU: " + upper(cpu_.name), 220, 532, 2, CPU);
	centred("NEURAL POLICY", 220, 552, 2, DIM);
	centred("VS", 470, 330, 6, TEXT);

	// The opponents, one of which the player plays on the right.
	const int count = (int)entries_.size();
	const float boxWidth = 140, boxHeight = 200, gap = 12;
	const float left = 560 + (680 - (count * boxWidth + (count - 1) * gap)) / 2;
	for (int i = 0; i < count; i++) {
		const float x = left + (float)i * (boxWidth + gap);
		platform::fillRect(x, 160, boxWidth, boxHeight, PANEL);
		drawPortrait(entries_[(size_t)i], x + 4, 164, boxWidth - 8, boxHeight - 8, true);
		const bool here = i == cursor_;
		if (here)
			frame(x - 4, 156, boxWidth + 8, boxHeight + 8, 4, (time_ / 8) % 2 ? CURSOR : 0xFFFFFFFF);
	}
	const Entry &chosen = entries_[(size_t)cursor_];
	centred("YOU: " + upper(chosen.name), 900, 400, 4, TEXT);
	char line[96];
	std::snprintf(line, sizeof line, "THE AI BEAT %s %d%% OF THE TIME IN TRAINING", upper(chosen.name).c_str(),
		(int)(chosen.trainedWins * 100 + 0.5f));
	centred(line, 900, 450, 2, DIM);
	centred("YOU PLAY ON THE RIGHT", 900, 480, 2, DIM);

	centred("LEFT / RIGHT: CHOOSE    ATTACK OR START: FIGHT    BACK: TITLE", w / 2, 660, 2, DIM);
}

} // namespace m360
