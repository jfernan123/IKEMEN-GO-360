// The Xbox 360 platform, on xsdk: the GPU draws colour rectangles and textured sprites, alpha blended, into a 1280x720
// frame; frames go out through xsdk_present on a console, or through VdSwap alone in Xenia, which has no display
// controller (see pong/main.c, where this was worked out).

#include <cstdarg>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <vector>

#include "platform/platform.h"

extern "C" {
#include "gpu_private.h"
}
#include "texture_shaders.h"
#include "triangle_shaders.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "xsdk/shader.h"
#include "xsdk/texture.h"
#include "xsdk/thread.h"

namespace m360::platform {

struct Texture {
	XsdkTexture texture;
};

namespace {

constexpr uint32_t BUFFERS = 3;
constexpr uint32_t BEGIN_TIMEOUT_MS = 1000;
constexpr uint32_t QUAD_VERTICES = 6;
// Colour vertices: position xyzw, colour rgba. Texture vertices: position xyzw, texture coordinates uv.
constexpr uint32_t COLOR_FLOATS = 8;
constexpr uint32_t TEXTURE_FLOATS = 6;
// Each frame's vertices, written in order as the frame draws.
constexpr uint32_t ARENA_BYTES = 4 * 1024 * 1024;
constexpr uint32_t TEXTURE_SLOT = 0;

const XsdkVertexElement color_layout[] = {
	{ XSDK_USAGE_POSITION, 0, XSDK_VERTEX_FLOAT4, 0, 0, {} },
	{ XSDK_USAGE_COLOR, 0, XSDK_VERTEX_FLOAT4, 0, 16, {} },
};
const XsdkVertexElement texture_layout[] = {
	{ XSDK_USAGE_POSITION, 0, XSDK_VERTEX_FLOAT4, 0, 0, {} },
	{ XSDK_USAGE_TEXCOORD, 0, XSDK_VERTEX_FLOAT2, 0, 16, {} },
};

XsdkShader color_vs, color_ps, texture_vs, texture_ps;
XsdkSamplerState point_sampler;
// Premultiplied "over", and "subtract": see platform::Blend.
XsdkBlendState blend_over, blend_subtract;
volatile uint32_t *arenas[BUFFERS];
volatile uint32_t *fronts[BUFFERS];
bool swap_only;
uint32_t frame_width = SCREEN_WIDTH, frame_height = SCREEN_HEIGHT;
uint32_t frame;
uint32_t frame_address;
uint32_t clear_color;
timespec next_frame;

constexpr size_t AUDIO_CHANNELS = 6, AUDIO_SAMPLES = 256;
float *audio_frame;
uint32_t audio_driver;
std::atomic_flag audio_lock = ATOMIC_FLAG_INIT;
struct Voice { std::vector<float> samples; size_t position = 0; int channel = -1; };
std::vector<Voice> voices;

void audio_callback(const uint32_t *)
{
	const uint32_t driver = __atomic_load_n(&audio_driver, __ATOMIC_ACQUIRE);
	if (!driver || !audio_frame)
		return;
	while (audio_lock.test_and_set(std::memory_order_acquire)) {}
	std::fill(audio_frame, audio_frame + AUDIO_CHANNELS * AUDIO_SAMPLES, 0.0f);
	for (Voice &voice : voices) {
		const size_t count = std::min(AUDIO_SAMPLES, voice.samples.size() / 2 - voice.position);
		for (size_t i = 0; i < count; i++) {
			audio_frame[i] += voice.samples[(voice.position + i) * 2];
			audio_frame[AUDIO_SAMPLES + i] += voice.samples[(voice.position + i) * 2 + 1];
		}
		voice.position += count;
	}
	voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice &v) {
		return v.position >= v.samples.size() / 2;
	}), voices.end());
	for (size_t i = 0; i < AUDIO_SAMPLES * 2; i++) audio_frame[i] = std::clamp(audio_frame[i], -1.0f, 1.0f);
	audio_lock.clear(std::memory_order_release);
	(void)XAudioSubmitRenderDriverFrame(driver, audio_frame);
}

