#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace m360 {

// Elecbyte SND archive and its embedded PCM WAV files. All binary fields pass through ByteReader for the 360.
class SoundBank {
public:
	struct Sound { std::vector<float> stereo48k; };
	bool load(const std::string &path, std::string &error);
	const Sound *get(int32_t group, int32_t number) const;
	void play(int32_t group, int32_t number, int channel = -1) const;
	static bool decodeWave(const uint8_t *data, size_t size, Sound &out);
	static void mix(const Sound &sound, size_t &position, float *stereo, size_t frames, float volume = 1);
	static int16_t floatToS16(float sample);
	static constexpr int SAMPLE_RATE = 48000;

private:
	std::map<std::pair<int32_t, int32_t>, Sound> sounds_;
};

} // namespace m360
