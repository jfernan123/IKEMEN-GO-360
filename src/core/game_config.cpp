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
	readPath("p1", player1);
	readPath("p2", player2);
	readPath("stage", stage);
	if (player1.empty() || player2.empty() || stage.empty()) {
		error = path + " has an empty p1, p2, or stage path";
		return false;
	}
	return true;
}

} // namespace m360
