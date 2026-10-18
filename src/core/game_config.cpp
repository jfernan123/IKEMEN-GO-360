#include "core/game_config.h"

#include <algorithm>
#include <vector>

#include "core/file.h"
#include "core/ini.h"
#include "core/text.h"

namespace m360 {

bool GameConfig::load(const std::string &path, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes))
		return true;
	const std::vector<IniSection> ini = parseIni(std::string(bytes.begin(), bytes.end()));
	const IniSection *game = findSection(ini, "game");
	if (!game) {
		error = path + " has no [Game] section";
		return false;
	}
	auto readPath = [&](const char *key, std::string &out) {
		const std::string value(text::trim(game->text(key)));
		if (!value.empty())
			out = value;
		std::replace(out.begin(), out.end(), '\\', '/');
	};
	// [Characters] and [Stages]: one entry a line, the part before any comma (the ini parser keeps only key = value
	// lines, so these are read here).
	std::string section;
	for (const std::string &raw : text::splitLines(std::string(bytes.begin(), bytes.end()))) {
		const std::string line(text::trim(text::stripComment(raw)));
		if (line.empty())
			continue;
		if (line.front() == '[') {
			section = text::toLower(line);
			continue;
		}
		std::string entry(text::trim(text::split(line, ',')[0]));
		std::replace(entry.begin(), entry.end(), '\\', '/');
		if (entry.empty() || entry.find('=') != std::string::npos)
			continue;
		if (section == "[characters]") {
			// "kfm" is data/chars/kfm/kfm.def; "kfm/kfm720.def" is under data/chars.
			const std::string lower = text::toLower(entry);
			const bool isDef = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".def") == 0;
			if (!text::startsWith(entry, "data/"))
				entry = isDef ? "data/chars/" + entry : "data/chars/" + entry + "/" + entry + ".def";
			characters.push_back(entry);
		} else if (section == "[stages]") {
			stages.push_back(text::startsWith(entry, "data/") ? entry : "data/" + entry);
		}
	}
	readPath("p1", player1);
	readPath("p2", player2);
	readPath("stage", stage);
	readPath("fight", fight);
	if (player1.empty() || player2.empty() || stage.empty()) {
		error = path + " has an empty p1, p2, or stage path";
		return false;
	}
	return true;
}

} // namespace m360
