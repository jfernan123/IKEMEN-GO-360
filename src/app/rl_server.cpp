// The PC-only bridge to the reinforcement-learning trainer (rl/). See rl_server.h.

#include "app/rl_server.h"

#ifdef M360_RL
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <io.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "app/stage_demo.h"
#include "core/ai/rl_observation.h"
#include "core/fight/character.h"
#include "core/util/log.h"
#include "platform/platform.h"

namespace m360 {
// The PC-only RL bridge deliberately exposes controller inputs rather than state changes. A learned policy must play
// through the same CMD parser as a person, and can therefore train on any mounted MUGEN game without character code.
// The observation and controller code is shared with the neural CPU (core/ai/rl_observation).
// Player 2's view is mirrored, so it looks like player 1's and one policy plays both sides alike.
static std::vector<float> rlObservation(const StageDemo &demo, int side, int previousDirection, int previousButton)
{
	return m360::rlObservation(demo.fighter(side), demo.fighter(1 - side), demo.roundState(), demo.matchTimeLeft(),
		previousDirection, previousButton, side == 1);
}

static void writeRlState(const StageDemo &demo, float reward, bool done, const int previousDirection[2], const int previousButton[2])
{
	const std::vector<float> observation1 = rlObservation(demo, 0, previousDirection[0], previousButton[0]);
	const std::vector<float> observation2 = rlObservation(demo, 1, previousDirection[1], previousButton[1]);
	std::cout << "RL_OBS " << std::setprecision(7) << reward << ' ' << (done ? 1 : 0) << ' ' << demo.winner();
	for (float value : observation1)
		std::cout << ' ' << value;
	for (float value : observation2)
		std::cout << ' ' << value;
	std::cout << std::endl;
}

// recordDir: when set, the fight loads its graphics and saves one .bmp there after each reset and step, so a clip
// shows exactly what the policy played (a scripted replay can drift from the training run).
// show: the fight plays in the window with its graphics and sound, every tick drawn at 60 a second, so a policy
// can be watched live (--show with --rl-server; closing the window ends the server).
int runRlServer(const GameConfig &config, Settings &settings, const std::string &recordDir, bool show, bool trace)
{
	// Each match plays in a fight built afresh, as in the RL pool: a reused one carries state from the last match
	// (animations, game time, input buffers), so a seed wouldn't replay the same match (BUGFIX_LIST 9b, 9).
	std::unique_ptr<FightAssets> assets;
	std::unique_ptr<StageDemo> demoHolder;
	std::string error;
	const auto build = [&]() {
		demoHolder.reset();
		assets = std::make_unique<FightAssets>();
		assets->settings = &settings;
		demoHolder = std::make_unique<StageDemo>(*assets);
		if (!demoHolder->load(config, error, trace, true, true, recordDir.empty() && !show))
			return false;
		demoHolder->returnToSelectAfterMatch();
		return true;
	};
	uint32_t recorded = 0;
	const auto record = [&]() {
		if (recordDir.empty() || !platform::beginFrame(BACKGROUND))
			return;
		demoHolder->draw();
		char path[32];
		std::snprintf(path, sizeof path, "/%06u.bmp", recorded++);
		platform::requestScreenshot((recordDir + path).c_str());
		platform::endFrame();
		SpriteRenderer::newFrame();
	};
	if (!build()) {
		std::cout << "RL_ERROR " << error << std::endl;
		return 1;
	}
	bool fresh = true;
	int previousDirection[2] = { 0, 0 }, previousButton[2] = { 0, 0 };
	std::cout << "RL_READY " << rlObservation(*demoHolder, 0, 0, 0).size() << ' ' << RL_DIRECTIONS << ' ' << RL_BUTTONS
		<< std::endl;
	std::string line;
	while (std::getline(std::cin, line)) {
		std::istringstream input(line);
		std::string command;
		input >> command;
		if (command == "quit")
			break;
		if (command == "reset") {
			uint32_t seed = 1;
			std::string opponent = "smart";
			// Optionally player 2's life bar as a fraction (a training curriculum's handicap, for clips of it).
			float opponentLife = 1;
			input >> seed >> opponent >> opponentLife;
			if (!fresh && !build()) {
				std::cout << "RL_ERROR " << error << std::endl;
				return 1;
			}
			fresh = false;
			if (opponentLife > 0 && opponentLife < 1)
				demoHolder->setLifeHandicap(1, std::max(opponentLife, 0.01f));
			demoHolder->resetTraining(seed, opponent != "external");
			previousDirection[0] = previousDirection[1] = previousButton[0] = previousButton[1] = 0;
			record();
			writeRlState(*demoHolder, 0, false, previousDirection, previousButton);
			continue;
		}
		if (command != "step") {
			std::cout << "RL_ERROR expected reset, step, or quit" << std::endl;
			continue;
		}
		int direction1 = 0, button1 = 0, direction2 = 0, button2 = 0, frames = 3;
		input >> direction1 >> button1 >> direction2 >> button2 >> frames;
		direction1 = std::clamp(direction1, 0, RL_DIRECTIONS - 1);
		button1 = std::clamp(button1, 0, RL_BUTTONS - 1);
		direction2 = std::clamp(direction2, 0, RL_DIRECTIONS - 1);
		button2 = std::clamp(button2, 0, RL_BUTTONS - 1);
		frames = std::clamp(frames, 1, 30);
		float reward = 0;
		for (int frame = 0; frame < frames && !demoHolder->trainingOver(); frame++) {
			const int32_t oldSelf = demoHolder->fighter(0).life, oldOther = demoHolder->fighter(1).life;
			Buttons buttons;
			const platform::Pad p1 = rlPad(direction1, button1, demoHolder->fighter(0).facing);
			const platform::Pad p2 = rlPad(direction2, button2, demoHolder->fighter(1).facing);
			demoHolder->update(buttons, p1, p2);
			if (show) {
				if (!platform::beginFrame(BACKGROUND))
					return 0;
				demoHolder->draw();
				platform::endFrame();
				SpriteRenderer::newFrame();
				// One tick per 60th of a second, however fast the drawing went.
				static auto nextFrame = std::chrono::steady_clock::now();
				nextFrame += std::chrono::microseconds(16667);
				const auto now = std::chrono::steady_clock::now();
				if (nextFrame > now)
					std::this_thread::sleep_until(nextFrame);
				else
					nextFrame = now;
			}
			reward += (float)std::max(0, oldOther - demoHolder->fighter(1).life) /
				(float)std::max(1, demoHolder->fighter(1).lifeMax());
			reward -= (float)std::max(0, oldSelf - demoHolder->fighter(0).life) /
				(float)std::max(1, demoHolder->fighter(0).lifeMax());
		}
		const bool done = demoHolder->trainingOver();
		if (done)
			reward += demoHolder->winner() == 0 ? 1.0f : demoHolder->winner() == 1 ? -1.0f : 0.0f;
		previousDirection[0] = direction1;
		previousDirection[1] = direction2;
		previousButton[0] = button1;
		previousButton[1] = button2;
		record();
		writeRlState(*demoHolder, reward, done, previousDirection, previousButton);
	}
	return 0;
}

// --rl-pool N: N headless fights of one matchup in one process, for RL training at scale. Each character loads once
// and every fight shares it (Character::loadFrom); each fight has its own FightAssets, since effects and the fight
// screen change as it plays. Steps run on a pool of threads, and the protocol is binary on the original stdout (log
// lines go to stderr instead), all fights at once:
//   start:  "RLPL", int32 fights, int32 observation size, int32 directions, int32 buttons
//   'R' uint32 seed, uint8 external   reset every fight (fight i uses seed + i); answers the observations
//   'S' uint8 frames, then per fight uint8 direction1, button1, direction2, button2
//       answers float32 reward[N], uint8 done[N], int8 winner[N], then the observations
//   'L' float32 fraction              player 2's life bar from each fight's next match on (no answer)
//   'Q'                               quit
// Observations are float32 [N][2][size]: each fight's player 1 view, then player 2's (mirrored). A fight whose match
// ended (won, or a drawn round: winner -1) reports it (done, winner, final reward) and is reset at once, so its
// observation is the next match's first one.
namespace {

struct RlFight {
	std::unique_ptr<FightAssets> assets;
	std::unique_ptr<StageDemo> demo;
	int previousDirection[2] = { 0, 0 }, previousButton[2] = { 0, 0 };
	uint32_t seed = 0, episodes = 0;
	bool external = false;
	// Player 2's life bar as a fraction of its usual size, from the next match on (a training curriculum's handicap).
	float opponentLife = 1;

