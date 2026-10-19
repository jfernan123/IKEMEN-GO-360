#include "core/file.h"

#include <cstdio>
#include <map>

#include "platform/platform.h"

namespace m360 {

namespace {

// Small files read once stay in memory: a character's .cns, .cmd and .air are read by several loaders, and again for
// a second copy of the same character. The 360's drives pay a seek for every read.
constexpr size_t CACHE_FILE_LIMIT = 256 * 1024, CACHE_TOTAL_LIMIT = 8 * 1024 * 1024;
std::map<std::string, std::vector<uint8_t>> cache;
size_t cachedBytes = 0;

} // namespace

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
	const auto found = cache.find(path);
	if (found != cache.end()) {
		out = found->second;
		return true;
	}
	if (!platform::readWholeFile(path.c_str(), out))
		return false;
	if (out.size() <= CACHE_FILE_LIMIT && cachedBytes + out.size() <= CACHE_TOTAL_LIMIT) {
		cache[path] = out;
		cachedBytes += out.size();
	}
	return true;
}

void forgetCachedFile(const std::string &path)
{
	const auto found = cache.find(path);
	if (found == cache.end())
		return;
	cachedBytes -= found->second.size();
	cache.erase(found);
}

bool fileExists(const std::string &path)
{
	if (cache.count(path))
		return true;
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::fclose(f);
	return true;
}

std::string directoryOf(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

} // namespace m360
