#pragma once

#include <cstddef>
#include <vector>

namespace m360::platform {

// The music both platforms play: a ring of 48 kHz stereo frames that the engine tops up every frame
// (platform::queueMusic) and the audio callback mixes in under the sounds. Plain C++: the caller holds the platform's
// audio lock around every call.
class MusicQueue {
public:
	// One second: enough to ride out a slow frame, small enough that a new song starts at once (clear drops the rest).
	static constexpr size_t CAPACITY = 48000;

	size_t space() const { return CAPACITY - count_; }
	void push(const float *stereo, size_t frames)
	{
		if (ring_.empty())
			ring_.resize(CAPACITY * 2);
		for (size_t i = 0; i < frames && count_ < CAPACITY; i++, count_++) {
			const size_t at = (start_ + count_) % CAPACITY * 2;
			ring_[at] = stereo[i * 2];
			ring_[at + 1] = stereo[i * 2 + 1];
		}
	}
	// The next frame, if there is one.
	bool pop(float &left, float &right)
	{
		if (count_ == 0)
			return false;
		left = ring_[start_ * 2];
		right = ring_[start_ * 2 + 1];
		start_ = (start_ + 1) % CAPACITY;
		count_--;
		return true;
	}
	void clear() { start_ = count_ = 0; }

private:
	std::vector<float> ring_;
	size_t start_ = 0, count_ = 0;
};

} // namespace m360::platform
