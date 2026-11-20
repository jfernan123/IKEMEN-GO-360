#pragma once

// What main.cpp and the fight, viewers and tools around it share: the frame counter, the viewers' layout,
// buttons that fire once or repeat, the loading screen and a few drawing helpers.

#include <cstdint>
#include <cstring>
#include <string>

#include "core/formats/air.h"
#include "core/render/debug_text.h"
#include "core/render/sprite_renderer.h"
#include "core/util/log.h"
#include "platform/platform.h"

namespace m360 {

// Frames played since the program started, the seed for real matches: when a player reaches a fight varies. Test and
// training runs leave it at 0 unless --charseed sets it.
inline uint32_t playedFrames = 0;

const char *const FIGHTFX_SFF = "data/fightfx.sff";
const char *const FIGHTFX_AIR = "data/fightfx.air";
const char *const COMMON_SND = "data/common.snd";
constexpr float SCALE = 3.0f;
// Where the viewers stand the character: MUGEN puts a sprite's axis at the character's feet.
constexpr float GROUND_X = platform::SCREEN_WIDTH / 2.0f;
constexpr float GROUND_Y = 600.0f;
constexpr uint32_t BACKGROUND = 0xFF203048;
constexpr uint32_t TEXT = 0xFFFFFFFF;
constexpr uint32_t DIM = 0xFF90A0B8;
constexpr uint32_t ERROR_TEXT = 0xFFFF6060;

// Put a frame on screen before the external game's large SFFs and screenpack start loading. Drawing it once for
// each 360 front buffer also makes the message visible immediately through Xenia's VdSwap path.
inline void showLoadingScreen(const std::string &game)
{
	std::string name = game.empty() ? std::string("MUGEN360") : game;
	const size_t slash = name.find_last_of("/\\");
	if (slash != std::string::npos)
		name = name.substr(slash + 1);
	const char *heading = "MUGEN360";
	const char *message = "LOADING GAME...";
	for (int frame = 0; frame < 3; frame++) {
		if (!platform::beginFrame(0xFF080C18))
			return;
		platform::fillRect(0, 0, platform::SCREEN_WIDTH, 10, 0xFF3058A0);
		platform::fillRect(0, platform::SCREEN_HEIGHT - 10, platform::SCREEN_WIDTH, 10, 0xFFC03038);
		const float headingScale = 9.0f, nameScale = 4.0f, messageScale = 3.0f;
		drawText(heading, (platform::SCREEN_WIDTH - std::strlen(heading) * 6.0f * headingScale) / 2.0f, 220,
			headingScale, 0xFFFFFFFF);
		drawText(name.c_str(),
			(platform::SCREEN_WIDTH - name.size() * 6.0f * nameScale) / 2.0f, 340, nameScale, 0xFFFFD050);
		drawText(message, (platform::SCREEN_WIDTH - std::strlen(message) * 6.0f * messageScale) / 2.0f, 430,
			messageScale, 0xFFB8C8E8);
		platform::fillRect(380, 500, 520, 6, 0xFF283850);
		platform::fillRect(380, 500, 130, 6, 0xFF60A0E8);
		platform::endFrame();
		SpriteRenderer::newFrame();
	}
}

// A button that fires once when pressed, then repeats while held.
class Repeat {
public:
	bool update(bool down)
	{
		held_ = down ? held_ + 1 : 0;
		return held_ == 1 || (held_ > 20 && held_ % 3 == 0);
	}

private:
	int held_ = 0;
};

// A button that fires once per press.
class Press {
public:
	bool update(bool down)
	{
		const bool fired = down && !was_;
		was_ = down;
		return fired;
	}

private:
	bool was_ = false;
};

struct Buttons {
	Repeat left, right, lb, rb, up, down, b;
	Press a, x, y, menu, back;
};

// Changes a renderer's palette with up / down: -1 (each sprite's own) then the SFF's palettes in turn.
inline void updatePalette(Buttons &b, const platform::Pad &pad, SpriteRenderer &renderer)
{
	const int palettes = (int)renderer.sff().palettes().size();
	int palette = renderer.palette();
	if (b.up.update(pad.up))
		palette = palette + 1 >= palettes ? -1 : palette + 1;
	if (b.down.update(pad.down))
		palette = palette - 1 < -1 ? palettes - 1 : palette - 1;
	if (palette != renderer.palette()) {
		renderer.setPalette(palette);
		renderer.clear();
	}
}

inline void drawGround()
{
	platform::fillRect(0, GROUND_Y, platform::SCREEN_WIDTH, 1, 0x60FFFFFF);
	platform::fillRect(GROUND_X, 150, 1, GROUND_Y - 150 + 40, 0x60FFFFFF);
}

// A box's outline and a faint fill, in MUGEN coordinates around the character's position.
inline void drawClsn(const Clsn &box, int facing, uint32_t rgb)
{
	float l = box[0] * facing, r = box[2] * facing;
	if (l > r)
		std::swap(l, r);
	const float x = GROUND_X + l * SCALE, y = GROUND_Y + box[1] * SCALE;
	const float w = (r - l) * SCALE, h = (box[3] - box[1]) * SCALE;
	platform::fillRect(x, y, w, h, 0x30000000 | rgb);
	platform::fillRect(x, y, w, 2, 0xE0000000 | rgb);
	platform::fillRect(x, y + h - 2, w, 2, 0xE0000000 | rgb);
	platform::fillRect(x, y, 2, h, 0xE0000000 | rgb);
	platform::fillRect(x + w - 2, y, 2, h, 0xE0000000 | rgb);
}

inline const char *transName(Trans t)
{
	switch (t) {
	case Trans::Add: return "ADD";
	case Trans::Sub: return "SUB";
	case Trans::SubAdd: return "SUBADD";
	default: return "NONE";
	}
}

// What the 360 has free and what the textures hold, in the log: a big game ran a real console out of memory (13).
inline void logMemory(const char *where)
{
	size_t free = 0;
	if (platform::freeMemory(free))
		logf("memory: %s: %zu MB free, %zu MB of textures", where, free >> 20, SpriteRenderer::totalTextureBytes() >> 20);
}

} // namespace m360
