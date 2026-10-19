// Milestone 11: options and saving. 11a: the settings' defaults (Ikemen's defaultConfig.ini), their ranges, a save
// and load through save/config.ini's format, and the button mapping. 11c: unused buttons.

#include <cstdio>
#include <string>

#include "core/font.h"
#include "core/input.h"
#include "core/settings.h"

using namespace m360;

namespace {
int passed, failed;

void expect(bool value, const char *name)
{
	if (value)
		passed++;
	else {
		failed++;
		std::printf("FAIL  11: %s\n", name);
	}
}
} // namespace

int run11Tests()
{
	passed = failed = 0;

	Settings s;
	expect(s.difficulty == 5 && s.life == 100 && s.time == 99 && s.gameSpeed == 0 && s.matchWins == 2,
		"Ikemen's default options");
	expect(s.masterVolume == 100 && s.wavVolume == 80 && s.bgmVolume == 75, "Ikemen's default volumes");
	expect(s.roundTicks(60) == 99 * 60 && s.ticksPerSecond() == 60, "99 counts of 60 ticks, 60 ticks a second");
	expect(s.soundGain() == 1.0f, "default sound volume plays sounds as they are");

	s.difficulty = 12;
	s.life = 5;
	s.time = -7;
	s.gameSpeed = 20;
	s.matchWins = 0;
	s.bgmVolume = 150;
	s.clamp();
	expect(s.difficulty == 8 && s.life == 10 && s.time == -1 && s.gameSpeed == 9 && s.matchWins == 1 &&
			s.bgmVolume == 100,
		"values held inside Ikemen's ranges");
	expect(s.roundTicks(60) == -1 && s.ticksPerSecond() == 60 + 9 * 5, "time -1 is no clock; speed 9 runs 105 ticks");

	// Save and load back through the file.
	Settings saved;
	saved.difficulty = 3;
	saved.life = 150;
	saved.time = 60;
	saved.wavVolume = 40;
	saved.buttons[1][0] = Settings::ButtonLB;
	const std::string path = "tests/out/settings_test.ini";
	expect(saved.save(path), "settings save");
	Settings loaded;
	expect(loaded.load(path), "settings load");
	expect(loaded.difficulty == 3 && loaded.life == 150 && loaded.time == 60 && loaded.wavVolume == 40,
		"options survive a save and load");
	expect(loaded.buttons[1][0] == Settings::ButtonLB && loaded.buttons[0][0] == Settings::ButtonA,
		"player 2's remapped a button survives; player 1's keeps the default");

	// The default layout: X Y RB punches, A B RT kicks; player 2's a is LB now.
	platform::Pad pad;
	pad.rb = true;
	pad.lb = true;
	InputState one, two;
	loaded.toInput(0, pad, one);
	loaded.toInput(1, pad, two);
	expect(one.z && !one.a, "player 1: RB is z, LB is nothing");
	expect(two.a && two.z, "player 2: LB is a, RB is still z");

	// 11c: a MUGEN button with no pad button saves as "Not used", loads back that way and plays nothing.
	Settings unbound;
	unbound.buttons[0][5] = Settings::BUTTON_NONE;
	expect(unbound.save(path) && loaded.load(path) && loaded.buttons[0][5] == Settings::BUTTON_NONE,
		"an unused button survives a save and load");
	InputState none;
	loaded.toInput(0, pad, none);
	expect(!none.z, "an unused button plays nothing");
	platform::Pad held;
	expect(Settings::heldButton(held) == Settings::BUTTON_NONE, "no pad button held");
	held.lt = true;
	expect(Settings::heldButton(held) == Settings::ButtonLT, "the key config sees LT held");

	// The options screen's TrueType font (ikemen1's font7, Open Sans) loads and measures text.
	Font openSans;
	std::string error;
	expect(openSans.load("data/font/Open_Sans.def", error), "a TrueType font loads (Open Sans)");
	expect(openSans.textWidth("Game Settings", 0) > openSans.textWidth("Game", 0) && openSans.textWidth("Game", 0) > 0,
		"TrueType text widths grow with the text");

	std::printf("11 options: %d passed, %d failed\n", passed, failed);
	return failed;
}