	// A new fight from the shared characters: a match left behind more than its random numbers (animations, the
	// game time, input buffers), so each match starts from a fight built afresh. That makes a seed play out the same
	// in any fight, as in a fresh process, which replays rely on.
	bool build(const GameConfig &config, Settings &settings, const std::function<const Character *(const std::string &)> &find,
		std::string &error)
	{
		demo.reset();
		assets = std::make_unique<FightAssets>();
		assets->settings = &settings;
		demo = std::make_unique<StageDemo>(*assets);
		demo->useSharedCharacters(find);
		if (!demo->load(config, error, false, true, true, true))
			return false;
		demo->returnToSelectAfterMatch();
		return true;
	}

	void reset(uint32_t count, uint32_t index, const GameConfig &config, Settings &settings,
		const std::function<const Character *(const std::string &)> &find)
	{
		std::string error;
		{
			// Loading reads files through the platform's file cache, which isn't thread-safe: one build at a time.
			static std::mutex building;
			std::lock_guard<std::mutex> lock(building);
			if (!build(config, settings, find, error))
				logf("rl pool: %s", error.c_str());
		}
		if (opponentLife != 1)
			demo->setLifeHandicap(1, opponentLife);
		demo->resetTraining(seed + index + count * episodes, !external);
		episodes++;
		previousDirection[0] = previousDirection[1] = previousButton[0] = previousButton[1] = 0;
	}

