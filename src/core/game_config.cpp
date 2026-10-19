#include "core/game_config.h"

#include <algorithm>
#include <vector>

#include "core/file.h"
#include "core/ini.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

namespace {

std::string lower(std::string s)
{
	return text::toLower(s);
}

bool endsWithDef(const std::string &s)
{
	const std::string l = lower(s);
	return l.size() > 4 && l.compare(l.size() - 4, 4, ".def") == 0;
}

} // namespace

// Ikemen's Select.AddChar: "kfm" is kfm/kfm.def, "kfm/alt" is kfm/alt.def, found under the game, chars/, data/ or
// data/chars/. Empty when it isn't there.
std::string GameConfig::findCharacter(const std::string &entry)
{
	std::string guess = entry;
	std::replace(guess.begin(), guess.end(), '\\', '/');
	if (!endsWithDef(guess))
		guess = guess.find('/') == std::string::npos ? guess + "/" + guess + ".def" : guess + ".def";
	const std::string found = searchFile(guess, { "", "data/" }, { "chars/" });
	return endsWithDef(found) && fileExists(found) ? found : std::string();
}

// Ikemen's Select.AddStage: the .def under the game, stages/, data/ or data/stages/. Empty when it isn't there.
std::string GameConfig::findStage(const std::string &entry)
{
	std::string def = entry;
	std::replace(def.begin(), def.end(), '\\', '/');
	if (!endsWithDef(def))
		def += ".def";
	const std::string found = searchFile(def, { "", "data/" }, { "stages/" });
	return fileExists(found) ? found : std::string();
}

// select.def's [Characters], [ExtraStages] and [Options] as main.lua reads them: a character line is its folder or
// .def, then stage .defs and param = value pairs (includestage, order, music...); "randomselect" is the random cell;
// slot = { a | b } lists several characters for one cell (each is added here); exclude = 1 characters go last. Stages
// are [ExtraStages] first, then each character's stages unless includestage = 0 or -1.
void GameConfig::readSelect(const std::string &text, bool extraStagesSection)
{
	std::string section;
	std::vector<std::string> excluded;
	std::vector<std::string> characterStages;
	const auto addCharacter = [&](const std::string &line) {
		const std::vector<std::string> parts = text::split(line, ',');
		std::string name(text::trim(parts[0]));
		if (name.empty())
			return;
		const std::string l = lower(name);
		if (l == "randomselect") {
			characters.push_back(l);
			return;
		}
		if (l == "dummyslot" || l == "skipslot" || l == "blank" || l == "emptyslot") {
			logf("game: select.def's %s cells aren't supported yet; left out", l.c_str());
			return;
		}
		const std::string def = findCharacter(name);
		if (def.empty()) {
			logf("game: can't find character %s; left out", name.c_str());
			return;
		}
		characters.push_back(def);
		int32_t include = 1;
		std::vector<std::string> stagesHere;
		for (size_t i = 1; i < parts.size(); i++) {
			const std::string param(text::trim(parts[i]));
			if (endsWithDef(param)) {
				stagesHere.push_back(param);
				continue;
			}
			const size_t eq = param.find('=');
			if (eq != std::string::npos && lower(std::string(text::trim(param.substr(0, eq)))) == "includestage")
				include = std::atoi(std::string(text::trim(param.substr(eq + 1))).c_str());
		}
		if (include == 1)
			characterStages.insert(characterStages.end(), stagesHere.begin(), stagesHere.end());
	};
	const auto addStage = [&](const std::string &entry) {
		const std::string def = findStage(entry);
		if (def.empty()) {
			logf("game: can't find stage %s; left out", entry.c_str());
			return;
		}
		for (const std::string &s : stages) {
			if (lower(s) == lower(def))
				return;
		}
		stages.push_back(def);
	};
	bool slot = false;
	for (const std::string &raw : text::splitLines(text)) {
		std::string line(text::trim(text::stripComment(raw)));
		if (line.empty())
			continue;
		std::replace(line.begin(), line.end(), '\\', '/');
		if (line.front() == '[') {
			section = lower(line);
			continue;
		}
		if (section == "[characters]") {
			// slot = { a | b }: one cell with several characters.
			std::string l = lower(line);
			if (!slot && text::startsWith(l, "slot") && line.find('{') != std::string::npos) {
				slot = true;
				line = line.substr(line.find('{') + 1);
			}
			bool slotEnd = false;
			if (slot && line.find('}') != std::string::npos) {
				line = line.substr(0, line.find('}'));
				slotEnd = true;
			}
			l = lower(line);
			const size_t exclude = l.find("exclude");
			if (exclude != std::string::npos && l.find('=', exclude) != std::string::npos &&
				std::atoi(l.substr(l.find('=', exclude) + 1).c_str()) == 1) {
				excluded.push_back(line);
			} else if (slot) {
				for (const std::string &entry : text::split(line, '|'))
					addCharacter(std::string(text::trim(entry)));
			} else {
				addCharacter(line);
			}
			if (slotEnd)
				slot = false;
		} else if (section == (extraStagesSection ? "[extrastages]" : "[stages]")) {
			// "unlock = ..." and anything after it is Ikemen's; the stage is the first field.
			addStage(std::string(text::trim(text::split(line, ',')[0])));
		} else if (section == "[options]") {
			const size_t eq = line.find('=');
			if (eq != std::string::npos) {
				const std::string key = lower(std::string(text::trim(line.substr(0, eq))));
				if (key == "arcade.maxmatches")
					arcadeMaxMatches = std::string(text::trim(line.substr(eq + 1)));
			}
		}
	}
	for (const std::string &line : excluded)
		addCharacter(line);
	for (const std::string &def : characterStages)
		addStage(def);
}

