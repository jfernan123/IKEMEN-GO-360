// The PC platform: SDL3's renderer in a 1280x720 window, keyboard and gamepads, logging to the console and m360.log.

#include <SDL3/SDL.h>

#include <cstdio>
#include <direct.h>
#include <algorithm>
#include <string>
#include <vector>

#include "platform/music_queue.h"
#include "platform/platform.h"

namespace m360::platform {

struct Texture {
	SDL_Texture *texture;
};

namespace {

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Gamepad *gamepads[2];
FILE *log_file;
Uint64 next_frame_ns;
bool unpaced = false;
// Premultiplied "over", and "subtract": see platform::Blend.
SDL_BlendMode blend_over, blend_subtract;
// Where to save this frame, if asked.
std::string screenshot_path;
SDL_AudioStream *audio_stream;
struct Voice { std::vector<float> samples; size_t position = 0; int channel = -1; };
std::vector<Voice> voices;
MusicQueue music;

void SDLCALL audio_callback(void *, SDL_AudioStream *stream, int additional_amount, int)
{
	const size_t frames = (size_t)std::max(0, additional_amount) / (sizeof(float) * 2);
	std::vector<float> mixed(frames * 2, 0);
	for (Voice &voice : voices) {
		const size_t count = std::min(frames, voice.samples.size() / 2 - voice.position);
		for (size_t i = 0; i < count * 2; i++) mixed[i] += voice.samples[voice.position * 2 + i];
		voice.position += count;
	}
	voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice &v) {
		return v.position >= v.samples.size() / 2;
	}), voices.end());
	float left = 0, right = 0;
	for (size_t i = 0; i < frames && music.pop(left, right); i++) {
		mixed[i * 2] += left;
		mixed[i * 2 + 1] += right;
	}
	for (float &sample : mixed) sample = std::clamp(sample, -1.0f, 1.0f);
	SDL_PutAudioStreamData(stream, mixed.data(), (int)(mixed.size() * sizeof(float)));
}

void set_color(uint32_t argb)
{
	SDL_SetRenderDrawColor(renderer, argb >> 16 & 0xFF, argb >> 8 & 0xFF, argb & 0xFF, argb >> 24);
}

void open_gamepads()
{
	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	for (int i = 0; i < 2; i++) {
		if (!gamepads[i] && i < count) {
			gamepads[i] = SDL_OpenGamepad(ids[i]);
			char line[256];
			std::snprintf(line, sizeof line, "platform: player %d gamepad: %s", i + 1,
				gamepads[i] ? SDL_GetGamepadName(gamepads[i]) : SDL_GetError());
			logLine(line);
		}
	}
	SDL_free(ids);
}

} // namespace

bool init(bool hidden)
{
	log_file = std::fopen("m360.log", "w");
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
		logLine(SDL_GetError());
		return false;
	}
	const SDL_WindowFlags flags = hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
	if (!SDL_CreateWindowAndRenderer("mugen360", SCREEN_WIDTH, SCREEN_HEIGHT, flags, &window, &renderer)) {
		logLine(SDL_GetError());
		return false;
	}
	SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	blend_over = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
	blend_subtract = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
		SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
	open_gamepads();
	const SDL_AudioSpec audio_spec{ SDL_AUDIO_F32, 2, 48000 };
	// A hidden test run stays silent: no sound from a window nobody can see.
	audio_stream = hidden ? nullptr
						  : SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, audio_callback, nullptr);
	if (audio_stream)
		SDL_ResumeAudioStreamDevice(audio_stream);
	else if (!hidden)
		logLine(SDL_GetError());
	next_frame_ns = SDL_GetTicksNS();
	unpaced = hidden;
	return true;
}

void shutdown()
{
	if (audio_stream)
		SDL_DestroyAudioStream(audio_stream);
	audio_stream = nullptr;
	for (SDL_Gamepad *&g : gamepads) {
		if (g)
			SDL_CloseGamepad(g);
		g = nullptr;
	}
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	if (log_file)
		std::fclose(log_file);
}

