#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace m360 {

// The buttons and directions a MUGEN character reads. Punches are x, y, z (light to strong), kicks a, b, c; s is
// start. d, w and m are Ikemen's extra buttons, kept so commands that name them parse.
struct InputState {
	bool up = false, down = false, left = false, right = false;
	bool a = false, b = false, c = false, x = false, y = false, z = false, s = false, d = false, w = false, m = false;
};

// Directions and buttons as a command names them; B and F are relative to where the character faces.
enum class Key : uint8_t { U, D, B, F, L, R, UB, UF, DB, DF, UL, UR, DL, DR, N, a, b, c, x, y, z, s, d, w, m, Count };

// How long each key has been held (+n frames) or released (-n frames), this frame and the one before.
// Translated from Ikemen-GO src/input.go (InputBuffer).
class InputBuffer {
public:
	// One frame of input; facing turns left / right into back / forward.
	void update(const InputState &in, int facing);
	void reset();

	// What a command key reads: for a press, 1 on the frame it went down (then 2, 3...) and <= 0 while it isn't;
	// for a release ("~"), 1 on the frame it came up. "$" accepts the direction with others held too, "*" likewise.
	int32_t state(Key key, bool tilde, bool dollar, bool star) const;
	// How long a key was held, for a charge ("~30$B" needs 30 frames of back before the release).
	int32_t charge(Key key, bool tilde, bool dollar, bool star) const;

private:
	// Indexed by Key, up to N (directions) then the buttons: cur this frame, prev the frame before.
	int32_t cur_[(int)Key::Count] = {};
	int32_t prev_[(int)Key::Count] = {};
};

// One key of a command step: "~30$B" is B, released, $, charged 30 frames.
struct CommandStepKey {
	Key key = Key::N;
	bool slash = false, tilde = false, dollar = false, star = false;
	int32_t chargetime = 0;
	bool operator==(const CommandStepKey &o) const
	{
		return key == o.key && slash == o.slash && tilde == o.tilde && dollar == o.dollar && star == o.star &&
			chargetime == o.chargetime;
	}
};

// One comma-separated step: its keys pressed together ("+"), or any of them ("|"); ">" when nothing else may come
// between it and the step before.
struct CommandStep {
	std::vector<CommandStepKey> keys;
	bool greater = false;
	bool orLogic = false;
	bool isSingleDirection() const;
	bool isDirToButton(const CommandStep &next) const;
};

// One [Command] of a CMD file. Translated from Ikemen-GO src/input.go (Command, ReadCommandSymbols,
// AutoGreaterExpand, Step, GreaterCheckFail).
class Command {
public:
	// Parses "~D, DF, F, x"; problems go to error, the command still built from what parsed.
	bool parse(const std::string &text, std::string &error);
	void step(const InputBuffer &buffer);
	void clear(bool resetBuffer);

	std::string name;
	int32_t maxtime = 15;
	int32_t maxbuftime = 1;
	int32_t maxsteptime = 0;
	bool shared = true;
	// Frames the command still counts as done: the "command" trigger is true while this is above 0.
	int32_t curbuftime = 0;
	// Set by step() on the frame the last step completes.
	bool completeframe = false;

private:
	bool greaterCheckFail(size_t i, const InputBuffer &buffer) const;
	void autoGreaterExpand();

	std::vector<CommandStep> steps_;
	std::vector<bool> completed_;
	std::vector<int32_t> stepTimers_;
	std::vector<size_t> loopOrder_;
	int32_t curtime_ = 0;
};

// A character's commands, with its input buffer. Loads the [Defaults] and [Command] sections of a CMD file.
class CommandList {
public:
	bool load(const std::string &cmdPath, std::string &error);
	// One frame: the buffer takes the input, then every command steps.
	void update(const InputState &in, int facing);
	// Forgets everything held and every command in progress (a new round; Ikemen's BufReset).
	void reset();
	// The "command" trigger: whether a command of this name has completed within its buffer time.
	bool active(const std::string &name) const;
	// Names of the commands active now, for debugging.
	std::vector<std::string> activeNames() const;
	const InputBuffer &buffer() const { return buffer_; }

private:
	InputBuffer buffer_;
	std::vector<Command> commands_;
	std::map<std::string, std::vector<size_t>> byName_;
};

} // namespace m360