bool GameConfig::load(const std::string &path, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes))
		return true;
	const std::string content(bytes.begin(), bytes.end());
	const std::vector<IniSection> ini = parseIni(content);
	const IniSection *gameSection = findSection(ini, "game");
	if (!gameSection) {
		error = path + " has no [Game] section";
		return false;
	}
	auto readPath = [&](const char *key, std::string &out) {
		const std::string value(text::trim(gameSection->text(key)));
		if (!value.empty())
			out = value;
		std::replace(out.begin(), out.end(), '\\', '/');
	};
	// [Characters] and [Stages]: one entry a line, as select.def's [Characters] and [ExtraStages].
	readSelect(content, false);
	readPath("p1", player1);
	readPath("p2", player2);
	readPath("stage", stage);
	readPath("fight", fight);
	readPath("motif", motif);
	readPath("game", game);
	if (player1.empty() || player2.empty() || stage.empty()) {
		error = path + " has an empty p1, p2, or stage path";
		return false;
	}
	return true;
}

// A MUGEN game's folder, as MUGEN / Ikemen start it: the motif from data/mugen.cfg's [Options] motif (Ikemen:
// save/config.ini's [Config] Motif), else data/system.def; its [Files] select and fight looked up from the motif's
// folder, the game and data/ (motif.go's lookup "def,,data/"); then select.def's roster and stages.
bool GameConfig::loadGame(const std::string &folder, std::string &error)
{
	setGameRoot(folder);
	game = gameRoot().substr(0, gameRoot().size() - 1);
	std::string motifPath;
	std::vector<uint8_t> bytes;
	if (readFile("data/mugen.cfg", bytes)) {
		const std::vector<IniSection> cfg = parseIni(std::string(bytes.begin(), bytes.end()));
		if (const IniSection *o = findSection(cfg, "options"))
			motifPath = std::string(text::trim(o->text("motif")));
	}
	if (motifPath.empty() && readFile("save/config.ini", bytes)) {
		const std::vector<IniSection> cfg = parseIni(std::string(bytes.begin(), bytes.end()));
		if (const IniSection *c = findSection(cfg, "config"))
			motifPath = std::string(text::trim(c->text("motif")));
	}
	if (motifPath.empty())
		motifPath = "data/system.def";
	motif = searchFile(motifPath, { "", "data/" });
	if (!readFile(motif, bytes)) {
		error = game + ": can't find its system.def (" + motifPath + ")";
		return false;
	}
	const std::vector<IniSection> system = parseIni(std::string(bytes.begin(), bytes.end()));
	const IniSection *files = findSection(system, "files");
	const std::string selectName = files && files->has("select") ? files->text("select") : "select.def";
	const std::string fightName = files && files->has("fight") ? files->text("fight") : "fight.def";
	const std::string selectPath = searchFile(selectName, { motif, "", "data/" });
	fight = searchFile(fightName, { motif, "", "data/" });
	if (!readFile(selectPath, bytes)) {
		error = game + ": can't find its select.def (" + selectName + ")";
		return false;
	}
	characters.clear();
	stages.clear();
	readSelect(std::string(bytes.begin(), bytes.end()), true);
	// Test runs and a roster-less start fight the first two characters on the first stage.
	std::vector<std::string> playable;
	for (const std::string &c : characters) {
		if (c != "randomselect")
			playable.push_back(c);
	}
	if (playable.empty() || stages.empty()) {
		error = game + ": select.def has no characters or no stages mugen360 can find";
		return false;
	}
	player1 = playable[0];
	player2 = playable.size() > 1 ? playable[1] : playable[0];
	stage = stages[0];
	logf("game: %s: motif %s, select %s, fight %s, %d characters, %d stages", game.c_str(), motif.c_str(),
		selectPath.c_str(), fight.c_str(), (int)playable.size(), (int)stages.size());
	return true;
}

} // namespace m360
