#pragma once

namespace m360 {

// printf-style logging: the console and m360.log on PC, game:\m360.log (or xsdk's fallbacks) on the 360.
void logf(const char *format, ...)
#if defined(__GNUC__)
	__attribute__((format(printf, 1, 2)))
#endif
	;

} // namespace m360