	void observe(float *out, size_t size) const
	{
		for (int side = 0; side < 2; side++) {
			const std::vector<float> o = rlObservation(*demo, side, previousDirection[side], previousButton[side]);
			std::memcpy(out + side * size, o.data(), std::min(o.size(), size) * sizeof(float));
		}
	}

	// One decision: the same frames and reward as runRlServer's step.
	void step(const uint8_t action[4], int frames, float &reward, uint8_t &done, int8_t &winner)
	{
		const int direction1 = std::min<int>(action[0], RL_DIRECTIONS - 1);
		const int button1 = std::min<int>(action[1], RL_BUTTONS - 1);
		const int direction2 = std::min<int>(action[2], RL_DIRECTIONS - 1);
		const int button2 = std::min<int>(action[3], RL_BUTTONS - 1);
		reward = 0;
		for (int frame = 0; frame < frames && !demo->trainingOver(); frame++) {
			const int32_t oldSelf = demo->fighter(0).life, oldOther = demo->fighter(1).life;
			Buttons buttons;
			const platform::Pad p1 = rlPad(direction1, button1, demo->fighter(0).facing);
			const platform::Pad p2 = rlPad(direction2, button2, demo->fighter(1).facing);
			demo->update(buttons, p1, p2);
			reward += (float)std::max(0, oldOther - demo->fighter(1).life) / (float)std::max(1, demo->fighter(1).lifeMax());
			reward -= (float)std::max(0, oldSelf - demo->fighter(0).life) / (float)std::max(1, demo->fighter(0).lifeMax());
		}
		done = demo->trainingOver() ? 1 : 0;
		winner = (int8_t)(done ? demo->winner() : -1);
		if (done)
			reward += winner == 0 ? 1.0f : winner == 1 ? -1.0f : 0.0f;
		previousDirection[0] = direction1;
		previousDirection[1] = direction2;
		previousButton[0] = button1;
		previousButton[1] = button2;
	}
};

// Runs job(worker) on every worker thread and waits for all of them; the threads stay up between calls.
class RlWorkers {
public:
	explicit RlWorkers(int count) : count_(count)
	{
		for (int t = 0; t < count; t++)
			threads_.emplace_back([this, t]() { loop(t); });
	}
	~RlWorkers()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stop_ = true;
		}
		start_.notify_all();
		for (std::thread &thread : threads_)
			thread.join();
	}
	int count() const { return count_; }
	void run(const std::function<void(int)> &job)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		job_ = &job;
		remaining_ = count_;
		generation_++;
		start_.notify_all();
		finished_.wait(lock, [this]() { return remaining_ == 0; });
		job_ = nullptr;
	}

private:
	void loop(int t)
	{
		uint64_t seen = 0;
		for (;;) {
			const std::function<void(int)> *job;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				start_.wait(lock, [&]() { return stop_ || generation_ != seen; });
				if (stop_)
					return;
				seen = generation_;
				job = job_;
			}
			(*job)(t);
			std::lock_guard<std::mutex> lock(mutex_);
			if (--remaining_ == 0)
				finished_.notify_one();
		}
	}
	int count_;
	std::vector<std::thread> threads_;
	std::mutex mutex_;
	std::condition_variable start_, finished_;
	const std::function<void(int)> *job_ = nullptr;
	uint64_t generation_ = 0;
	int remaining_ = 0;
	bool stop_ = false;
};

bool readExact(void *data, size_t size)
{
	return std::fread(data, 1, size, stdin) == size;
}

} // namespace