// A frame is recorded on the CPU, then at endFrame its vertices go to GPU memory at once and its draws are queued.
// That keeps every vertex write ahead of the cache invalidate and every draw behind it.
struct Clip {
	int x, y, width, height; // width < 0: the whole screen
};
struct Draw {
	Texture *texture; // null for a run of colour rectangles
	Blend blend;
	uint32_t first_float;
	uint32_t vertices;
	Clip clip;
};
std::vector<float> frame_floats;
std::vector<Draw> frame_draws;
// The clip rectangle draws are recorded with (setClip).
Clip current_clip = { 0, 0, -1, -1 };

bool same_clip(const Clip &a, const Clip &b)
{
	return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

float ndc_x(float x)
{
	return (x - 0.5f) / (SCREEN_WIDTH / 2.0f) - 1.0f;
}

float ndc_y(float y)
{
	return 1.0f - (y - 0.5f) / (SCREEN_HEIGHT / 2.0f);
}

// Records a quad's floats; colour quads in a row join one draw.
void record(Texture *texture, Blend blend, const float *floats, uint32_t count)
{
	if ((frame_floats.size() + count) * 4 > ARENA_BYTES)
		return;
	if (texture || frame_draws.empty() || frame_draws.back().texture || !same_clip(frame_draws.back().clip, current_clip))
		frame_draws.push_back(Draw{ texture, blend, (uint32_t)frame_floats.size(), 0, current_clip });
	frame_draws.back().vertices += QUAD_VERTICES;
	frame_floats.insert(frame_floats.end(), floats, floats + count);
}

// Copies the frame's floats into its arena with 32-bit volatile stores, as write-combined memory needs.
void upload(volatile uint32_t *arena)
{
	uint32_t word;
	for (size_t i = 0; i < frame_floats.size(); i++) {
		std::memcpy(&word, &frame_floats[i], 4);
		arena[i] = word;
	}
	__asm__ volatile("sync" ::: "memory");
	xsdk_gpu_invalidate(xsdk_gpu_address(arena), (uint32_t)frame_floats.size() * 4);
}

void pace()
{
	const long frame_ns = 1000000000L / 60;
	next_frame.tv_nsec += frame_ns;
	if (next_frame.tv_nsec >= 1000000000L) {
		next_frame.tv_nsec -= 1000000000L;
		next_frame.tv_sec++;
	}
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	const int64_t ahead_ns =
		(int64_t)(next_frame.tv_sec - now.tv_sec) * 1000000000LL + (next_frame.tv_nsec - now.tv_nsec);
	if (ahead_ns < -100000000LL)
		next_frame = now;
	else if (ahead_ns > 1000000)
		(void)xsdk_thread_sleep((uint32_t)(ahead_ns / 1000000));
}

} // namespace

