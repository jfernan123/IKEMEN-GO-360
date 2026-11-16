#pragma once

// The resources every fight shares, loaded once.

#include <cstdint>
#include <string>
#include <vector>

#include "app/shared.h"
#include "core/fight/effects.h"
#include "core/formats/sound.h"
#include "core/game/game_config.h"
#include "core/game/settings.h"
#include "core/screens/fight_screen.h"
#include "core/util/file.h"
#include "core/util/ini.h"

namespace m360 {

// What every fight uses and no fight changes: the shared effects (fightfx), the shared sounds (common.snd) and the
// screenpack's fight screen. Loaded with the first fight and kept, so later fights don't read them again (the 360's
// drives are slow); each fight resets their state.
struct FightAssets {
	FightEffects effects;
	SoundBank commonSound;
	FightScreen fightScreen;
	bool loaded = false, fightScreenLoaded = false;
	std::string fightError;
	// The player's options (main owns them; the options screen changes them between fights).
	const Settings *settings = nullptr;

	bool load(const GameConfig &config, std::string &error, bool simulationOnly = false)
	{
		if (loaded)
			return true;
		if (simulationOnly) {
			fightError = "simulation only";
			if (!effects.load(FIGHTFX_SFF, FIGHTFX_AIR, error, true))
				return false;
			loaded = true;
			return true;
		}
		// The screenpack's localcoord, for a fight.def that gives none.
		int32_t motifW = 320, motifH = 240;
		std::vector<uint8_t> motifBytes;
		if (!config.motif.empty() && readFile(config.motif, motifBytes)) {
			const std::vector<IniSection> motifIni = parseIni(std::string(motifBytes.begin(), motifBytes.end()));
			if (const IniSection *info = findSection(motifIni, "info"))
				info->ints("localcoord", { &motifW, &motifH });
		}
		fightScreenLoaded = !config.fight.empty() && fightScreen.load(config.fight, fightError, motifW, motifH);
		if (config.fight.empty())
			fightError = "none";
		// The fight screen's own fightfx and common.snd, else the shared ones (found in the game's folder first).
		const bool own = fightScreenLoaded && !fightScreen.fightfxSff().empty() && !fightScreen.fightfxAir().empty();
		if (!effects.load(own ? fightScreen.fightfxSff() : FIGHTFX_SFF, own ? fightScreen.fightfxAir() : FIGHTFX_AIR,
				error) ||
			!commonSound.load(fightScreenLoaded && !fightScreen.commonSnd().empty() ? fightScreen.commonSnd() : COMMON_SND,
				error))
			return false;
		loaded = true;
		return true;
	}
};

} // namespace m360
