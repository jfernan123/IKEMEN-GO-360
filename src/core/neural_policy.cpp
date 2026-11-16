#include "core/neural_policy.h"

#include <cmath>
#include <cstring>

#include "core/bytes.h"
#include "core/file.h"
#include "core/text.h"

namespace m360 {

namespace {

float readFloat(ByteReader &r)
{
	const uint32_t bits = r.u32();
	float value;
	std::memcpy(&value, &bits, sizeof value);
	return value;
}

std::string readName(ByteReader &r)
{
	const uint16_t length = r.u16();
	std::string name(length, '\0');
	if (length > 0)
		r.bytes(name.data(), length);
	return name;
}

// out = weight * in + bias (PyTorch's Linear: weight is rows x cols, in has cols values).
void linear(const float *weight, const float *bias, int32_t rows, int32_t cols, const float *in, float *out)
{
	for (int32_t r = 0; r < rows; r++) {
		const float *w = weight + (size_t)r * (size_t)cols;
		float sum = bias ? bias[r] : 0.0f;
		for (int32_t c = 0; c < cols; c++)
			sum += w[c] * in[c];
		out[r] = sum;
	}
}

// PyTorch's LayerNorm (eps 1e-5), then ReLU.
void normRelu(std::vector<float> &x, const float *scale, const float *shift)
{
	double mean = 0, variance = 0;
	for (float v : x)
		mean += v;
	mean /= (double)x.size();
	for (float v : x)
		variance += ((double)v - mean) * ((double)v - mean);
	variance /= (double)x.size();
	const float inverse = (float)(1.0 / std::sqrt(variance + 1e-5));
	for (size_t i = 0; i < x.size(); i++) {
		const float v = ((x[i] - (float)mean) * inverse) * scale[i] + shift[i];
		x[i] = v > 0 ? v : 0;
	}
}

float sigmoid(float v)
{
	return 1.0f / (1.0f + std::exp(-v));
}

// Samples from softmax(logits), as torch's Categorical does in training.
int sample(const std::vector<float> &logits, uint32_t &random)
{
	float most = logits[0];
	for (float v : logits)
		most = v > most ? v : most;
	std::vector<float> p(logits.size());
	float total = 0;
	for (size_t i = 0; i < logits.size(); i++) {
		p[i] = std::exp(logits[i] - most);
		total += p[i];
	}
	// xorshift32
	random ^= random << 13;
	random ^= random >> 17;
	random ^= random << 5;
	float pick = (float)(random >> 8) / 16777216.0f * total;
	for (size_t i = 0; i < p.size(); i++) {
		if (pick < p[i])
			return (int)i;
		pick -= p[i];
	}
	return (int)p.size() - 1;
}

} // namespace

const NeuralPolicy::Matrix *NeuralPolicy::find(const std::string &name) const
{
	for (const auto &[key, matrix] : tensors_) {
		if (key == name)
			return &matrix;
	}
	return nullptr;
}

NeuralPolicy::Layer NeuralPolicy::layer(const std::string &prefix) const
{
	Layer l;
	l.weight = find(prefix + ".weight");
	l.bias = find(prefix + ".bias");
	return l;
}

bool NeuralPolicy::load(const std::string &path, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes)) {
		error = "can't read " + path;
		return false;
	}
	ByteReader r(bytes);
	char magic[4] = {};
	r.bytes(magic, 4);
	const uint32_t version = r.u32();
	if (std::memcmp(magic, "M3NN", 4) != 0 || version != 1) {
		error = path + " isn't a policy file (rl/export_policy.py)";
		return false;
	}
	observationSize_ = (int32_t)r.u32();
	const int32_t hidden = (int32_t)r.u32();
	r.u32(); // skills: a style code isn't used; style 0's input is learned like the rest.
	characters_.resize(r.u32());
	for (std::string &name : characters_)
		name = text::toLower(readName(r));
	const uint32_t count = r.u32();
	for (uint32_t t = 0; t < count && r.ok(); t++) {
		std::string name = readName(r);
		Matrix m;
		m.rows = (int32_t)r.u32();
		m.cols = (int32_t)r.u32();
		const size_t n = (size_t)m.rows * (size_t)m.cols;
		if (!r.at(r.pos(), n * 4)) {
			r.skip(n * 4); // past the end: marks the reader not ok
			break;
		}
		m.values.resize(n);
		for (float &v : m.values)
			v = readFloat(r);
		tensors_.emplace_back(std::move(name), std::move(m));
	}
	if (!r.ok()) {
		error = path + " is cut short";
		return false;
	}
	embedding_ = find("character.weight");
	encoder_ = layer("encoder.0");
	encoderNorm_ = layer("encoder.1");
	for (int b = 0; b < 2; b++) {
		blocks_[b] = layer("blocks." + std::to_string(b) + ".0");
		blockNorms_[b] = layer("blocks." + std::to_string(b) + ".1");
	}
	gruIn_ = find("memory.weight_ih");
	gruHidden_ = find("memory.weight_hh");
	gruInBias_ = find("memory.bias_ih");
	gruHiddenBias_ = find("memory.bias_hh");
	directionHidden_ = layer("direction.0");
	directionOut_ = layer("direction.2");
	buttonHidden_ = layer("button.0");
	buttonOut_ = layer("button.2");
	const Layer all[] = { encoder_, encoderNorm_, blocks_[0], blocks_[1], blockNorms_[0], blockNorms_[1], directionHidden_,
		directionOut_, buttonHidden_, buttonOut_ };
	bool complete = embedding_ && gruIn_ && gruHidden_ && gruInBias_ && gruHiddenBias_;
	for (const Layer &l : all)
		complete = complete && l.weight && l.bias;
	if (!complete || encoder_.weight->rows != hidden ||
		encoder_.weight->cols != observationSize_ + 2 * embedding_->cols + (find("skill.weight") ? find("skill.weight")->cols : 0) ||
		gruIn_->rows != 3 * hidden) {
		error = path + ": not a \"large\" policy network";
		return false;
	}
	hidden_ = hidden;
	return true;
}

