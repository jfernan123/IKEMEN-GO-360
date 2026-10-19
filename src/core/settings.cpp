#include "core/settings.h"

#include <algorithm>
#include <vector>

#include "core/file.h"
#include "core/ini.h"
#include "core/input.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

const char *const BUTTON_NAMES[Settings::BUTTON_COUNT] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start", "Back" };
// [Joystick_Pn]'s keys for MUGEN's buttons, in the order of Settings::buttons.
const char *const MUGEN_NAMES[Settings::MUGEN_BUTTONS] = { "A", "B", "C", "X", "Y", "Z", "Start" };

bool pressed(const platform::Pad &pad, uint8_t button)
{
	switch (button) {
	case Settings::ButtonA: return pad.a;
	case Settings::ButtonB: return pad.b;
	case Settings::ButtonX: return pad.x;
	case Settings::ButtonY: return pad.y;
	case Settings::ButtonLB: return pad.lb;
	case Settings::ButtonRB: return pad.rb;
	case Settings::ButtonLT: return pad.lt;
	case Settings::ButtonRT: return pad.rt;
	case Settings::ButtonStart: return pad.start;
	case Settings::ButtonBack: return pad.back;
	default: return false;
	}
}

} // namespace

// Ikemen-GO's defaultConfig.ini values; the buttons are mugen360's layout (X Y RB punches, A B RT kicks, Start).
void Settings::reset()
{
	difficulty = 5;
	life = 100;
	time = 99;
	gameSpeed = 0;
	gameSpeedStep = 5;
	matchWins = 2;
	maxDrawGames = 1;
	simulWins = 2;
	masterVolume = 100;
	wavVolume = 80;
	bgmVolume = 75;
	const uint8_t layout[MUGEN_BUTTONS] = { ButtonA, ButtonB, ButtonRT, ButtonX, ButtonY, ButtonRB, ButtonStart };
	for (auto &player : buttons)
		for (int i = 0; i < MUGEN_BUTTONS; i++)
			player[i] = layout[i];
}

// Ikemen's ValidateConfig ranges, and the options menu's limits.
void Settings::clamp()
{
	difficulty = std::clamp(difficulty, 1, 8);
	life = std::clamp(life, 10, 300);
	time = std::clamp(time, -1, 1000);
	gameSpeed = std::clamp(gameSpeed, -9, 9);
	gameSpeedStep = std::clamp(gameSpeedStep, 1, 60);
	matchWins = std::clamp(matchWins, 1, 10);
	maxDrawGames = std::clamp(maxDrawGames, -2, 10);
	simulWins = std::clamp(simulWins, 1, 10);
	masterVolume = std::clamp(masterVolume, 0, 200);
	for (int32_t *v : { &wavVolume, &bgmVolume })
		*v = std::clamp(*v, 0, 100);
	for (auto &player : buttons)
		for (uint8_t &b : player)
			if (b >= BUTTON_COUNT)
				b = ButtonA;
}

bool Settings::load(const std::string &path)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes))
		return false;
	const std::vector<IniSection> sections = parseIni(std::string(bytes.begin(), bytes.end()));
	if (const IniSection *o = findSection(sections, "options")) {
		o->readInt("difficulty", difficulty);
		o->readInt("life", life);
		o->readInt("time", time);
		o->readInt("gamespeed", gameSpeed);
		o->readInt("gamespeedstep", gameSpeedStep);
		o->readInt("match.wins", matchWins);
		o->readInt("match.maxdrawgames", maxDrawGames);
		o->readInt("simul.match.wins", simulWins);
	}
	if (const IniSection *s = findSection(sections, "sound")) {
		s->readInt("mastervolume", masterVolume);
		s->readInt("wavvolume", wavVolume);
		s->readInt("bgmvolume", bgmVolume);
	}
	for (int p = 0; p < 2; p++) {
		const IniSection *j = findSection(sections, "joystick_p" + std::to_string(p + 1));
		if (!j)
			continue;
		for (int i = 0; i < MUGEN_BUTTONS; i++) {
			const std::string value = text::toLower(j->text(text::toLower(MUGEN_NAMES[i])));
			for (uint8_t b = 0; b < BUTTON_COUNT; b++) {
				if (value == text::toLower(BUTTON_NAMES[b]))
					buttons[p][i] = b;
			}
		}
	}
	clamp();
	logf("settings: %s: difficulty %d, life %d%%, time %d, speed %d, wins %d", path.c_str(), (int)difficulty, (int)life,
		(int)time, (int)gameSpeed, (int)matchWins);
	return true;
}

bool Settings::save(const std::string &path) const
{
	std::string out = "; mugen360's settings, in Ikemen-GO's config.ini names. The options menu writes this file.\n";
	out += "[Options]\n";
	out += "Difficulty = " + std::to_string(difficulty) + "\n";
	out += "Life = " + std::to_string(life) + "\n";
	out += "Time = " + std::to_string(time) + "\n";
	out += "GameSpeed = " + std::to_string(gameSpeed) + "\n";
	out += "GameSpeedStep = " + std::to_string(gameSpeedStep) + "\n";
	out += "Match.Wins = " + std::to_string(matchWins) + "\n";
	out += "Match.MaxDrawGames = " + std::to_string(maxDrawGames) + "\n";
	out += "Simul.Match.Wins = " + std::to_string(simulWins) + "\n\n";
	out += "[Sound]\n";
	out += "MasterVolume = " + std::to_string(masterVolume) + "\n";
	out += "WavVolume = " + std::to_string(wavVolume) + "\n";
	out += "BGMVolume = " + std::to_string(bgmVolume) + "\n";
	for (int p = 0; p < 2; p++) {
		out += "\n[Joystick_P" + std::to_string(p + 1) + "]\n";
		for (int i = 0; i < MUGEN_BUTTONS; i++)
			out += std::string(MUGEN_NAMES[i]) + " = " + BUTTON_NAMES[buttons[p][i]] + "\n";
	}
	const bool written = platform::writeWholeFile(path.c_str(), out);
	logf("settings: %s %s", written ? "saved to" : "couldn't save", path.c_str());
	forgetCachedFile(path);
	return written;
}

const char *Settings::buttonName(uint8_t button)
{
	return button < BUTTON_COUNT ? BUTTON_NAMES[button] : "?";
}

const char *Settings::mugenButtonName(int index)
{
	return index >= 0 && index < MUGEN_BUTTONS ? MUGEN_NAMES[index] : "?";
}

void Settings::toInput(int player, const platform::Pad &pad, InputState &in) const
{
	const uint8_t *map = buttons[player == 1 ? 1 : 0];
	in.up = pad.up;
	in.down = pad.down;
	in.left = pad.left;
	in.right = pad.right;
	in.a = pressed(pad, map[0]);
	in.b = pressed(pad, map[1]);
	in.c = pressed(pad, map[2]);
	in.x = pressed(pad, map[3]);
	in.y = pressed(pad, map[4]);
	in.z = pressed(pad, map[5]);
	in.s = pressed(pad, map[6]);
}

} // namespace m360
