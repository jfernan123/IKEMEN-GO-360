#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace m360 {

// One "[Name]" section of a MUGEN .def file: "key = value" lines, keys lowercase, comments (';') removed.
struct IniSection {
	// The name as written between the brackets, trimmed ("BG Floor"); lower is lowercase ("bg floor").
	std::string name;
	std::string lower;
	std::map<std::string, std::string> values;
	// The keys in file order, each once: for lists whose order matters, like a menu's "menu.itemname." entries.
	std::vector<std::string> keys;

	bool has(const std::string &key) const { return values.count(key) != 0; }
	// The raw value, or "" when missing.
	std::string get(const std::string &key) const;
	// The value without surrounding double quotes: a name or a path.
	std::string text(const std::string &key) const;
	// Comma-separated numbers into out, as many as the value has, up to out's size; false when the key is missing or
	// its first number is blank. Missing values keep what out held, as Ikemen's ReadF32 / ReadI32 do.
	bool floats(const std::string &key, std::vector<float *> out) const;
	bool ints(const std::string &key, std::vector<int32_t *> out) const;
	bool readFloat(const std::string &key, float &a) const { return floats(key, { &a }); }
	bool readInt(const std::string &key, int32_t &a) const { return ints(key, { &a }); }
};

// Every section of a .def text, in order. "key = value" lines before the first section are dropped. Within a
// section, the first of duplicate keys wins.
std::vector<IniSection> parseIni(const std::string &text);
// The first section named name (case-insensitive), or null.
const IniSection *findSection(const std::vector<IniSection> &sections, const std::string &name);
// The keys of a section that aren't in known, in file order: what a loader doesn't read yet, for its log.
std::vector<std::string> unreadKeys(const IniSection &section, std::initializer_list<const char *> known);

} // namespace m360
