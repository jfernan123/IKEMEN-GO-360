#include "core/compat.h"

#include <algorithm>
#include <cstdio>

#include "core/character.h"
#include "core/fight_screen.h"
#include "core/game_config.h"
#include "core/log.h"
#include "core/motif.h"
#include "core/stage.h"

namespace m360 {
namespace compat {

namespace {

bool collecting = false;
std::string current;
std::map<std::string, Feature> featureMap;
std::vector<Item> itemList;

Item *currentItem()
{
	for (Item &item : itemList) {
		if (item.name == current)
			return &item;
	}
	return nullptr;
}

const char *const KIND_NAMES[(int)Kind::KIND_COUNT] = { "controller", "not done", "trigger", "expression", "file",
	"stage key", "background", "select.def" };

std::string kb(size_t bytes)
{
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
	return buffer;
}

} // namespace

const char *kindName(Kind kind)
{
	return (int)kind < (int)Kind::KIND_COUNT ? KIND_NAMES[(int)kind] : "?";
}

void begin()
{
	collecting = true;
	current.clear();
	featureMap.clear();
	itemList.clear();
}

void end()
{
	collecting = false;
	current.clear();
}

void setItem(const std::string &item)
{
	if (!collecting)
		return;
	current = item;
	if (!item.empty() && !currentItem()) {
		Item fresh;
		fresh.name = item;
		itemList.push_back(fresh);
	}
}

void note(Kind kind, const std::string &feature)
{
	if (!collecting || current.empty())
		return;
	const std::string key = std::string(kindName(kind)) + "\t" + feature;
	Feature &f = featureMap[key];
	f.uses++;
	if (std::find(f.items.begin(), f.items.end(), current) == f.items.end())
		f.items.push_back(current);
	Item *item = currentItem();
	const std::string label = std::string(kindName(kind)) + " " + feature;
	if (item && std::find(item->missing.begin(), item->missing.end(), label) == item->missing.end())
		item->missing.push_back(label);
}

void addSpriteBytes(size_t bytes)
{
	if (Item *item = collecting ? currentItem() : nullptr)
		item->spriteBytes += bytes;
}

const std::map<std::string, Feature> &features()
{
	return featureMap;
}

std::vector<Item> &items()
{
	return itemList;
}

std::string checkGame(const GameConfig &config)
{
	begin();
	// The screenpack: the title / select / VS / options screens and the fight screen.
	{
		setItem("motif " + config.motif);
		Motif motif;
		std::string error;
		if (!motif.load(config.motif, error)) {
			currentItem()->loaded = false;
			currentItem()->error = error;
		}
	}
	{
		setItem("fight " + config.fight);
		FightScreen fight;
		std::string error;
		if (!fight.load(config.fight, error)) {
			currentItem()->loaded = false;
			currentItem()->error = error;
		}
	}
	for (const std::string &def : config.characters) {
		if (def == "randomselect")
			continue;
		setItem("char " + def);
		Item &item = *currentItem();
		Character character;
		std::string error;
		if (!character.load(def, 1, error)) {
			item.loaded = false;
			item.error = error;
			continue;
		}
		const StateTable::Stats &stats = character.states().stats();
		item.states = (int)character.states().all().size();
		item.controllers = stats.controllers;
	}
	for (const std::string &def : config.stages) {
		setItem("stage " + def);
		Stage stage;
		std::string error;
		if (!stage.load(def, error)) {
			currentItem()->loaded = false;
			currentItem()->error = error;
		}
	}
	end();

	// The report: totals, the missing features most-used first, then each item.
	int chars = 0, charsLoaded = 0, stages = 0, stagesLoaded = 0;
	size_t charBytes = 0;
	for (const Item &item : itemList) {
		const bool isChar = item.name.compare(0, 5, "char ") == 0, isStage = item.name.compare(0, 6, "stage ") == 0;
		chars += isChar;
		charsLoaded += isChar && item.loaded;
		stages += isStage;
		stagesLoaded += isStage && item.loaded;
		if (isChar)
			charBytes += item.spriteBytes;
	}
	std::string out = "mugen360 compatibility report: " + (config.game.empty() ? std::string("(mugen360's own content)") : config.game) + "\n";
	out += "Characters: " + std::to_string(charsLoaded) + " of " + std::to_string(chars) + " load, " + kb(charBytes) +
		" of packed sprites in all\n";
	out += "Stages: " + std::to_string(stagesLoaded) + " of " + std::to_string(stages) + " load\n";
	for (const Item &item : itemList) {
		if (item.name.compare(0, 6, "motif ") == 0 || item.name.compare(0, 6, "fight ") == 0)
			out += (item.name.compare(0, 6, "motif ") == 0 ? "Screenpack: " : "Fight screen: ") +
				std::string(item.loaded ? "loads" : "fails: " + item.error) + "\n";
	}

	std::vector<std::pair<std::string, const Feature *>> ranked;
	for (const auto &[key, feature] : featureMap)
		ranked.emplace_back(key, &feature);
	std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
		if (a.second->items.size() != b.second->items.size())
			return a.second->items.size() > b.second->items.size();
		return a.second->uses > b.second->uses;
	});
	out += "\nMissing features, by how many items use them (" + std::to_string(ranked.size()) + "):\n";
	for (const auto &[key, feature] : ranked) {
		const size_t tab = key.find('\t');
		char line[256];
		std::snprintf(line, sizeof line, "  %-11s %-40s %4d uses in %3d items: ", key.substr(0, tab).c_str(),
			key.substr(tab + 1).c_str(), feature->uses, (int)feature->items.size());
		out += line;
		for (size_t i = 0; i < feature->items.size() && i < 4; i++)
			out += (i ? ", " : "") + feature->items[i];
		if (feature->items.size() > 4)
			out += ", ...";
		out += "\n";
	}

	out += "\nItems:\n";
	for (const Item &item : itemList) {
		out += "  " + item.name + ": " + (item.loaded ? "loads" : "FAILS: " + item.error);
		if (item.states > 0)
			out += ", " + std::to_string(item.states) + " states, " + std::to_string(item.controllers) + " controllers";
		if (item.spriteBytes > 0)
			out += ", " + kb(item.spriteBytes) + " sprites";
		out += item.missing.empty() ? "\n" : ", " + std::to_string(item.missing.size()) + " missing\n";
		for (const std::string &m : item.missing)
			out += "      " + m + "\n";
	}
	logf("check: %d of %d characters, %d of %d stages load; %d missing features", charsLoaded, chars, stagesLoaded,
		stages, (int)ranked.size());
	return out;
}

} // namespace compat
} // namespace m360
