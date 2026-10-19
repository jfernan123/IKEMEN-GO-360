#include "core/storyboard.h"

#include <algorithm>
#include <cmath>

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"
#include "platform/platform.h"

namespace m360 {

namespace {

// [Scene n] in file order ("scene", "scene 0", "scene intro").
bool isScene(const IniSection &s)
{
	return s.lower == "scene" || text::startsWith(s.lower, "scene ");
}

// The text a layer shows after `ticks` of typing at `delay` ticks a letter (Ikemen's StepTypewriter), counting
// UTF-8 letters.
std::string typed(const std::string &line, int32_t ticks, float delay)
{
	if (delay <= 0)
		return line;
	const size_t letters = (size_t)std::max(0.0f, std::floor((float)ticks / delay) + 1);
	size_t count = 0, i = 0;
	while (i < line.size() && count < letters) {
		const uint8_t c = (uint8_t)line[i];
		i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
		count++;
	}
	return line.substr(0, std::min(i, line.size()));
}

} // namespace

bool Storyboard::load(const std::string &defPath, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(defPath, bytes)) {
		error = "can't read " + defPath;
		return false;
	}
	path_ = defPath;
	const std::string body(bytes.begin(), bytes.end());
	sections_ = parseIni(body);
	anims_.parse(defPath, body);
	int32_t localW = 320, localH = 240;
	if (const IniSection *info = findSection(sections_, "info"))
		info->ints("localcoord", { &localW, &localH });
	space_ = ScreenSpace::forLocalcoord(localW > 0 ? localW : 320, localH > 0 ? localH : 240);

	// [SceneDef]: the files, found as Ikemen's lookup "def,,data/" finds them; the first scene; the music rule.
	const IniSection *def = findSection(sections_, "scenedef");
	if (def) {
		const std::string spr = def->text("spr");
		std::string sffError;
		if (!spr.empty() && !sff_.load(findBesideDef(directoryOf(defPath), spr), sffError))
			logf("storyboard: %s: %s", defPath.c_str(), sffError.c_str());
		const std::string snd = def->text("snd");
		std::string sndError;
		if (!snd.empty())
			hasSnd_ = snd_.load(findBesideDef(directoryOf(defPath), snd), sndError);
		loadFonts(*def, defPath, fonts_);
		def->readInt("startscene", startScene_);
		int32_t stop = 1;
		stopMusic_ = !def->readInt("stopmusic", stop) || stop != 0;
	}
	renderer_ = std::make_unique<SpriteRenderer>(sff_);

	// The scenes, each over the values the one before carried (loadStoryboard's keysToCheck).
	std::string carried[4];
	const char *const CARRIED_KEYS[4] = { "clearcolor", "clearalpha", "clearlayerno", "layerall.pos" };
	for (const IniSection &original : sections_) {
		if (!isScene(original))
			continue;
		IniSection s = original;
		for (int k = 0; k < 4; k++) {
			if (s.has(CARRIED_KEYS[k]))
				carried[k] = s.text(CARRIED_KEYS[k]);
			else if (!carried[k].empty())
				s.values[CARRIED_KEYS[k]] = carried[k];
		}
		Scene scene;
		s.readInt("end.time", scene.endTime);
		scene.fadeIn.time = scene.fadeOut.time = 0;
		scene.fadeIn.read("fadein.", s);
		scene.fadeOut.read("fadeout.", s);
		int32_t c[3] = { -1, 0, 0 };
		if (s.ints("clearcolor", { &c[0], &c[1], &c[2] }) && c[0] >= 0)
			scene.clearColour = (int32_t)((uint32_t)std::clamp(c[0], 0, 255) << 16 | (uint32_t)std::clamp(c[1], 0, 255) << 8 |
				(uint32_t)std::clamp(c[2], 0, 255));
		int32_t alpha[2] = { 255, 0 };
		s.ints("clearalpha", { &alpha[0], &alpha[1] });
		scene.clearAlpha = std::clamp(alpha[0], 0, 255);
		s.floats("layerall.pos", { &scene.layerAllX, &scene.layerAllY });
		const std::string bgName = text::toLower(std::string(text::trim(s.text("bg.name"))));
		if (!bgName.empty()) {
			scene.bg = std::make_unique<BgDef>();
			scene.bg->load(sections_, bgName, sff_, anims_, defPath);
		}
		// Layers: layer0..layer9 (Ikemen also takes layer_n), in number order.
		for (int n = 0; n < 10; n++) {
			std::string prefix = "layer" + std::to_string(n) + ".";
			bool found = false;
			for (const auto &[key, value] : s.values) {
				if (text::startsWith(key, prefix)) {
					found = true;
					break;
				}
			}
			if (!found)
				continue;
			Layer layer;
			layer.anim.read(prefix, s, anims_, 2);
			layer.text.read(prefix, s, 0, 2);
			layer.hasText = !layer.text.text.empty();
			s.readInt(prefix + "starttime", layer.startTime);
			s.readInt(prefix + "endtime", layer.endTime);
			s.readFloat(prefix + "textdelay", layer.textDelay);
			scene.layers.push_back(std::move(layer));
		}
		for (int n = 0; n < 32; n++) {
			const std::string prefix = "sound" + std::to_string(n) + ".";
			Sound sound;
			if (!s.ints(prefix + "value", { &sound.value[0], &sound.value[1] }))
				continue;
			s.readInt(prefix + "starttime", sound.startTime);
			scene.sounds.push_back(sound);
		}
		if (s.has("bgm") && !std::string(text::trim(s.text("bgm"))).empty()) {
			scene.bgm.readMotif("bgm", s);
			scene.bgm.resolve(defPath);
			scene.hasBgm = true;
		}
		if (s.has("jump"))
			s.readInt("jump", scene.jump);
		scenes_.push_back(std::move(scene));
	}
	if (scenes_.empty()) {
		error = defPath + " has no scenes";
		return false;
	}
	logf("storyboard: %s: %zu scenes", defPath.c_str(), scenes_.size());
	return true;
}