bool init(bool)
{
	if (!NT_SUCCESS(xsdk_gpu_open())) {
		logLine("platform: GPU open failed");
		return false;
	}
	audio_frame = (float *)MmAllocatePhysicalMemoryEx(0, AUDIO_CHANNELS * AUDIO_SAMPLES * sizeof(float), PAGE_READWRITE,
		0, UINT32_MAX, 0);
	if (audio_frame) {
		const XAUDIO_RENDER_DRIVER_CLIENT client = { audio_callback, 0 };
		uint32_t driver = 0;
		if (XAudioRegisterRenderDriverClient(&client, &driver) == ERROR_SUCCESS)
			__atomic_store_n(&audio_driver, driver, __ATOMIC_RELEASE);
		else
			logLine("platform: audio registration failed");
	} else {
		logLine("platform: audio allocation failed");
	}
	color_vs = color_ps = texture_vs = texture_ps = XsdkShader{};
	const uint32_t color_stride = COLOR_FLOATS * 4, texture_stride = TEXTURE_FLOATS * 4;
	if (!NT_SUCCESS(xsdk_shader_load(&color_vs, triangle_vertex_shader, sizeof triangle_vertex_shader)) ||
		!NT_SUCCESS(xsdk_shader_load(&color_ps, triangle_pixel_shader, sizeof triangle_pixel_shader)) ||
		!NT_SUCCESS(xsdk_shader_set_vertex_layout(&color_vs, color_layout, 2, &color_stride)) ||
		!NT_SUCCESS(xsdk_shader_load(&texture_vs, texture_vertex_shader, sizeof texture_vertex_shader)) ||
		!NT_SUCCESS(xsdk_shader_load(&texture_ps, texture_pixel_shader, sizeof texture_pixel_shader)) ||
		!NT_SUCCESS(xsdk_shader_set_vertex_layout(&texture_vs, texture_layout, 2, &texture_stride))) {
		logLine("platform: shader load failed");
		return false;
	}
	XsdkSampler sampler{};
	sampler.mag_filter = sampler.min_filter = sampler.mip_filter = XSDK_FILTER_POINT;
	sampler.address_u = sampler.address_v = XSDK_ADDRESS_CLAMP;
	xsdk_texture_sampler_state(&point_sampler, &sampler);
	const XsdkBlend over = { XSDK_BLEND_ONE, XSDK_BLEND_ONE_MINUS_SRC_ALPHA, XSDK_BLEND_OP_ADD, XSDK_BLEND_ONE,
		XSDK_BLEND_ONE_MINUS_SRC_ALPHA, XSDK_BLEND_OP_ADD, XSDK_WRITE_ALL };
	xsdk_gpu_blend_state(&blend_over, &over);
	// Reverse subtract: destination minus source.
	const XsdkBlend subtract = { XSDK_BLEND_ONE, XSDK_BLEND_ONE, XSDK_BLEND_OP_REVERSE_SUBTRACT, XSDK_BLEND_ZERO,
		XSDK_BLEND_ONE, XSDK_BLEND_OP_ADD, XSDK_WRITE_ALL };
	xsdk_gpu_blend_state(&blend_subtract, &subtract);
	for (uint32_t i = 0; i < BUFFERS; i++) {
		arenas[i] = xsdk_gpu_alloc(ARENA_BYTES);
		if (!arenas[i]) {
			logLine("platform: out of GPU memory");
			return false;
		}
	}

	VIDEO_MODE video;
	VdQueryVideoMode(&video);
	xsdk_log_write("platform: video mode %ux%u interlaced %u hi-def %u", (unsigned)video.display_width,
		(unsigned)video.display_height, (unsigned)video.is_interlaced, (unsigned)video.is_hi_def);
	uint32_t width = 0, height = 0;
	const NTSTATUS present = xsdk_present_open(XSDK_DISPLAY_720P, BUFFERS, 1, &width, &height);
	swap_only = !NT_SUCCESS(present);
	if (swap_only) {
		xsdk_log_write("platform: present open 0x%08X: no display controller (Xenia?), frames through VdSwap alone",
			(unsigned)present);
		for (uint32_t i = 0; i < BUFFERS; i++) {
			fronts[i] = xsdk_gpu_alloc(xsdk_gpu_resolve_bytes(SCREEN_WIDTH, SCREEN_HEIGHT));
			if (!fronts[i]) {
				logLine("platform: out of GPU memory");
				return false;
			}
		}
	} else {
		frame_width = width;
		frame_height = height;
		xsdk_log_write("platform: display %ux%u", (unsigned)width, (unsigned)height);
	}
	clock_gettime(CLOCK_MONOTONIC, &next_frame);
	return true;
}

void shutdown()
{
	const uint32_t driver = __atomic_exchange_n(&audio_driver, 0, __ATOMIC_ACQ_REL);
	if (driver)
		(void)XAudioUnregisterRenderDriverClient(driver);
	if (audio_frame)
		MmFreePhysicalMemory(0, audio_frame);
	audio_frame = nullptr;
	if (swap_only) {
		for (volatile uint32_t *f : fronts)
			xsdk_gpu_free(f);
	} else {
		xsdk_present_close();
	}
	for (volatile uint32_t *a : arenas)
		xsdk_gpu_free(a);
	xsdk_shader_free(&texture_ps);
	xsdk_shader_free(&texture_vs);
	xsdk_shader_free(&color_ps);
	xsdk_shader_free(&color_vs);
	(void)xsdk_gpu_close();
}

void playSound(const float *stereo, size_t frames, int channel)
{
	if (!audio_driver || !stereo || !frames)
		return;
	while (audio_lock.test_and_set(std::memory_order_acquire)) {}
	if (channel >= 0)
		voices.erase(std::remove_if(voices.begin(), voices.end(), [channel](const Voice &v) { return v.channel == channel; }), voices.end());
	Voice voice;
	voice.samples.assign(stereo, stereo + frames * 2);
	voice.channel = channel;
	voices.push_back(std::move(voice));
	audio_lock.clear(std::memory_order_release);
}

