#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace m360 {

// Elecbyte SND archive and its embedded PCM WAV files. All binary fields pass through ByteReader for the 360.
// The file stays in memory as it is and each sound is decoded the first time it's asked for: decoded to 48 kHz
// float stereo, a screenpack's fight.snd (6 MB) would take tens of MB, most of it sounds a match never plays.
class SoundBank {
public:
	struct Sound { std::vector<float> stereo48k; };
	bool load(const std::string &path, std::string &error);
	const Sound *get(int32_t group, int32_t number) const;
	void play(int32_t group, int32_t number, int channel = -1) const;
	// The options' sound effect volume as a gain, 1 at Ikemen's defaults.
	static void setVolume(float gain);
	static bool decodeWave(const uint8_t *data, size_t size, Sound &out);
	static void mix(const Sound &sound, size_t &position, float *stereo, size_t frames, float volume = 1);
	static int16_t floatToS16(float sample);
	static constexpr int SAMPLE_RATE = 48000;

private:
	// Where a sound's WAV is in the file, and once asked for, the sound decoded (empty if it didn't decode).
	struct Entry {
		size_t offset = 0, length = 0;
		bool decoded = false;
		Sound sound;
	};
	std::vector<uint8_t> file_;
	mutable std::map<std::pair<int32_t, int32_t>, Entry> sounds_;
};

} // namespace m360
