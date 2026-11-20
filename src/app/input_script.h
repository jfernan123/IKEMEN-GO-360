#pragma once

// A test run's controller, played from a script file (--input, --input2).

#include <cstdint>
#include <string>
#include <vector>

#include "core/util/file.h"
#include "core/util/text.h"
#include "platform/platform.h"

namespace m360 {

// Test runs (PC): "--input script" plays player 1's controller from a file instead of the keyboard ("--input2 script"
// player 2's, else player 2 stands still), "--shot N path"
// saves frame N as a .bmp, "--quit N" stops after frame N, "--quickstart" skips the intros so the fight begins at
// frame 0, "--cpu" makes player 2 the computer, "--trace" logs every state change and hit, "--show" keeps the window on
// screen during a test run so it can be watched. A script line is "frames keys...": hold those keys for that
// many frames, keys being U D L R (directions) x y z a b c s (buttons), m (Back, Ikemen's menu key), or "-" for
// nothing; '#' starts a comment.
class InputScript {
public:
	bool load(const std::string &path)
	{
		std::vector<uint8_t> bytes;
		if (!readFile(path, bytes))
			return false;
		for (const std::string &raw : text::splitLines(std::string(bytes.begin(), bytes.end()))) {
			const std::string line(text::stripComment(std::string_view(raw).substr(0, raw.find('#'))));
			if (line.empty())
				continue;
			const size_t space = line.find(' ');
			const int frames = text::atoi(line.substr(0, space));
			uint16_t held = 0;
			for (char k : space == std::string::npos ? std::string() : line.substr(space)) {
				switch (k) {
				case 'U': held |= Up; break;
				case 'D': held |= Down; break;
				case 'L': held |= Left; break;
				case 'R': held |= Right; break;
				case 'x': held |= X; break;
				case 'y': held |= Y; break;
				case 'z': held |= Z; break;
				case 'a': held |= A; break;
				case 'b': held |= B; break;
				case 'c': held |= C; break;
				case 's': held |= Start; break;
				case 'm': held |= Back; break;
				case 'i': held |= Lb; break;
				case 't': held |= Menu; break;
				default: break;
				}
			}
			for (int i = 0; i < frames; i++)
				frames_.push_back(held);
		}
		return true;
	}
	// The pad for a frame; nothing held once the script has run out.
	platform::Pad at(uint32_t frame) const
	{
		const uint16_t held = frame < frames_.size() ? frames_[frame] : 0;
		platform::Pad pad;
		pad.connected = true;
		pad.up = (held & Up) != 0;
		pad.down = (held & Down) != 0;
		pad.left = (held & Left) != 0;
		pad.right = (held & Right) != 0;
		pad.x = (held & X) != 0;
		pad.y = (held & Y) != 0;
		pad.rb = (held & Z) != 0;
		pad.a = (held & A) != 0;
		pad.b = (held & B) != 0;
		pad.rt = (held & C) != 0;
		pad.start = (held & Start) != 0;
		pad.back = (held & Back) != 0;
		pad.lb = (held & Lb) != 0;
		pad.menu = (held & Menu) != 0;
		return pad;
	}

private:
	enum : uint16_t {
		Up = 1 << 0, Down = 1 << 1, Left = 1 << 2, Right = 1 << 3, X = 1 << 4, Y = 1 << 5,
		Z = 1 << 6, A = 1 << 7, B = 1 << 8, C = 1 << 9, Start = 1 << 10, Back = 1 << 11,
		// i: LB (the info and hitbox view), t: the debug screen switch (Tab).
		Lb = 1 << 12, Menu = 1 << 13
	};
	// A compact mask avoids GCC combining the Pad's individual bytes with instructions Xenia does not implement.
	std::vector<uint16_t> frames_;
};

} // namespace m360