int NeuralPolicy::characterId(const std::string &def) const
{
	const std::string wanted = text::toLower(def);
	for (size_t i = 0; i < characters_.size(); i++) {
		if (characters_[i] == wanted)
			return (int)i;
	}
	return -1;
}

void NeuralPolicy::decide(const std::vector<float> &observation, int self, int opponent, std::vector<float> &memory,
	uint32_t &random, int &direction, int &button) const
{
	direction = button = 0;
	std::vector<float> directions, buttons;
	if (!evaluate(observation, self, opponent, memory, directions, buttons))
		return;
	direction = sample(directions, random);
	button = sample(buttons, random);
}

bool NeuralPolicy::evaluate(const std::vector<float> &observation, int self, int opponent, std::vector<float> &memory,
	std::vector<float> &directions, std::vector<float> &buttons) const
{
	if (!loaded() || (int32_t)observation.size() != observationSize_ || self < 0 || opponent < 0 ||
		self >= embedding_->rows || opponent >= embedding_->rows)
		return false;
	const size_t h = (size_t)hidden_;
	if (memory.size() != h)
		memory = freshMemory();
	// The input: the observation, then both characters' embeddings, then style 0's code if there is one (Policy.encode).
	std::vector<float> in(observation);
	const int32_t e = embedding_->cols;
	for (int id : { self, opponent })
		in.insert(in.end(), embedding_->values.begin() + (size_t)id * (size_t)e,
			embedding_->values.begin() + (size_t)(id + 1) * (size_t)e);
	if (const Matrix *skill = find("skill.weight"))
		in.insert(in.end(), skill->values.begin(), skill->values.begin() + skill->cols);
	// Encoder: Linear, LayerNorm, ReLU; then two blocks, each added to what came before.
	std::vector<float> x(h), y(h);
	linear(encoder_.weight->values.data(), encoder_.bias->values.data(), hidden_, encoder_.weight->cols, in.data(), x.data());
	normRelu(x, encoderNorm_.weight->values.data(), encoderNorm_.bias->values.data());
	for (int b = 0; b < 2; b++) {
		linear(blocks_[b].weight->values.data(), blocks_[b].bias->values.data(), hidden_, hidden_, x.data(), y.data());
		normRelu(y, blockNorms_[b].weight->values.data(), blockNorms_[b].bias->values.data());
		for (size_t i = 0; i < h; i++)
			x[i] += y[i];
	}
	// The GRU cell (PyTorch's gate order r, z, n).
	std::vector<float> gi(3 * h), gh(3 * h);
	linear(gruIn_->values.data(), gruInBias_->values.data(), 3 * hidden_, hidden_, x.data(), gi.data());
	linear(gruHidden_->values.data(), gruHiddenBias_->values.data(), 3 * hidden_, hidden_, memory.data(), gh.data());
	for (size_t i = 0; i < h; i++) {
		const float r = sigmoid(gi[i] + gh[i]);
		const float z = sigmoid(gi[h + i] + gh[h + i]);
		const float n = std::tanh(gi[2 * h + i] + r * gh[2 * h + i]);
		memory[i] = (1 - z) * n + z * memory[i];
	}
	// The heads: Linear, ReLU, Linear; then a sample from each.
	auto head = [&](const Layer &first, const Layer &last) {
		std::vector<float> mid((size_t)first.weight->rows), logits((size_t)last.weight->rows);
		linear(first.weight->values.data(), first.bias->values.data(), first.weight->rows, hidden_, memory.data(), mid.data());
		for (float &v : mid)
			v = v > 0 ? v : 0;
		linear(last.weight->values.data(), last.bias->values.data(), last.weight->rows, first.weight->rows, mid.data(),
			logits.data());
		return logits;
	};
	directions = head(directionHidden_, directionOut_);
	buttons = head(buttonHidden_, buttonOut_);
	return true;
}

} // namespace m360