void playSound(const float *stereo, size_t frames, int channel)
{
	if (!audio_stream || !stereo || !frames)
		return;
	SDL_LockAudioStream(audio_stream);
	if (channel >= 0)
		voices.erase(std::remove_if(voices.begin(), voices.end(), [channel](const Voice &v) { return v.channel == channel; }), voices.end());
	Voice voice;
	voice.samples.assign(stereo, stereo + frames * 2);
	voice.channel = channel;
	voices.push_back(std::move(voice));
	SDL_UnlockAudioStream(audio_stream);
}

void stopSound(int channel)
{
	if (!audio_stream)
		return;
	SDL_LockAudioStream(audio_stream);
	voices.erase(std::remove_if(voices.begin(), voices.end(), [channel](const Voice &v) { return channel < 0 || v.channel == channel; }), voices.end());
	SDL_UnlockAudioStream(audio_stream);
}

size_t musicSpace()
{
	if (!audio_stream)
		return 0;
	SDL_LockAudioStream(audio_stream);
	const size_t space = music.space();
	SDL_UnlockAudioStream(audio_stream);
	return space;
}

void queueMusic(const float *stereo, size_t frames)
{
	if (!audio_stream || !stereo || !frames)
		return;
	SDL_LockAudioStream(audio_stream);
	music.push(stereo, frames);
	SDL_UnlockAudioStream(audio_stream);
}

void clearMusic()
{
	if (!audio_stream)
		return;
	SDL_LockAudioStream(audio_stream);
	music.clear();
	SDL_UnlockAudioStream(audio_stream);
}

bool beginFrame(uint32_t clear_argb)
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_EVENT_QUIT)
			return false;
		if (event.type == SDL_EVENT_GAMEPAD_ADDED)
			open_gamepads();
		if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
			return false;
	}
	set_color(clear_argb);
	SDL_SetRenderClipRect(renderer, nullptr);
	SDL_RenderClear(renderer);
	return true;
}

bool requestScreenshot(const char *path)
{
	screenshot_path = path;
	return true;
}

void endFrame()
{
	if (!screenshot_path.empty()) {
		// The render target as drawn, before it's shown.
		if (SDL_Surface *s = SDL_RenderReadPixels(renderer, nullptr)) {
			if (!SDL_SaveBMP(s, screenshot_path.c_str()))
				logLine(SDL_GetError());
			SDL_DestroySurface(s);
		} else {
			logLine(SDL_GetError());
		}
		screenshot_path.clear();
	}
	SDL_RenderPresent(renderer);
	// A hidden test run goes as fast as it can: nobody is watching, and the game counts frames, not time.
	if (unpaced)
		return;
	// 60 frames a second, as on the 360, whatever the monitor's rate.
	const Uint64 frame_ns = 1000000000ull / 60;
	next_frame_ns += frame_ns;
	const Uint64 now = SDL_GetTicksNS();
	if (now < next_frame_ns)
		SDL_DelayNS(next_frame_ns - now);
	else if (now - next_frame_ns > 100000000ull)
		next_frame_ns = now;
}

Texture *createTexture(int width, int height, const uint32_t *argb)
{
	SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
	if (!t) {
		logLine(SDL_GetError());
		return nullptr;
	}
	SDL_UpdateTexture(t, nullptr, argb, width * 4);
	SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
	if (!SDL_SetTextureBlendMode(t, blend_over))
		logLine(SDL_GetError());
	return new Texture{ t };
}

void destroyTexture(Texture *texture)
{
	if (!texture)
		return;
	SDL_DestroyTexture(texture->texture);
	delete texture;
}

void drawQuad(Texture *texture, const Vertex corners[4], Blend blend)
{
	if (!texture)
		return;
	SDL_SetTextureBlendMode(texture->texture, blend == Blend::Subtract ? blend_subtract : blend_over);
	SDL_Vertex v[4];
	for (int i = 0; i < 4; i++)
		v[i] = SDL_Vertex{ { corners[i].x, corners[i].y }, { 1, 1, 1, 1 }, { corners[i].u, corners[i].v } };
	// Top left, top right, bottom left; bottom left, top right, bottom right.
	static const int indices[6] = { 0, 1, 2, 2, 1, 3 };
	SDL_RenderGeometry(renderer, texture->texture, v, 4, indices, 6);
}