void stopSound(int channel)
{
	while (audio_lock.test_and_set(std::memory_order_acquire)) {}
	voices.erase(std::remove_if(voices.begin(), voices.end(), [channel](const Voice &v) { return channel < 0 || v.channel == channel; }), voices.end());
	audio_lock.clear(std::memory_order_release);
}

bool beginFrame(uint32_t clear_argb)
{
	if (swap_only) {
		frame_address = xsdk_gpu_address(fronts[frame % BUFFERS]);
	} else if (!NT_SUCCESS(xsdk_present_begin(&frame_address, BEGIN_TIMEOUT_MS))) {
		xsdk_log_write("platform: present begin timed out at frame %u", (unsigned)frame);
		return false;
	}
	frame_floats.clear();
	frame_draws.clear();
	current_clip = Clip{ 0, 0, -1, -1 };
	clear_color = clear_argb;
	// eDRAM still holds whatever the last resolve cleared it to; a full-screen rectangle sets this frame's colour.
	fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, clear_argb | 0xFF000000);
	return true;
}

void endFrame()
{
	// The GPU is done with the frame that last used this arena: its front buffer came free in beginFrame.
	volatile uint32_t *const arena = arenas[frame % BUFFERS];
	upload(arena);
	const uint32_t arena_address = xsdk_gpu_address(arena);
	xsdk_gpu_set_target(frame_width, frame_height);
	Blend current = Blend::Over;
	xsdk_gpu_set_blend(&blend_over);
	Clip scissor = { 0, 0, -1, -1 };
	for (const Draw &d : frame_draws) {
		const uint32_t floats = d.texture ? TEXTURE_FLOATS : COLOR_FLOATS;
		if (!same_clip(d.clip, scissor)) {
			scissor = d.clip;
			// Screen pixels to the target's, which may be larger than 1280x720 on a console.
			if (scissor.width < 0) {
				xsdk_gpu_set_scissor(0, 0, frame_width, frame_height);
			} else {
				const uint32_t x0 = (uint32_t)std::clamp(scissor.x, 0, SCREEN_WIDTH) * frame_width / SCREEN_WIDTH;
				const uint32_t y0 = (uint32_t)std::clamp(scissor.y, 0, SCREEN_HEIGHT) * frame_height / SCREEN_HEIGHT;
				const uint32_t x1 = (uint32_t)std::clamp(scissor.x + scissor.width, 0, SCREEN_WIDTH) * frame_width / SCREEN_WIDTH;
				const uint32_t y1 = (uint32_t)std::clamp(scissor.y + scissor.height, 0, SCREEN_HEIGHT) * frame_height / SCREEN_HEIGHT;
				xsdk_gpu_set_scissor(x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0);
			}
		}
		if (d.blend != current) {
			current = d.blend;
			xsdk_gpu_set_blend(current == Blend::Subtract ? &blend_subtract : &blend_over);
		}
		if (d.texture) {
			xsdk_gpu_set_shaders(&texture_vs, &texture_ps);
			xsdk_gpu_set_texture(TEXTURE_SLOT, &d.texture->texture, &point_sampler);
		} else {
			xsdk_gpu_set_shaders(&color_vs, &color_ps);
		}
		xsdk_gpu_set_vertices(0, arena_address + d.first_float * 4, d.vertices * floats * 4);
		xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, d.vertices);
	}
	xsdk_gpu_resolve(frame_address, frame_width, frame_height, clear_color);
	if (!swap_only) {
		xsdk_present_end();
	} else {
		GpuSurface system;
		(void)xsdk_gpu_swap(fronts[frame % BUFFERS], SCREEN_WIDTH, SCREEN_HEIGHT, &system);
		if (!NT_SUCCESS(xsdk_gpu_wait(xsdk_gpu_fence(), BEGIN_TIMEOUT_MS)))
			xsdk_log_write("platform: frame %u didn't finish", (unsigned)frame);
		pace();
	}
	frame++;
}

Texture *createTexture(int width, int height, const uint32_t *argb)
{
	Texture *t = new Texture{};
	if (!NT_SUCCESS(xsdk_texture_create(&t->texture, XSDK_FORMAT_A8R8G8B8, (uint32_t)width, (uint32_t)height, 1, false))) {
		xsdk_log_write("platform: texture %dx%d failed", width, height);
		delete t;
		return nullptr;
	}
	xsdk_texture_upload(&t->texture, 0, argb, (uint32_t)width * 4);
	xsdk_gpu_invalidate(t->texture.address, t->texture.size_bytes);
	return t;
}

