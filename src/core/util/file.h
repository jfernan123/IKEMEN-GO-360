#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace m360 {

// Reads a whole file. Paths use '/' and are relative to the game's folder: the working directory on PC, game:\ on the
// 360 (xsdk maps them).
// Small files are kept in memory once read, so reading one again costs no disk access.
bool readFile(const std::string &path, std::vector<uint8_t> &out);
// Whether a file is there, without reading it.
bool fileExists(const std::string &path);
// Drops a file from the cache, after the game writes it.
void forgetCachedFile(const std::string &path);

// A MUGEN game's folder (13a, --game): relative paths are looked up there first, then in mugen360's own folder, so
// a game uses its own files and anything it lacks (fonts the options screen needs, common1.cns) comes from ours.
// Empty: mugen360's folder only.
void setGameRoot(const std::string &folder);
const std::string &gameRoot();

// Ikemen's SearchFile: "file" tried against each of "dirs" (a folder ending in '/', or a file whose folder is used,
// "" for the game's root), directly and under each of "defaultDirs" ("chars/", "stages/"), then as given. Returns the
// first that exists, else the file unchanged. A ';' comment and '\' are cleaned first.
std::string searchFile(const std::string &file, const std::vector<std::string> &dirs,
	const std::vector<std::string> &defaultDirs = {});

// A file a .def names, as Ikemen's LoadFile with {def, "", "data/"} finds it: next to the .def (in folder "dir"),
// else from the game's root, else under data/. Not found: next to the .def, for the error message.
std::string findBesideDef(const std::string &dir, const std::string &name);

// Parts of a file without reading all of it: a select screen takes a few portraits out of each character's sprite
// file, which can be tens of MB. Found like readFile (the game's root first). Not cached.
class FileRanges {
public:
	FileRanges() = default;
	FileRanges(const FileRanges &) = delete;
	FileRanges &operator=(const FileRanges &) = delete;
	~FileRanges();
	bool open(const std::string &path);
	uint64_t size() const { return size_; }
	// size bytes from offset; false if the file is shorter.
	bool read(uint64_t offset, size_t size, std::vector<uint8_t> &out);

private:
	void *file_ = nullptr;
	uint64_t size_ = 0;
};

// The folder part of a path, with its trailing '/': "data/chars/kfm/kfm.def" gives "data/chars/kfm/".
std::string directoryOf(const std::string &path);

} // namespace m360
