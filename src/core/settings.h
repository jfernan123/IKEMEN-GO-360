#pragma once

#include <cstdint>
#include <string>

#include "platform/platform.h"

namespace m360 {

struct InputState;

// The game options a player can change and that stay between runs: the subset of Ikemen-GO's config.ini that
// mugen360 uses (src/resources/defaultConfig.ini [Options], [Sound], [Joystick_Pn]; config.go's clamping), saved to
// save/config.ini as Ikemen does.
struct Settings {
	// [Options]
	int32_t difficulty;   // Difficulty: the computer's AI level, 1-8
	int32_t life;         // Life: % of each character's life, 10-300
	int32_t time;         // Time: round time in counts, -1 for none
	int32_t gameSpeed;    // GameSpeed: -9..9 steps of GameSpeedStep frames a second from 60
	int32_t gameSpeedStep;
	int32_t matchWins;    // Match.Wins: rounds to win a single match
	int32_t maxDrawGames; // Match.MaxDrawGames
	int32_t simulWins;    // Simul.Match.Wins
	// [Sound], 0-100
	int32_t masterVolume, wavVolume, bgmVolume;

	// The pad buttons, and which one plays each MUGEN button (a b c x y z s) for player 1 and 2 ([Joystick_Pn]).
	enum Button : uint8_t { ButtonA, ButtonB, ButtonX, ButtonY, ButtonLB, ButtonRB, ButtonLT, ButtonRT, ButtonStart, ButtonBack, BUTTON_COUNT };
	static constexpr int MUGEN_BUTTONS = 7;
	uint8_t buttons[2][MUGEN_BUTTONS];

	Settings() { reset(); }
	// Ikemen's defaults.
	void reset();
	// Holds every value inside Ikemen's ranges (config.go's ValidateConfig).
	void clamp();
	// Reads the file over the defaults; false when it isn't there (the defaults stay).
	bool load(const std::string &path);
	bool save(const std::string &path) const;

	// The round's length in ticks for a fight screen counting framesPerCount ticks a second, -1 for none (Ikemen's
	// setRoundTime(Options.Time * framespercount)).
	int32_t roundTicks(int32_t framesPerCount) const { return time < 0 ? -1 : time * framesPerCount; }
	// Logic ticks per second (Ikemen: 60 + GameSpeed * GameSpeedStep).
	int32_t ticksPerSecond() const { return 60 + gameSpeed * gameSpeedStep; }
	// The sound effects' gain (Ikemen's Normalizer: 0.5 * Wav * Master / 10000, against its default 80 / 100).
	float soundGain() const { return (float)(wavVolume * masterVolume) / 8000.0f; }

	static const char *buttonName(uint8_t button);
	static const char *mugenButtonName(int index);
	// Player n's pad as MUGEN's buttons, through the mapping.
	void toInput(int player, const platform::Pad &pad, InputState &in) const;
};

// Where the settings live, as in Ikemen.
constexpr const char *SETTINGS_PATH = "save/config.ini";

} // namespace m360
