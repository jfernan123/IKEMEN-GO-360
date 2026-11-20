// The neural CPU's network (core/ai/neural_policy) against PyTorch: the exported policy must give the same scores as the
// trained one on the same inputs, over two steps so the GRU's memory is checked too.
// tests/fixtures/neural/reference.txt comes from rl/export_policy.py --reference.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/ai/neural_policy.h"

using namespace m360;

namespace {

int passed = 0, failed = 0;

void expect(bool ok, const char *what)
{
	std::printf("    %s %s\n", ok ? "ok  " : "FAIL", what);
	(ok ? passed : failed)++;
}

std::vector<float> numbers(const std::string &line)
{
	std::istringstream in(line);
	std::vector<float> out;
	float v;
	while (in >> v)
		out.push_back(v);
	return out;
}

float largestDifference(const std::vector<float> &a, const std::vector<float> &b)
{
	if (a.size() != b.size())
		return 1e9f;
	float most = 0;
	for (size_t i = 0; i < a.size(); i++)
		most = std::fmax(most, std::fabs(a[i] - b[i]));
	return most;
}

} // namespace

int runNeuralTests()
{
	passed = failed = 0;
	std::printf("Neural CPU\n");
	NeuralPolicy policy;
	std::string error;
	const bool loaded = policy.load("data/neural/cap_vs5.m3nn", error);
	expect(loaded, "the exported policy loads");
	if (!loaded) {
		std::printf("      %s\n", error.c_str());
		return failed;
	}
	expect(policy.characterId("chars/captainamerica/captainamerica.def") == 15, "Captain America's id");
	expect(policy.characterId("Chars/Thanos/Thanos.def") == 145, "ids ignore case");
	expect(policy.characterId("chars/nobody/nobody.def") == -1, "a character it never saw");

	std::ifstream file("tests/fixtures/neural/reference.txt");
	std::vector<std::string> lines;
	for (std::string line; std::getline(file, line);)
		lines.push_back(line);
	expect(lines.size() == 7, "the reference has two steps");
	if (lines.size() != 7)
		return failed;
	const std::vector<float> ids = numbers(lines[0]);
	std::vector<float> memory = policy.freshMemory();
	for (int step = 0; step < 2; step++) {
		std::vector<float> directions, buttons;
		const bool ran = policy.evaluate(numbers(lines[1 + 3 * step]), (int)ids[0], (int)ids[1], memory, directions,
			buttons);
		const float d = largestDifference(directions, numbers(lines[2 + 3 * step]));
		const float b = largestDifference(buttons, numbers(lines[3 + 3 * step]));
		std::printf("      step %d: largest difference %.2e (directions), %.2e (buttons)\n", step + 1, d, b);
		expect(ran && d < 1e-3f && b < 1e-3f, step == 0 ? "first step's scores match PyTorch"
														: "second step's scores match (the memory carried over)");
	}

	// Sampling stays inside the action space and differs with the random state.
	uint32_t random = 12345;
	int direction = -1, button = -1, different = 0, last = -1;
	memory = policy.freshMemory();
	for (int i = 0; i < 50; i++) {
		policy.decide(numbers(lines[1]), (int)ids[0], (int)ids[1], memory, random, direction, button);
		different += direction != last;
		last = direction;
	}
	expect(direction >= 0 && direction < 9 && button >= 0 && button < 8, "a decision is a direction and a button");
	expect(different > 1, "sampled decisions vary");
	std::printf("  neural: %d passed, %d failed\n", passed, failed);
	return failed;
}
