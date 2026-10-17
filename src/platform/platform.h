#pragma once

#include <cstdint>
#include <cstddef>

// The engine's only view of the machine. src/platform/pc implements it with SDL3, src/platform/x360 with xsdk.
// The engine draws in a fixed 1280x720 screen space, top left (0,0); each platform scales that to its display.
namespace m360::platform {

constexpr int SCREEN_WIDTH = 1280;
constexpr int SCREEN_HEIGHT = 720;

struct Texture;

struct Pad {
	bool connected = false;
	bool up = false, down = false, left = false, right = false;
	bool a = false, b = false, x = false, y = false;
	bool lb = false, rb = false;
	// Triggers, pressed past a third of the way.
	bool lt = false, rt = false;
	bool start = false, back = false;
};

// How a textured draw combines with the screen. Textures hold premultiplied colour, so one blend covers MUGEN's
// normal, additive and add-alpha drawing: the texture's colour and alpha carry the source and destination factors
// (see SpriteRenderer). Subtract takes the texture's colour away from the screen.
enum class Blend : uint8_t {
	Over,     // screen = texture + screen * (1 - texture alpha)
	Subtract, // screen = screen - texture
};

// A corner of a textured quad: screen position and texture coordinate (0..1 across the texture).
struct Vertex {
	float x, y, u, v;
};

// hidden: no window on screen (PC test runs); the 360 ignores it.
bool init(bool hidden = false);
void shutdown();

// Starts a frame; false once the window is closed (PC). Clears to clear_argb.
bool beginFrame(uint32_t clear_argb);
// Shows the frame and waits for the next 1/60 s.
void endFrame();

// A texture of premultiplied 0xAARRGGBB pixels, width * height of them, row by row; sampled with point filtering.
Texture *createTexture(int width, int height, const uint32_t *argb);
void destroyTexture(Texture *texture);

// Draws a textured quad from its top left, top right, bottom left and bottom right corners. Any parallelogram draws
// exactly; other shapes split along the TR-BL diagonal.
void drawQuad(Texture *texture, const Vertex corners[4], Blend blend = Blend::Over);
// Draws the whole texture into the rectangle, in screen pixels; a negative width or height mirrors it.
void drawTexture(Texture *texture, float x, float y, float width, float height, Blend blend = Blend::Over);
// A solid rectangle of straight (not premultiplied) 0xAARRGGBB.
void fillRect(float x, float y, float width, float height, uint32_t argb);

// Player 0 or 1's controller; the keyboard plays player 0 on PC.
Pad pad(int player);

// Saves the frame being drawn to a .bmp when it's shown (endFrame); false where the platform can't (the 360).
bool requestScreenshot(const char *path);

// A copied 48 kHz stereo floating-point sound. A nonnegative MUGEN channel replaces the sound already on it.
void playSound(const float *stereo, size_t frames, int channel = -1);
void stopSound(int channel);

// Writes a line to the platform's log.
void logLine(const char *line);

} // namespace m360::platform
