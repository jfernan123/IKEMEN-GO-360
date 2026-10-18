#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/bgdef.h"
#include "core/camera.h"
#include "core/ini.h"
#include "core/layout.h"
#include "core/music.h"
#include "core/sff.h"
#include "core/sound.h"
#include "core/sprite_renderer.h"

namespace m360 {

// A screenpack's system.def: its sprites, sounds, fonts and actions, which the title, select and VS screens draw
// from (Ikemen's motif loading, src/motif.go). Each screen reads its own section ("[Title Info]") through section().
class Motif {
public:
	Motif() = default;
	Motif(const Motif &) = delete;
	Motif &operator=(const Motif &) = delete;

	bool load(const std::string &defPath, std::string &error);

	const std::string &path() const { return path_; }
	const ScreenSpace &space() const { return space_; }
	int32_t localWidth() const { return localWidth_; }
	int32_t localHeight() const { return localHeight_; }
	// The view a screenpack background draws with: no camera, the motif's localcoord on the screen.
	View view() const;
	const Sff &sff() const { return sff_; }
	SpriteRenderer &renderer() { return *renderer_; }
	FontTable &fonts() { return fonts_; }
	const AnimationTable &anims() const { return anims_; }
	const std::vector<IniSection> &sections() const { return sections_; }
	// A section by name, or null.
	const IniSection *section(const std::string &name) const { return findSection(sections_, name); }
	// Loads a background set ("titlebg") and its [<prefix>def] clear colour (0xAARRGGBB; black if none).
	void loadBg(const std::string &prefix, BgDef &bg, uint32_t &clearColour) const;
	// A layout that names its font by file gets that font (see resolveFontFile).
	void resolveFont(TextLayout &text) { resolveFontFile(text, fonts_, fontFiles_, path_); }
	void play(const int32_t snd[2]) const;
	// A screen's song from [Music] ("title", "select", "vs"): "<screen>.bgm" and its settings, found next to the
	// system.def, as given, or under data/ or sound/.
	MusicTrack music(const std::string &screen) const;

private:
	std::string path_;
	int32_t localWidth_ = 320, localHeight_ = 240;
	ScreenSpace space_;
	std::vector<IniSection> sections_;
	AnimationTable anims_;
	Sff sff_;
	std::unique_ptr<SpriteRenderer> renderer_;
	SoundBank snd_;
	FontTable fonts_;
	FontFiles fontFiles_;
};

} // namespace m360