void drawTexture(Texture *texture, float x, float y, float width, float height, Blend blend)
{
	// A negative size runs the quad back from (x, y), which mirrors the texture across it.
	const Vertex corners[4] = {
		{ x, y, 0, 0 },
		{ x + width, y, 1, 0 },
		{ x, y + height, 0, 1 },
		{ x + width, y + height, 1, 1 },
	};
	drawQuad(texture, corners, blend);
}

void setClip(int x, int y, int width, int height)
{
	const SDL_Rect r = { x, y, width > 0 ? width : 0, height > 0 ? height : 0 };
	SDL_SetRenderClipRect(renderer, &r);
}

void clearClip()
{
	SDL_SetRenderClipRect(renderer, nullptr);
}

void fillRect(float x, float y, float width, float height, uint32_t argb)
{
	set_color(argb);
	const SDL_FRect r = { x, y, width, height };
	SDL_RenderFillRect(renderer, &r);
}

Pad pad(int player)
{
	Pad p;
	if (player == 0) {
		const bool *k = SDL_GetKeyboardState(nullptr);
		p.connected = true;
		p.up = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W];
		p.down = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
		p.left = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A];
		p.right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
		p.a = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_J];
		p.b = k[SDL_SCANCODE_X] || k[SDL_SCANCODE_K];
		p.x = k[SDL_SCANCODE_C] || k[SDL_SCANCODE_U];
		p.y = k[SDL_SCANCODE_V] || k[SDL_SCANCODE_I];
		p.lb = k[SDL_SCANCODE_Q];
		// Fighting layout: punches U I O (x y z), kicks J K L (a b c) - O is RB and L is RT, MUGEN's z and c.
		p.rb = k[SDL_SCANCODE_E] || k[SDL_SCANCODE_O];
		p.rt = k[SDL_SCANCODE_L];
		p.lt = k[SDL_SCANCODE_R];
		p.start = k[SDL_SCANCODE_RETURN];
		p.back = k[SDL_SCANCODE_BACKSPACE];
		p.menu = k[SDL_SCANCODE_TAB];
	}
	if (player >= 0 && player < 2 && gamepads[player]) {
		SDL_Gamepad *g = gamepads[player];
		const Sint16 lx = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
		const Sint16 ly = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
		const Sint16 dead = 12000;
		p.connected = true;
		p.up |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -dead;
		p.down |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > dead;
		p.left |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -dead;
		p.right |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > dead;
		p.a |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH);
		p.b |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST);
		p.x |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_WEST);
		p.y |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_NORTH);
		p.lb |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
		p.rb |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
		p.lt |= SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 10000;
		p.rt |= SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 10000;
		p.start |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START);
		p.back |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK);
		p.menu |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START) &&
			SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
	}
	// Logs each change in the directions, to catch a drifting stick or a stuck key.
	static int last[2] = { -1, -1 };
	const int dirs = (int)p.up | (int)p.down << 1 | (int)p.left << 2 | (int)p.right << 3;
	if (player >= 0 && player < 2 && dirs != last[player]) {
		char line[128];
		std::snprintf(line, sizeof line, "platform: player %d directions%s%s%s%s", player + 1, p.up ? " up" : "",
			p.down ? " down" : "", p.left ? " left" : "", p.right ? " right" : "");
		if (last[player] != -1 || dirs)
			logLine(line);
		last[player] = dirs;
	}
	return p;
}

bool readWholeFile(const char *path, std::vector<uint8_t> &out)
{
	std::FILE *f = std::fopen(path, "rb");
	if (!f)
		return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	out.resize(size > 0 ? (size_t)size : 0);
	const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
	std::fclose(f);
	out.resize(got);
	return size >= 0 && got == (size_t)size;
}

bool writeWholeFile(const char *path, const std::string &data)
{
	// The folder first, as Ikemen makes save/ when it's missing.
	const std::string folder = std::string(path).substr(0, std::string(path).find_last_of('/') + 1);
	if (!folder.empty())
		_mkdir(folder.c_str());
	std::FILE *f = std::fopen(path, "wb");
	if (!f)
		return false;
	const size_t written = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), f);
	return std::fclose(f) == 0 && written == data.size();
}

void logLine(const char *line)
{
	std::printf("%s\n", line);
	std::fflush(stdout);
	if (log_file) {
		std::fprintf(log_file, "%s\n", line);
		std::fflush(log_file);
	}
}

} // namespace m360::platform