void destroyTexture(Texture *texture)
{
	if (!texture)
		return;
	xsdk_texture_free(&texture->texture);
	delete texture;
}

void drawQuad(Texture *texture, const Vertex c[4], Blend blend)
{
	if (!texture)
		return;
	// Top left, top right, bottom left; bottom left, top right, bottom right.
	const float quad[QUAD_VERTICES * TEXTURE_FLOATS] = {
		ndc_x(c[0].x), ndc_y(c[0].y), 0, 1, c[0].u, c[0].v,
		ndc_x(c[1].x), ndc_y(c[1].y), 0, 1, c[1].u, c[1].v,
		ndc_x(c[2].x), ndc_y(c[2].y), 0, 1, c[2].u, c[2].v,
		ndc_x(c[2].x), ndc_y(c[2].y), 0, 1, c[2].u, c[2].v,
		ndc_x(c[1].x), ndc_y(c[1].y), 0, 1, c[1].u, c[1].v,
		ndc_x(c[3].x), ndc_y(c[3].y), 0, 1, c[3].u, c[3].v,
	};
	record(texture, blend, quad, sizeof quad / 4);
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
	current_clip = Clip{ x, y, width > 0 ? width : 0, height > 0 ? height : 0 };
}

void clearClip()
{
	current_clip = Clip{ 0, 0, -1, -1 };
}

void fillRect(float x, float y, float width, float height, uint32_t argb)
{
	// Premultiplied, for the "over" blend.
	const float ca = (argb >> 24) / 255.0f, cr = (argb >> 16 & 0xFF) / 255.0f * ca, cg = (argb >> 8 & 0xFF) / 255.0f * ca,
				cb = (argb & 0xFF) / 255.0f * ca;
	const float l = ndc_x(x), r = ndc_x(x + width), t = ndc_y(y), b = ndc_y(y + height);
	const float quad[QUAD_VERTICES * COLOR_FLOATS] = {
		l, t, 0, 1, cr, cg, cb, ca,
		r, t, 0, 1, cr, cg, cb, ca,
		l, b, 0, 1, cr, cg, cb, ca,
		l, b, 0, 1, cr, cg, cb, ca,
		r, t, 0, 1, cr, cg, cb, ca,
		r, b, 0, 1, cr, cg, cb, ca,
	};
	record(nullptr, Blend::Over, quad, sizeof quad / 4);
}

Pad pad(int player)
{
	Pad p;
	INPUT_STATE state;
	if (XamInputGetState((uint32_t)player, 0, &state) != ERROR_SUCCESS)
		return p;
	const INPUT_GAMEPAD &g = state.gamepad;
	const int dead = 12000;
	p.connected = true;
	p.up = (g.buttons & INPUT_GAMEPAD_DPAD_UP) || g.thumb_ly > dead;
	p.down = (g.buttons & INPUT_GAMEPAD_DPAD_DOWN) || g.thumb_ly < -dead;
	p.left = (g.buttons & INPUT_GAMEPAD_DPAD_LEFT) || g.thumb_lx < -dead;
	p.right = (g.buttons & INPUT_GAMEPAD_DPAD_RIGHT) || g.thumb_lx > dead;
	p.a = g.buttons & INPUT_GAMEPAD_A;
	p.b = g.buttons & INPUT_GAMEPAD_B;
	p.x = g.buttons & INPUT_GAMEPAD_X;
	p.y = g.buttons & INPUT_GAMEPAD_Y;
	p.lb = g.buttons & INPUT_GAMEPAD_LEFT_SHOULDER;
	p.rb = g.buttons & INPUT_GAMEPAD_RIGHT_SHOULDER;
	p.lt = g.left_trigger > 85;
	p.rt = g.right_trigger > 85;
	p.start = g.buttons & INPUT_GAMEPAD_START;
	p.back = g.buttons & INPUT_GAMEPAD_BACK;
	p.menu = p.start && p.lb;
	return p;
}

bool requestScreenshot(const char *)
{
	return false;
}

void logLine(const char *line)
{
	xsdk_log_write("%s", line);
}

} // namespace m360::platform
