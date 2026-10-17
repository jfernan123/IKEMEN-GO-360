#include "core/file.h"

#include <cstdio>

namespace m360 {

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.clear();
	uint8_t chunk[64 * 1024];
	size_t n;
	while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0)
		out.insert(out.end(), chunk, chunk + n);
	const bool failed = std::ferror(f) != 0;
	std::fclose(f);
	return !failed;
}

std::string directoryOf(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

} // namespace m360