void Storyboard::start()
{
	scene_ = std::clamp(startScene_, 0, (int32_t)scenes_.size() - 1);
	counter_ = 0;
	endTimer_ = -1;
	active_ = true;
	canceled_ = false;
	for (Scene &scene : scenes_) {
		for (Layer &layer : scene.layers)
			layer.anim.reset();
		if (scene.bg)
			scene.bg->reset();
	}
}

const MusicTrack *Storyboard::music() const
{
	if (!active_ || scene_ < 0 || scene_ >= (int32_t)scenes_.size())
		return nullptr;
	const Scene &scene = scenes_[(size_t)scene_];
	return scene.hasBgm && !scene.bgm.path.empty() ? &scene.bgm : nullptr;
}

// Ikemen's Storyboard.step.
void Storyboard::update(bool skip, bool cancel)
{
	if (!active_)
		return;
	if (scene_ < 0 || scene_ >= (int32_t)scenes_.size()) {
		active_ = false;
		return;
	}
	Scene &scene = scenes_[(size_t)scene_];
	if (scene.bg)
		scene.bg->update();
	const bool sceneJustStarted = counter_ == 0;
	if (cancel)
		canceled_ = true;

	// The fade-out starts its length before end.time (Ikemen's startFadeAt), at once on a skip or cancel.
	int32_t startFadeAt = -1;
	if (scene.endTime > 0)
		startFadeAt = std::max(0, scene.endTime - (scene.fadeOut.duration() > 0 ? scene.fadeOut.duration() : 1));
	const bool reachedEnd = scene.endTime <= 0 || (startFadeAt >= 0 && counter_ >= startFadeAt);
	if (endTimer_ == -1 && (reachedEnd || canceled_ || skip)) {
		scene.fadeIn.stop();
		scene.fadeOut.start(false);
		endTimer_ = counter_ + scene.fadeOut.duration();
	}
	if (sceneJustStarted && endTimer_ == -1)
		scene.fadeIn.start(true);

	if (hasSnd_) {
		for (const Sound &sound : scene.sounds) {
			if (counter_ == sound.startTime && sound.value[0] >= 0)
				snd_.play(sound.value[0], sound.value[1]);
		}
	}
	for (Layer &layer : scene.layers) {
		if (counter_ >= layer.startTime && (layer.endTime <= 0 || counter_ < layer.endTime)) {
			layer.anim.action();
			layer.text.step();
		}
	}
	scene.fadeIn.step();
	scene.fadeOut.step();

	// The scene ends once its fade-out has played: the next one (or its jump), or the end.
	if (endTimer_ != -1 && counter_ >= endTimer_) {
		counter_ = 0;
		endTimer_ = -1;
		scene.fadeOut.stop();
		if (canceled_)
			scene_ = (int32_t)scenes_.size();
		else if (scene.jump >= 0 && scene.jump != scene_)
			scene_ = scene.jump;
		else
			scene_++;
		if (scene_ >= (int32_t)scenes_.size()) {
			active_ = false;
			return;
		}
		Scene &next = scenes_[(size_t)scene_];
		for (Layer &layer : next.layers)
			layer.anim.reset();
		next.fadeIn.start(true);
		return;
	}
	counter_++;
}

// Ikemen's Storyboard.draw: the clear colour, the background, then each layer in its time window, by layer number.
void Storyboard::draw()
{
	platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT, 0xFF000000);
	if (scene_ < 0 || scene_ >= (int32_t)scenes_.size())
		return;
	const Scene &scene = scenes_[(size_t)scene_];
	if (scene.clearColour >= 0)
		platform::fillRect(0, 0, platform::SCREEN_WIDTH, platform::SCREEN_HEIGHT,
			(uint32_t)scene.clearAlpha << 24 | (uint32_t)scene.clearColour);
	View view;
	view.scale = space_.scale;
	view.topCut = -space_.originY / space_.scale;
	for (int32_t layerNo = 0; layerNo <= 2; layerNo++) {
		if (scene.bg && layerNo < 2)
			scene.bg->draw(layerNo, view);
		for (const Layer &layer : scene.layers) {
			if (counter_ < layer.startTime || (layer.endTime > 0 && counter_ >= layer.endTime))
				continue;
			const float x = scene.layerAllX, y = scene.layerAllY;
			if (layer.anim.present)
				layer.anim.draw(x, y, layerNo, *renderer_, space_);
			if (layer.hasText)
				layer.text.draw(typed(layer.text.text, counter_ - layer.startTime, layer.textDelay), x, y, layerNo, fonts_,
					space_);
		}
	}
	if (scene.fadeOut.isActive())
		scene.fadeOut.draw();
	else if (scene.fadeIn.isActive())
		scene.fadeIn.draw();
	space_.drawBars();
}

} // namespace m360
