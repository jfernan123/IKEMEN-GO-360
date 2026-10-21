#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/bgdef.h"
#include "core/layout.h"
#include "core/music.h"
#include "core/sff.h"
#include "core/sound.h"
#include "core/sprite_renderer.h"

namespace m360 {

// A MUGEN storyboard (a screenpack's logo and intro, a character's arcade intro and ending): scenes played one after
// another, each with a clear colour, a background, up to ten layers (an animation or sprite, or text typed out),
// sounds, music, a fade in and out and a length. Translated from Ikemen-GO src/storyboard.go (loadStoryboard, step,
// draw), without videos, 3D models or dialogue paging. Scenes carry clearcolor, clearalpha, clearlayerno and
// layerall.pos over from the one before when they don't set them.
class Storyboard {
public:
	Storyboard() = default;
	Storyboard(const Storyboard &) = delete;
	Storyboard &operator=(const Storyboard &) = delete;

	bool load(const std::string &defPath, std::string &error);
	// From [SceneDef] startscene, at its start.
	void start();
	// One tick. skip (Start) ends the scene early; cancel (an attack button, Back) ends the storyboard. Both
	// still fade out.
	void update(bool skip, bool cancel);
	void draw();
	bool done() const { return !active_; }
	// The song the scene now playing asks for, else null; and whether the music stops at the end ([SceneDef]
	// stopmusic).
	const MusicTrack *music() const;
	int32_t sceneIndex() const { return scene_; }
	bool stopMusic() const { return stopMusic_; }
	const std::string &path() const { return path_; }

private:
	struct Layer {
		AnimLayout anim;
		TextLayout text;
		bool hasText = false;
		int32_t startTime = 0, endTime = 0;
		// Ikemen's textdelay: ticks per letter typed.
		float textDelay = 2;
	};
	struct Sound {
		int32_t value[2] = { -1, 0 };
		int32_t startTime = 0;
	};
	struct Scene {
		int32_t endTime = 0;
		ScreenFade fadeIn, fadeOut;
		// clearcolor (-1: none), clearalpha's source alpha, layerall.pos.
		int32_t clearColour = -1, clearAlpha = 255;
		float layerAllX = 0, layerAllY = 0;
		std::unique_ptr<BgDef> bg;
		std::vector<Layer> layers;
		std::vector<Sound> sounds;
		MusicTrack bgm;
		bool hasBgm = false;
		int32_t jump = -1;
	};

	std::string path_;
	ScreenSpace space_;
	std::vector<IniSection> sections_;
	AnimationTable anims_;
	Sff sff_;
	std::unique_ptr<SpriteRenderer> renderer_;
	SoundBank snd_;
	bool hasSnd_ = false;
	FontTable fonts_;
	std::vector<Scene> scenes_;
	int32_t startScene_ = 0;
	bool stopMusic_ = true;

	int32_t scene_ = 0, counter_ = 0, endTimer_ = -1;
	bool active_ = false, canceled_ = false;
};

} // namespace m360
