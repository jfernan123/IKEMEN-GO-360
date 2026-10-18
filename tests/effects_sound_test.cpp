#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "core/sound.h"
#include "core/effects.h"
#include "platform/platform.h"

using namespace m360;

namespace {
int passed, failed;
std::vector<float> played;
int playedChannel = -2;

void expect(bool value, const char *name)
{
	if (value) passed++;
	else { failed++; std::printf("FAIL  6d: %s\n", name); }
}

void put16(std::vector<uint8_t> &b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
void put32(std::vector<uint8_t> &b, uint32_t v) { put16(b, (uint16_t)v); put16(b, (uint16_t)(v >> 16)); }
void putTag(std::vector<uint8_t> &b, const char *s) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)s[i]); }

std::vector<uint8_t> wave(int channels, int rate, int bits, const std::vector<int16_t> &samples)
{
	const uint32_t dataBytes = (uint32_t)samples.size() * bits / 8;
	std::vector<uint8_t> b;
	putTag(b, "RIFF"); put32(b, 36 + dataBytes); putTag(b, "WAVE");
	putTag(b, "fmt "); put32(b, 16); put16(b, 1); put16(b, (uint16_t)channels); put32(b, (uint32_t)rate);
	put32(b, (uint32_t)(rate * channels * bits / 8)); put16(b, (uint16_t)(channels * bits / 8)); put16(b, (uint16_t)bits);
	putTag(b, "data"); put32(b, dataBytes);
	for (int16_t s : samples) {
		if (bits == 8) b.push_back((uint8_t)(s + 128));
		else put16(b, (uint16_t)s);
	}
	return b;
}
}

void m360::platform::playSound(const float *stereo, size_t frames, int channel)
{
	played.assign(stereo, stereo + frames * 2);
	playedChannel = channel;
}
void m360::platform::stopSound(int) {}
size_t m360::platform::musicSpace() { return 0; }
void m360::platform::queueMusic(const float *, size_t) {}
void m360::platform::clearMusic() {}
struct m360::platform::Texture {};
m360::platform::Texture *m360::platform::createTexture(int, int, const uint32_t *) { return new Texture; }
void m360::platform::destroyTexture(Texture *texture) { delete texture; }
void m360::platform::drawQuad(Texture *, const Vertex[4], Blend) {}
void m360::platform::drawTexture(Texture *, float, float, float, float, Blend) {}
void m360::platform::fillRect(float, float, float, float, uint32_t) {}
void m360::platform::setClip(int, int, int, int) {}
void m360::platform::clearClip() {}
bool m360::platform::readWholeFile(const char *path, std::vector<uint8_t> &out)
{
	std::FILE *f = std::fopen(path, "rb");
	if (!f)
		return false;
	out.clear();
	uint8_t chunk[65536];
	size_t n;
	while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0)
		out.insert(out.end(), chunk, chunk + n);
	std::fclose(f);
	return true;
}

int run6dTests()
{
	std::printf("\nMilestone 6d sound and mixer\n");
	SoundBank::Sound decoded;
	auto mono8 = wave(1, 48000, 8, { -128, 0, 127 });
	expect(SoundBank::decodeWave(mono8.data(), mono8.size(), decoded), "decode PCM8 mono WAV");
	expect(decoded.stereo48k.size() == 6, "mono duplicates into stereo");
	expect(decoded.stereo48k[0] == decoded.stereo48k[1] && decoded.stereo48k[2] == decoded.stereo48k[3], "mono channels equal");
	auto stereo16 = wave(2, 24000, 16, { -32768, 32767, 0, 16384 });
	expect(SoundBank::decodeWave(stereo16.data(), stereo16.size(), decoded), "decode PCM16 stereo WAV");
	expect(decoded.stereo48k.size() == 8, "resample 24 kHz to 48 kHz");
	expect(decoded.stereo48k.front() <= -.99f && decoded.stereo48k[1] >= .99f, "signed PCM endpoints");
	auto bad = mono8; bad[0] = 'X';
	expect(!SoundBank::decodeWave(bad.data(), bad.size(), decoded), "reject invalid RIFF");
	SoundBank common, kfm;
	std::string error;
	expect(common.load("data/common.snd", error), "load common.snd");
	expect(kfm.load("data/chars/kfm/kfm.snd", error), "load kfm.snd");
	expect(common.get(5, 0) != nullptr, "common hit sound F5,0 exists");
	expect(common.get(6, 0) != nullptr, "common guard sound F6,0 exists");
	expect(kfm.get(0, 0) != nullptr, "KFM voice 0,0 exists");
	played.clear(); kfm.play(0, 0, 3);
	expect(!played.empty() && playedChannel == 3, "bank sends decoded sound to platform channel");
	SoundBank::Sound mixSound; mixSound.stereo48k = { .5f, -.5f, 1, -1 };
	float mixed[6]{}; size_t position = 0;
	SoundBank::mix(mixSound, position, mixed, 3, .5f);
	expect(position == 2 && std::fabs(mixed[0] - .25f) < .0001f && std::fabs(mixed[3] + .5f) < .0001f,
		"mixer advances and applies volume");
	expect(SoundBank::floatToS16(2) == 32767 && SoundBank::floatToS16(-2) == -32767, "mixer clamps output");

	std::printf("Milestone 6d effects and pause\n");
	FightEffects effects;
	expect(effects.load("data/fightfx.sff", "data/fightfx.air", error), "load shared fightfx SFF and AIR");
	Animation shared;
	expect(effects.getSharedAnimation(100, shared), "shared super-pause F100 exists");
	expect(effects.getSharedAnimation(120, shared), "shared MakeDust F120 exists");
	FightEffects::Spawn spawn;
	spawn.animNo = 120; spawn.shared = true; spawn.owner = 1; spawn.removeTime = 2;
	expect(effects.spawnExplod(spawn) && effects.countExplods(1) == 1, "spawn and count shared Explod");
	effects.tick(); effects.tick(); effects.tick();
	expect(effects.countExplods(1) == 0, "Explod removetime expires");
	effects.startShake(4, 180, 2, 90);
	effects.tick();
	expect(std::fabs(effects.shakeY()) > 1.9f && std::fabs(effects.shakeX()) < .001f, "EnvShake phase and direction");
	for (int i = 0; i < 4; i++) effects.tick();
	expect(effects.shakeX() == 0 && effects.shakeY() == 0, "EnvShake expires");
	effects.startPause(0, 3, 1, false, false);
	expect(effects.paused() && !effects.pausedFor(0) && effects.pausedFor(1) && !effects.backgroundPaused(),
		"Pause owner movetime and pausebg");
	effects.tickPause();
	expect(effects.pausedFor(0), "Pause owner freezes after movetime");
	effects.tickPause(); effects.tickPause();
	expect(!effects.paused(), "Pause timer expires");
	effects.startPause(1, 2, 0, true, true);
	expect(effects.superPaused() && effects.backgroundPaused(), "SuperPause flags");
	std::printf("%d milestone 6d tests passed, %d failed\n", passed, failed);
	return failed;
}
