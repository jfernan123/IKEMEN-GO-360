#pragma once

namespace m360::coords {

// Ikemen's localscl: stage units per character-local unit. A 1280-wide character therefore uses 0.25.
inline float scaleForWidth(int width) { return width > 0 ? 320.0f / width : 1.0f; }
inline float toWorld(float value, float scale) { return value * scale; }
inline float toLocal(float value, float scale) { return value / scale; }
inline float between(float value, float fromScale, float toScale) { return value * fromScale / toScale; }

} // namespace m360::coords
