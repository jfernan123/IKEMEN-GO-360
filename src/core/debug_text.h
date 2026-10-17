#pragma once

#include <cstdint>

namespace m360 {

// Draws text in a 5x7 pixel font with platform::fillRect, scale screen pixels per font pixel; for debug screens.
// Covers A-Z (lowercase draws as uppercase), 0-9 and ! ? . , ' - : / ( ) + = # _ %; anything else is a space.
void drawText(const char *text, float x, float y, float scale, uint32_t argb);
// printf-style.
void drawTextf(float x, float y, float scale, uint32_t argb, const char *format, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 5, 6)))
#endif
	;

} // namespace m360
