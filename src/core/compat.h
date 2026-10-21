#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace m360 {

struct GameConfig;

// Milestone 13b: what a game uses that mugen360 doesn't have yet. The loaders note each missing feature (an unknown
// controller or trigger, an expression that doesn't parse, a file format, a stage or background key) against the
// item loading at the time; --check loads a whole game and writes the notes as a report, most-used first.
namespace compat {

enum class Kind : uint8_t {
	Controller, // a controller type mugen360 doesn't know
	NotDone,    // a known controller (or AssertSpecial flag) that runs as a no-op so far
	Trigger,    // a trigger name the expression parser doesn't know
	Expression, // an expression that doesn't parse for another reason
	File,       // a file or format that can't be read
	Stage,      // a stage key or section not read yet
	Background, // a background element type, trans or BGCtrl type not supported
	Select,     // a select.def entry not supported
	KIND_COUNT
};

const char *kindName(Kind kind);

// The item loading now ("char data/chars/kfm/kfm.def"); notes go against it. Empty: notes are dropped (normal play
// doesn't collect).
void setItem(const std::string &item);
void note(Kind kind, const std::string &feature);
// Packed sprite bytes the item loaded (its SFF files): the report's size column.
void addSpriteBytes(size_t bytes);

struct Feature {
	int uses = 0;
	std::vector<std::string> items;
};
struct Item {
	std::string name;
	bool loaded = true;
	std::string error;
	size_t spriteBytes = 0;
	int states = 0, controllers = 0;
	std::vector<std::string> missing; // "kind name", each once
};

// Starts collecting (clears earlier notes).
void begin();
void end();
// Notes by "kind\tname", and the items in load order.
const std::map<std::string, Feature> &features();
std::vector<Item> &items();

// Loads every character, stage, the motif and fight.def of a game (its config, after loadGame) and returns the
// report text.
std::string checkGame(const GameConfig &config);

} // namespace compat
} // namespace m360