int runRlPool(const GameConfig &config, Settings &settings, int count, int threads)
{
	// The protocol keeps the real stdout; everything printf'd (log lines) goes to stderr from here on.
	std::fflush(stdout);
	const int protocolFd = _dup(_fileno(stdout));
	_dup2(_fileno(stderr), _fileno(stdout));
	_setmode(protocolFd, _O_BINARY);
	_setmode(_fileno(stdin), _O_BINARY);
	std::FILE *out = _fdopen(protocolFd, "wb");
	if (!out)
		return 1;
	std::setvbuf(out, nullptr, _IOFBF, 1 << 20);

	// Each character's files load once, the first time a fight asks for them; fights share them after that. Once the
	// fights are built the map only gets read, so the worker threads can rebuild fights from it.
	std::map<std::string, std::unique_ptr<Character>> loaded;
	bool loading = true;
	std::function<const Character *(const std::string &)> find = [&loaded, &loading](const std::string &def) -> const Character * {
		auto it = loaded.find(def);
		if (it == loaded.end()) {
			if (!loading)
				return nullptr;
			auto character = std::make_unique<Character>();
			std::string error;
			if (!character->load(def, 1, error, true)) {
				logf("rl pool: %s", error.c_str());
				return nullptr;
			}
			it = loaded.emplace(def, std::move(character)).first;
		}
		return it->second.get();
	};
	std::vector<std::unique_ptr<RlFight>> fights;
	for (int i = 0; i < count; i++) {
		auto fight = std::make_unique<RlFight>();
		std::string error;
		if (!fight->build(config, settings, find, error)) {
			logf("rl pool: %s", error.c_str());
			return 1;
		}
		fights.push_back(std::move(fight));
	}
	loading = false;
	const size_t size = rlObservation(*fights[0]->demo, 0, 0, 0).size();
	const int32_t header[4] = { count, (int32_t)size, RL_DIRECTIONS, RL_BUTTONS };
	std::fwrite("RLPL", 1, 4, out);
	std::fwrite(header, sizeof header, 1, out);
	std::fflush(out);

	RlWorkers workers(std::max(1, std::min(threads, count)));
	std::vector<float> observations((size_t)count * 2 * size), rewards((size_t)count);
	std::vector<uint8_t> actions((size_t)count * 4), done((size_t)count);
	std::vector<int8_t> winners((size_t)count);
	auto writeObservations = [&]() {
		std::fwrite(observations.data(), sizeof(float), observations.size(), out);
		std::fflush(out);
	};
	for (;;) {
		char command = 0;
		if (!readExact(&command, 1) || command == 'Q')
			break;
		if (command == 'R') {
			uint32_t seed = 1;
			uint8_t external = 0;
			if (!readExact(&seed, 4) || !readExact(&external, 1))
				break;
			workers.run([&](int t) {
				for (int i = t; i < count; i += workers.count()) {
					RlFight &fight = *fights[(size_t)i];
					fight.seed = seed;
					fight.episodes = 0;
					fight.external = external != 0;
					fight.reset((uint32_t)count, (uint32_t)i, config, settings, find);
					fight.observe(&observations[(size_t)i * 2 * size], size);
				}
			});
			writeObservations();
		} else if (command == 'S') {
			uint8_t frames = 3;
			if (!readExact(&frames, 1) || !readExact(actions.data(), actions.size()))
				break;
			const int ticks = std::clamp<int>(frames, 1, 30);
			workers.run([&](int t) {
				for (int i = t; i < count; i += workers.count()) {
					RlFight &fight = *fights[(size_t)i];
					fight.step(&actions[(size_t)i * 4], ticks, rewards[(size_t)i], done[(size_t)i], winners[(size_t)i]);
					if (done[(size_t)i])
						fight.reset((uint32_t)count, (uint32_t)i, config, settings, find);
					fight.observe(&observations[(size_t)i * 2 * size], size);
				}
			});
			std::fwrite(rewards.data(), sizeof(float), rewards.size(), out);
			std::fwrite(done.data(), 1, done.size(), out);
			std::fwrite(winners.data(), 1, winners.size(), out);
			writeObservations();
		} else if (command == 'L') {
			float fraction = 1;
			if (!readExact(&fraction, 4))
				break;
			fraction = std::clamp(fraction, 0.01f, 1.0f);
			for (auto &fight : fights)
				fight->opponentLife = fraction;
		} else {
			logf("rl pool: unknown command %d", (int)command);
			break;
		}
	}
	return 0;
}

} // namespace m360
#endif
