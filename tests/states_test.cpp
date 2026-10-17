// Loads Kung Fu Man's states as the game will (kfm.cns, kfm.cmd, then common1.cns) and reports what's there:
// states, controller types and how many of each, and every problem found.

#include <cstdio>
#include <string>

#include "core/states.h"

using namespace m360;

int runStatesReport()
{
	StateTable table;
	std::string error;
	int failures = 0;
	for (const char *file : { "data/chars/kfm/kfm.cns", "data/chars/kfm/kfm.cmd", "data/common1.cns" }) {
		if (!table.load(file, error)) {
			std::printf("FAIL  %s\n", error.c_str());
			failures++;
		}
	}
	const StateTable::Stats &s = table.stats();
	std::printf("\nKung Fu Man's states: %zu states, %d controllers (%d skipped), %d parameter errors\n",
		table.all().size(), s.controllers, s.skipped, s.paramErrors);
	std::printf("\nController types used:\n");
	for (const auto &[name, count] : s.byType)
		std::printf("  %-16s %d\n", name.c_str(), count);
	if (!s.problems.empty()) {
		std::printf("\nProblems:\n");
		for (const std::string &p : s.problems)
			std::printf("  %s\n", p.c_str());
	}
	// Spot checks: states the game needs.
	for (int no : { -1, 0, 20, 40, 200, 5000 }) {
		const StateDef *d = table.find(no);
		if (!d) {
			std::printf("FAIL  state %d missing\n", no);
			failures++;
		} else {
			std::printf("  state %d: type %c, %zu controllers, from %s\n", no, d->type, d->ctrls.size(), d->file.c_str());
		}
	}
	return failures + s.skipped + s.paramErrors;
}
