#include "core/file.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "core/text.h"
#include "platform/platform.h"

namespace m360 {

namespace {

// Small files read once stay in memory: a character's .cns, .cmd and .air are read by several loaders, and again for
// a second copy of the same character. The 360's drives pay a seek for every read.
constexpr size_t CACHE_FILE_LIMIT = 256 * 1024, CACHE_TOTAL_LIMIT = 8 * 1024 * 1024;
std::map<std::string, std::vector<uint8_t>> cache;
size_t cachedBytes = 0;
std::string root;

bool isAbsolute(const std::string &path)
{
	return (!path.empty() && (path[0] == '/' || path[0] == '\\')) || path.find(':') != std::string::npos;
}

bool rawExists(const std::string &path)
{
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::fclose(f);
	return true;
}

} // namespace

void setGameRoot(const std::string &folder)
{
	root = folder;
	std::replace(root.begin(), root.end(), '\\', '/');
	if (!root.empty() && root.back() != '/')
		root += '/';
	cache.clear();
	cachedBytes = 0;
}

const std::string &gameRoot()
{
	return root;
}

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
	const auto found = cache.find(path);
	if (found != cache.end()) {
		out = found->second;
		return true;
	}
	const bool inRoot = !root.empty() && !isAbsolute(path) && platform::readWholeFile((root + path).c_str(), out);
	if (!inRoot && !platform::readWholeFile(path.c_str(), out))
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
	if (!root.empty() && !isAbsolute(path) && rawExists(root + path))
		return true;
	return rawExists(path);
}

// Ikemen's SearchFile (common.go), without its zip archives.
std::string searchFile(const std::string &fileIn, const std::vector<std::string> &dirs,
	const std::vector<std::string> &defaultDirs)
{
	std::string file = fileIn;
	const size_t comment = file.find(';');
	if (comment != std::string::npos)
		file.resize(comment);
	file = std::string(text::trim(file));
	std::replace(file.begin(), file.end(), '\\', '/');
	if (file.empty())
		return file;
	const auto lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	};
	std::vector<std::string> defaults;
	for (std::string d : defaultDirs) {
		if (d.compare(0, 2, "./") == 0)
			d = d.substr(2);
		if (d.empty())
			continue;
		if (d.back() != '/')
			d += '/';
		if (std::find(defaults.begin(), defaults.end(), d) == defaults.end())
			defaults.push_back(d);
	}
	// The paths tried so far, each once. A list, not a std::set: GCC builds std::set<std::string>'s insert with an
	// instruction Xenia skips (see xenia_scan).
	std::vector<std::string> seen;
	const auto tryPath = [&](const std::string &candidate) {
		const std::string key = lower(candidate);
		if (std::find(seen.begin(), seen.end(), key) != seen.end())
			return false;
		seen.push_back(key);
		return fileExists(candidate);
	};
	const auto join = [](const std::string &base, const std::string &name) {
		return base.empty() ? name : base + "/" + name;
	};
	if (isAbsolute(file) && tryPath(file))
		return file;
	// A root ending in '/' is that folder; otherwise its folder, and itself too when it has no extension.
	const std::vector<std::string> roots = dirs.empty() ? std::vector<std::string>{ "" } : dirs;
	for (std::string r : roots) {
		std::replace(r.begin(), r.end(), '\\', '/');
		std::vector<std::string> bases;
		if (r.empty()) {
			bases.push_back("");
		} else if (r.back() == '/') {
			bases.push_back(r.substr(0, r.size() - 1));
		} else {
			const size_t slash = r.find_last_of('/');
			bases.push_back(slash == std::string::npos ? std::string() : r.substr(0, slash));
			const size_t dot = r.find_last_of('.');
			if ((dot == std::string::npos || (slash != std::string::npos && dot < slash)) && r != bases[0])
				bases.push_back(r);
		}
		for (const std::string &base : bases) {
			if (tryPath(join(base, file)))
				return join(base, file);
			for (const std::string &d : defaults) {
				if (lower(file).compare(0, d.size(), lower(d)) == 0)
					continue;
				if (tryPath(join(base, d + file)))
					return join(base, d + file);
			}
		}
	}
	return file;
}

FileRanges::~FileRanges()
{
	if (file_)
		std::fclose(static_cast<std::FILE *>(file_));
}

bool FileRanges::open(const std::string &path)
{
	std::FILE *f = nullptr;
	if (!root.empty() && !isAbsolute(path))
		f = std::fopen((root + path).c_str(), "rb");
	if (!f)
		f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	file_ = f;
	std::fseek(f, 0, SEEK_END);
	size_ = (uint64_t)std::ftell(f);
	return true;
}

bool FileRanges::read(uint64_t offset, size_t size, std::vector<uint8_t> &out)
{
	out.resize(size);
	if (!file_ || offset + size > size_)
		return false;
	std::FILE *f = static_cast<std::FILE *>(file_);
	if (std::fseek(f, (long)offset, SEEK_SET) != 0)
		return false;
	return size == 0 || std::fread(out.data(), 1, size, f) == size;
}

std::string findBesideDef(const std::string &dir, const std::string &name)
{
	const std::string found = searchFile(name, { dir, "", "data/" });
	return fileExists(found) ? found : dir + name;
}

std::string directoryOf(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

} // namespace m360
