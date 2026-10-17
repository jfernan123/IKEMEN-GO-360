#include "core/log.h"

#include <cstdarg>
#include <cstdio>

#include "platform/platform.h"

namespace m360 {

void logf(const char *format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof line, format, args);
	va_end(args);
	platform::logLine(line);
}

} // namespace m360
