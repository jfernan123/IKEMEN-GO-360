// Milestone 8f: music. The decoders on two synthetic one-second tones (440 Hz, left at half volume, right at a
// quarter: tests/fixtures/tone.mp3 and tone.ogg), a WAV made here, the 48 kHz resampling, loop points, Ikemen's music
// volume, finding the files, and ikemen1's [Music].

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/file.h"
#include "core/motif.h"
#include "core/music.h"

using namespace m360;

namespace {
int passed, failed;

void expect(bool value, const char *name)
{
	if (value)
		passed++;
	else {
		failed++;
		std::printf("FAIL  8f: %s\n", name);
	}
}

void put16(std::vector<uint8_t> &b, uint16_t v)
{
	b.push_back((uint8_t)v);
	b.push_back((uint8_t)(v >> 8));
}

void put32(std::vector<uint8_t> &b, uint32_t v)
{
	put16(b, (uint16_t)v);
	put16(b, (uint16_t)(v >> 16));
}

// A mono 16-bit WAV whose sample i is i * 100.
std::vector<uint8_t> rampWave(uint32_t rate, uint32_t frames)
{
	std::vector<uint8_t> b = { 'R', 'I', 'F', 'F' };
	put32(b, 36 + frames * 2);
	b.insert(b.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
	put32(b, 16);
	put16(b, 1);
	put16(b, 1);
	put32(b, rate);
	put32(b, rate * 2);
	put16(b, 2);
	put16(b, 16);
	b.insert(b.end(), { 'd', 'a', 't', 'a' });
	put32(b, frames * 2);
	for (uint32_t i = 0; i < frames; i++)
		put16(b, (uint16_t)(i * 100));
	return b;
}

float ramp(uint32_t i)
{
	return (float)(i * 100) / 32768.0f;
}

// Decodes a whole tone and measures each side's loudness (root mean square; a sine of amplitude a gives a / sqrt 2).
void checkTone(const char *path, const char *name)
{
	std::vector<uint8_t> bytes;
	expect(readFile(path, bytes), name);
	std::unique_ptr<MusicSource> source = openMusic(path, bytes);
	expect(source && source->rate == 44100 && source->channels == 2, name);
	if (!source)
		return;
	std::vector<float> all, chunk(1024 * 2);
	size_t got = 0;
	while ((got = source->read(chunk.data(), 1024)) > 0)
		all.insert(all.end(), chunk.begin(), chunk.begin() + (ptrdiff_t)(got * 2));
	const size_t frames = all.size() / 2;
	// MP3 adds a little padding at the ends.
	expect(frames >= 44100 && frames < 44100 + 3000, name);
	double left = 0, right = 0;
	for (size_t i = 0; i < frames; i++) {
		left += all[i * 2] * all[i * 2];
		right += all[i * 2 + 1] * all[i * 2 + 1];
	}
	left = std::sqrt(left / (double)frames);
	right = std::sqrt(right / (double)frames);
	expect(std::fabs(left - 0.5 / std::sqrt(2.0)) < 0.03 && std::fabs(right - 0.25 / std::sqrt(2.0)) < 0.03, name);
	// Back to the start: the same first samples again.
	expect(source->seek(0), name);
	std::vector<float> again(16 * 2);
	source->read(again.data(), 16);
	expect(std::fabs(again[20] - all[20]) < 0.001f, name);
}
} // namespace

int run8fTests()
{
	passed = failed = 0;

	// Ikemen's default BGM volume (75) at a song volume of 100: 2 ^ -0.5.
	expect(std::fabs(Music::gain(100) - 0.7071f) < 0.001f, "volume 100 plays at 2^-0.5");
	expect(Music::gain(0) == 0, "volume 0 is silent");

	checkTone("tests/fixtures/tone.mp3", "MP3: decodes a 440 Hz stereo tone");
	checkTone("tests/fixtures/tone.ogg", "Ogg Vorbis: decodes a 440 Hz stereo tone");

	// A 24 kHz WAV plays at 48 kHz: every other output frame is a sample, the ones between halfway.
	const std::vector<uint8_t> slow = rampWave(24000, 100);
	std::unique_ptr<MusicSource> wave = openMusic("ramp.wav", slow);
	expect(wave && wave->rate == 24000 && wave->channels == 1, "WAV: a 24 kHz mono file opens");
	Music music;
	MusicTrack once;
	once.loop = false;
	music.playSource(std::move(wave), once);
	std::vector<float> out(400 * 2);
	const size_t made = music.produce(out.data(), 400);
	const float g = Music::gain(100);
	// The last output frame is halfway between the last two samples: 2 * 99 frames.
	expect(made == 198, "a 100-frame 24 kHz song is 198 frames at 48 kHz");
	expect(std::fabs(out[10 * 2] - ramp(5) * g) < 1e-5f && std::fabs(out[11 * 2] - (ramp(5) + ramp(6)) / 2 * g) < 1e-5f,
		"resampled frames: samples, and halfway between them");
	expect(out[11 * 2] == out[11 * 2 + 1], "mono plays on both sides");
	expect(music.produce(out.data(), 10) == 0, "a song without a loop ends");

	// At 48 kHz with loop points 10 and 20: frames 0-19, then 10-19 again and again.
	const std::vector<uint8_t> fast = rampWave(48000, 100);
	MusicTrack looped;
	looped.loopStart = 10;
	looped.loopEnd = 20;
	music.playSource(openMusic("ramp.wav", fast), looped);
	expect(music.produce(out.data(), 45) == 45, "a looping song keeps going");
	expect(std::fabs(out[19 * 2] - ramp(19) * g) < 1e-5f && std::fabs(out[20 * 2] - ramp(10) * g) < 1e-5f &&
			std::fabs(out[30 * 2] - ramp(10) * g) < 1e-5f,
		"after the loop end it goes back to the loop start");
	// Without a loop end it loops at the end of the file.
	MusicTrack whole;
	whole.loopStart = 50;
	music.playSource(openMusic("ramp.wav", fast), whole);
	music.produce(out.data(), 100);
	music.produce(out.data(), 2);
	expect(std::fabs(out[0] - ramp(50) * g) < 1e-5f, "the end of the file goes back to the loop start");

	expect(!openMusic("sound/kfm.mid", fast), "MIDI isn't supported");

	// Finding files as Ikemen's SearchFile: next to the def, then as given.
	MusicTrack found;
	found.path = "tone.ogg";
	found.resolve("tests/fixtures/some.def");
	expect(found.path == "tests/fixtures/tone.ogg", "a song next to its def is found");
	MusicTrack missing;
	missing.path = "sound\\Title.mp3";
	missing.resolve("data/ikemen1/system.def");
	expect(missing.path == "sound/Title.mp3", "a missing song keeps its name (with / for \\)");

	std::string error;
	Motif motif;
	expect(motif.load("data/ikemen1/system.def", error), "system.def loads");
	const MusicTrack title = motif.music("title"), vs = motif.music("vs"), option = motif.music("option");
	expect(title.path == "sound/Title.mp3" && title.loop && title.volume == 100, "ikemen1's title music loops");
	expect(vs.path == "sound/Versus.mp3" && !vs.loop, "ikemen1's VS music plays once");
	expect(option.path.empty(), "ikemen1 has no options music");

	std::printf("8f music: %d passed, %d failed\n", passed, failed);
	return failed;
}
