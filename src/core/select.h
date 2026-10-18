#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/game_config.h"
#include "core/sff.h"
#include "core/sprite_renderer.h"
#include "platform/platform.h"

namespace m360 {

// The character select screen, MUGEN's select.def flow kept simple: a grid of the roster's small portraits (sprite
// 9000,0), each player's cursor with the big portrait (9000,1) and name; a, b, c, x, y, z choose the palette (1-6) as
// in MUGEN. Player 2 picks with a second controller, or the computer picks at random. Then the stage, then "VS".
class SelectScreen {
public:
	// Loads each character's name and portraits (the rest of its sprites are dropped) and each stage's name.
	bool load(const GameConfig &config, std::string &error);
	// A new selection; p2Cpu: the computer picks for player 2.
	void reset(bool p2Cpu);
	void update(const platform::Pad &p1, const platform::Pad &p2);
	void draw() const;
	// Picks made, "VS" shown: the fight can start with fight().
	bool done() const { return phase_ == Phase::Done; }
	// The chosen characters, palettes and stage, as the fight's config.
	GameConfig fight() const;

private:
	struct Entry {
		std::string def, name;
		// Portraits are drawn in 320-wide units: a 1280-wide character's are a quarter the size.
		float scale = 1;
		std::unique_ptr<Sff> portraits;
		std::unique_ptr<SpriteRenderer> renderer;
	};
	struct Player {
		int cursor = 0;
		int palette = 1;
		bool chosen = false;
	};
	enum class Phase { Characters, Stage, Versus, Done };

	// A controller's buttons this frame and last, for presses.
	struct Edges {
		platform::Pad now, before;
		bool pressed(bool platform::Pad::*button) const { return now.*button && !(before.*button); }
	};
	void move(Player &p, const Edges &e);
	// The palette a button picks (a 1, b 2, c 3, x 4, y 5, z 6), or 0.
	static int paletteButton(const Edges &e);
	// palette: the character's palette n (1,n), or 0 for the sprite's own.
	void drawPortrait(const Entry &entry, int32_t number, float x, float y, float scale, bool mirror, int palette) const;
	int32_t random(int32_t n);

	GameConfig base_;
	std::vector<Entry> roster_;
	std::vector<std::string> stages_, stageNames_;
	Player players_[2];
	int stage_ = 0;
	bool p2Cpu_ = true;
	Phase phase_ = Phase::Characters;
	int32_t timer_ = 0;
	Edges pads_[2];
	uint32_t random_ = 0x9E3779B9u;
};

} // namespace m360
