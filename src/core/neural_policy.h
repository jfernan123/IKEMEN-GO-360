#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace m360 {

// A policy trained with the RL tools (rl/train_pool.py), playing as the computer: the network of rl/model.py ("large")
// run on the CPU, from a file rl/export_policy.py writes. It reads core/rl_observation's view of the fight and
// picks a direction and a button for the next RL_FRAMES frames, sampled as in training (so it varies its play).
// Not from Ikemen: mugen360's own.
class NeuralPolicy {
public:
	bool load(const std::string &path, std::string &error);
	bool loaded() const { return hidden_ > 0; }
	// The id training gave a character (its .def path, compared without case), or -1 if it wasn't in the roster.
	int characterId(const std::string &def) const;
	// The memory a match starts with.
	std::vector<float> freshMemory() const { return std::vector<float>((size_t)hidden_, 0.0f); }
	// The network's scores for each direction and button (before the softmax); memory carries over between calls.
	// False (and nothing changed) if the observation or ids don't fit the network.
	bool evaluate(const std::vector<float> &observation, int self, int opponent, std::vector<float> &memory,
		std::vector<float> &directions, std::vector<float> &buttons) const;
	// One decision: memory carries over from the last one; random is the sampling's state (any value but 0).
	void decide(const std::vector<float> &observation, int self, int opponent, std::vector<float> &memory,
		uint32_t &random, int &direction, int &button) const;

private:
	struct Matrix {
		int32_t rows = 0, cols = 0;
		std::vector<float> values;
	};
	struct Layer {
		const Matrix *weight = nullptr, *bias = nullptr;
	};
	const Matrix *find(const std::string &name) const;
	Layer layer(const std::string &prefix) const;

	int32_t observationSize_ = 0, hidden_ = 0;
	std::vector<std::string> characters_;
	std::vector<std::pair<std::string, Matrix>> tensors_;
	Layer encoder_, encoderNorm_, blocks_[2], blockNorms_[2], directionHidden_, directionOut_, buttonHidden_,
		buttonOut_;
	const Matrix *embedding_ = nullptr, *gruIn_ = nullptr, *gruHidden_ = nullptr, *gruInBias_ = nullptr,
				 *gruHiddenBias_ = nullptr;
};

} // namespace m360
