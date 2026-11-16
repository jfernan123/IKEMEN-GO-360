#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/sff.h"
#include "core/sprite_renderer.h"
#include "platform/platform.h"

namespace m360 {

// VS NEURAL CAP's select screen: the computer is always the trained policy's character (Captain America), and the
// player picks one of the opponents it trained against. Each shows its big portrait (sprite 9000,1), its name and how
// often the policy beat it in training. Not from Ikemen: mugen360's own, kept simple on purpose.
class NeuralSelect {
public:
	struct Opponent {
		std::string def;
		// The policy's win rate against it at the end of training, 0..1.
		float trainedWins = 0;
	};
	bool load(const std::string &cpuDef, const std::vector<Opponent> &opponents, std::string &error);
	void reset();
	void update(const platform::Pad &pad);
	void draw();
	bool done() const { return done_; }
	bool cancelled() const { return cancelled_; }
	const std::string &picked() const { return entries_[(size_t)cursor_].def; }

private:
	struct Entry {
		std::string def, name;
		float trainedWins = 0;
		std::unique_ptr<Sff> sff;
		std::unique_ptr<SpriteRenderer> renderer;
	};
	bool loadEntry(const std::string &def, Entry &out);
	void drawPortrait(Entry &entry, float x, float y, float width, float height, bool mirrored);

	Entry cpu_;
	std::vector<Entry> entries_;
	int cursor_ = 0;
	bool done_ = false, cancelled_ = false;
	platform::Pad last_;
	uint32_t time_ = 0;
};

} // namespace m360
