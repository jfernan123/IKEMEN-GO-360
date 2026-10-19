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

// The folder part of a path, with its trailing '/': "data/chars/kfm/kfm.def" gives "data/chars/kfm/".
std::string directoryOf(const std::string &path);

} // namespace m360
