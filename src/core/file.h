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

// The folder part of a path, with its trailing '/': "data/chars/kfm/kfm.def" gives "data/chars/kfm/".
std::string directoryOf(const std::string &path);

} // namespace m360
