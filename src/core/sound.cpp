#include "core/sound.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/bytes.h"
#include "core/file.h"
#include "platform/platform.h"

namespace m360 {

namespace {
uint32_t tag(const char *s) { return (uint8_t)s[0] | (uint32_t)(uint8_t)s[1] << 8 | (uint32_t)(uint8_t)s[2] << 16 |
	(uint32_t)(uint8_t)s[3] << 24; }
}

bool SoundBank::decodeWave(const uint8_t *data, size_t size, Sound &out)
{
	out.stereo48k.clear();
	ByteReader r(data, size);
	if (r.u32() != tag("RIFF")) return false;
	r.u32();
	if (r.u32() != tag("WAVE")) return false;
	uint16_t format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	const uint8_t *pcm = nullptr;
	size_t pcmBytes = 0;
	while (r.ok() && r.pos() + 8 <= r.size()) {
		const uint32_t kind = r.u32(), bytes = r.u32();
		const size_t start = r.pos();
		if (kind == tag("fmt ") && bytes >= 16) {
			format = r.u16(); channels = r.u16(); rate = r.u32(); r.u32(); r.u16(); bits = r.u16();
		} else if (kind == tag("data")) {
			pcm = r.at(start, bytes); pcmBytes = pcm ? bytes : 0;
		}
		r.seek(start + bytes + (bytes & 1));
	}
	if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || !rate || !pcm)
		return false;
	const size_t bytesPerFrame = channels * (bits / 8);
	const size_t sourceFrames = pcmBytes / bytesPerFrame;
	if (!sourceFrames) return false;
	// Floating-point here also avoids GCC emitting rldic for the 64-bit integer multiply on the 360; Xenia skips it.
	const size_t outputFrames = (size_t)std::ceil(sourceFrames * (double)SAMPLE_RATE / rate);
	out.stereo48k.resize(outputFrames * 2);
	auto sample = [&](size_t frame, int channel) {
		channel = std::min(channel, (int)channels - 1);
		const size_t offset = frame * bytesPerFrame + channel * bits / 8;
		return bits == 8 ? ((int)pcm[offset] - 128) / 128.0f
			: (float)(int16_t)((uint16_t)pcm[offset] | (uint16_t)pcm[offset + 1] << 8) / 32768.0f;
	};
	for (size_t i = 0; i < outputFrames; i++) {
		const double source = i * (double)rate / SAMPLE_RATE;
		const size_t a = std::min((size_t)source, sourceFrames - 1), b = std::min(a + 1, sourceFrames - 1);
		const float f = (float)(source - a);
		for (int c = 0; c < 2; c++) out.stereo48k[i * 2 + c] = sample(a, c) + (sample(b, c) - sample(a, c)) * f;
	}
	return true;
}

bool SoundBank::load(const std::string &path, std::string &error)
{
	sounds_.clear();
	if (!readFile(path, file_)) { error = "can't read " + path; return false; }
	ByteReader r(file_);
	char signature[12]{};
	if (!r.bytes(signature, 12) || std::memcmp(signature, "ElecbyteSnd\0", 12) != 0) {
		error = path + " has an invalid SND header"; return false;
	}
	r.u16(); r.u16();
	const uint32_t count = r.u32();
	uint32_t offset = r.u32();
	for (uint32_t i = 0; i < count && offset; i++) {
		r.seek(offset);
		const uint32_t next = r.u32(), length = r.u32();
		const int32_t group = (int32_t)r.u32(), number = (int32_t)r.u32();
		// The first of two sounds with the same numbers wins.
		if (r.at(r.pos(), length) && group >= 0 && number >= 0) {
			Entry entry;
			entry.offset = r.pos();
			entry.length = length;
			sounds_.emplace(std::make_pair(group, number), std::move(entry));
		}
		offset = next;
	}
	if (!r.ok()) { error = path + " is truncated"; return false; }
	return true;
}

const SoundBank::Sound *SoundBank::get(int32_t group, int32_t number) const
{
	const auto found = sounds_.find({ group, number });
	if (found == sounds_.end())
		return nullptr;
	Entry &entry = found->second;
	if (!entry.decoded) {
		entry.decoded = true;
		if (!decodeWave(file_.data() + entry.offset, entry.length, entry.sound))
			entry.sound.stereo48k.clear();
	}
	return entry.sound.stereo48k.empty() ? nullptr : &entry.sound;
}

namespace {
float soundVolume = 1;
} // namespace

void SoundBank::setVolume(float gain)
{
	soundVolume = gain;
}

void SoundBank::play(int32_t group, int32_t number, int channel) const
{
	const Sound *sound = get(group, number);
	if (!sound)
		return;
	if (soundVolume == 1) {
		platform::playSound(sound->stereo48k.data(), sound->stereo48k.size() / 2, channel);
		return;
	}
	std::vector<float> scaled(sound->stereo48k);
	for (float &sample : scaled)
		sample *= soundVolume;
	platform::playSound(scaled.data(), scaled.size() / 2, channel);
}

void SoundBank::mix(const Sound &sound, size_t &position, float *stereo, size_t frames, float volume)
{
	const size_t available = sound.stereo48k.size() / 2 - std::min(position, sound.stereo48k.size() / 2);
	const size_t count = std::min(frames, available);
	for (size_t i = 0; i < count * 2; i++) stereo[i] += sound.stereo48k[position * 2 + i] * volume;
	position += count;
}

int16_t SoundBank::floatToS16(float sample)
{
	return (int16_t)std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
}

} // namespace m360
