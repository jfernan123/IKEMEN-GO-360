#include "core/stage.h"

#include <cmath>

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

bool Stage::load(const std::string &defPath, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(defPath, bytes)) {
		error = "can't read " + defPath;
		return false;
	}
	const std::string body(bytes.begin(), bytes.end());
	const std::vector<IniSection> sections = parseIni(body);
	// The stage's own animations, for "type = anim" elements.
	anims_.parse(defPath, body);

	if (const IniSection *info = findSection(sections, "info")) {
		name = info->text("name");
		float major = 0, minor = 0;
		ikemen = info->floats("ikemenversion", { &major, &minor }) && (major != 0 || minor != 0);
	}
	if (const IniSection *si = findSection(sections, "stageinfo")) {
		si->readInt("zoffset", zoffset);
		si->ints("localcoord", { &localWidth, &localHeight });
		int32_t flag = 1;
		resetBg = !si->readInt("resetbg", flag) || flag != 0;
	}
	if (localWidth <= 0 || localHeight <= 0) {
		localWidth = 320;
		localHeight = 240;
	}
	gameWidth = 240.0f * (float)localWidth / (float)localHeight;
	localScale = gameWidth / (float)localWidth;
	// Defaults are written for 320-wide stages, as for characters.
	const float coordRatio = localWidth / 320.0f;
	p[0] = { (int32_t)(-70 * coordRatio), 0, 1 };
	p[1] = { (int32_t)(70 * coordRatio), 0, -1 };
	screenleft = (int32_t)(15 * coordRatio);
	screenright = (int32_t)(15 * coordRatio);
	if (const IniSection *b = findSection(sections, "bound")) {
		b->readInt("screenleft", screenleft);
		b->readInt("screenright", screenright);
	}
	if (const IniSection *pi = findSection(sections, "playerinfo")) {
		for (int i = 0; i < 2; i++) {
			const std::string n = std::to_string(i + 1);
			pi->readInt("p" + n + "startx", p[i].x);
			pi->readInt("p" + n + "starty", p[i].y);
			pi->readInt("p" + n + "facing", p[i].facing);
		}
		pi->readFloat("leftbound", leftbound);
		pi->readFloat("rightbound", rightbound);
	}
	if (const IniSection *c = findSection(sections, "camera")) {
		c->readInt("startx", camera.startx);
		c->readInt("starty", camera.starty);
		c->readInt("boundleft", camera.boundleft);
		c->readInt("boundright", camera.boundright);
		c->readInt("boundhigh", camera.boundhigh);
		c->readInt("boundlow", camera.boundlow);
		c->readFloat("verticalfollow", camera.verticalfollow);
		c->readInt("floortension", camera.floortension);
		c->readInt("tension", camera.tension);
		c->readInt("cuthigh", camera.cuthigh);
		c->readInt("cutlow", camera.cutlow);
		c->readInt("overdrawhigh", camera.overdrawhigh);
		c->readInt("overdrawlow", camera.overdrawlow);
		c->readFloat("tensionvel", camera.tensionvel);
		c->readFloat("startzoom", camera.startzoom);
		c->readFloat("zoomin", camera.zoomin);
		c->readFloat("zoomout", camera.zoomout);
		c->readFloat("zoomindelay", camera.zoomindelay);
		c->readFloat("zoominspeed", camera.zoominspeed);
		c->readFloat("zoomoutspeed", camera.zoomoutspeed);
		c->readFloat("yscrollspeed", camera.yscrollspeed);
		c->readFloat("boundhighzoomdelta", camera.boundhighzoomdelta);
		c->readFloat("verticalfollowzoomdelta", camera.verticalfollowzoomdelta);
		int32_t flag = 0;
		camera.autocenter = c->readInt("autocenter", flag) && flag != 0;
		camera.lowestcap = c->readInt("lowestcap", flag) && flag != 0;
		// "tensionlow" turns on the vertical tension (Ikemen's stage loading).
		if (c->readInt("tensionlow", camera.tensionlow)) {
			camera.ytensionenable = true;
			c->readInt("tensionhigh", camera.tensionhigh);
		}
	}
	if (const IniSection *s = findSection(sections, "shadow")) {
		s->readInt("intensity", shadow.intensity);
		shadow.intensity = std::clamp(shadow.intensity, 0, 255);
		s->readFloat("yscale", shadow.yscale);
		s->ints("fade.range", { &shadow.fadeEnd, &shadow.fadeBegin });
	}
	if (const IniSection *r = findSection(sections, "reflection")) {
		r->readInt("intensity", reflection);
		reflection = std::clamp(reflection, 0, 255);
	}
	music = MusicTrack{};
	if (const IniSection *m = findSection(sections, "music"))
		music.readStage(*m);
	music.resolve(defPath);

	// The sprites: [BGdef] spr, next to the .def, else under data/.
	const IniSection *bgdef = findSection(sections, "bgdef");
	const std::string spr = bgdef ? bgdef->text("spr") : std::string();
	if (spr.empty()) {
		error = defPath + " names no sprite file ([BGdef] spr)";
		return false;
	}
	std::string sffError;
	if (!sff_.load(directoryOf(defPath) + spr, sffError) && !sff_.load("data/" + spr, sffError)) {
		error = sffError;
		return false;
	}

	// Backgrounds, the [BG ...] sections.
	bgs_.setStage(true);
	bgs_.load(sections, "bg", sff_, anims_, defPath);
	logUnread(sections, defPath);
	logf("stage: %s: \"%s\", %zu backgrounds", defPath.c_str(), name.c_str(), bgs_.size());
	return true;
}

// One log line of what the stage sets that isn't read yet (the background elements log their own): whole sections
// as "[name]", else "[section] key, key". The .def's comments and author details don't count.
void Stage::logUnread(const std::vector<IniSection> &sections, const std::string &defPath)
{
	struct Known {
		const char *section;
		std::initializer_list<const char *> keys;
	};
	static const Known KNOWN[] = {
		{ "info", { "name", "displayname", "versiondate", "mugenversion", "author", "ikemenversion" } },
		{ "stageinfo", { "zoffset", "localcoord", "portraitscale", "resetbg" } },
		{ "bound", { "screenleft", "screenright" } },
		{ "playerinfo", { "p1startx", "p1starty", "p1facing", "p2startx", "p2starty", "p2facing", "leftbound",
							"rightbound" } },
		{ "camera", { "startx", "starty", "boundleft", "boundright", "boundhigh", "boundlow", "verticalfollow",
						"floortension", "tension", "cuthigh", "cutlow", "overdrawhigh", "overdrawlow", "tensionvel",
						"startzoom", "zoomin", "zoomout", "zoomindelay", "zoominspeed", "zoomoutspeed", "yscrollspeed",
						"boundhighzoomdelta", "verticalfollowzoomdelta", "autocenter", "lowestcap", "tensionlow",
						"tensionhigh" } },
		{ "shadow", { "intensity", "yscale", "fade.range" } },
		{ "reflection", { "intensity" } },
		{ "music", { "bgmusic", "bgmvolume", "bgmloop", "bgmloopstart", "bgmloopend" } },
		{ "bgdef", { "spr", "debugbg" } },
	};
	std::string report;
	for (const IniSection &s : sections) {
		// Background elements log their own; "[Begin Action n]" is the stage's animations.
		if (text::startsWith(s.lower, "bg ") || s.lower == "bg" || text::startsWith(s.lower, "begin action"))
			continue;
		const Known *known = nullptr;
		for (const Known &k : KNOWN)
			known = s.lower == k.section ? &k : known;
		if (!known) {
			report += (report.empty() ? "[" : "; [") + s.name + "]";
			continue;
		}
		const std::vector<std::string> keys = unreadKeys(s, known->keys);
		if (keys.empty())
			continue;
		report += (report.empty() ? "[" : "; [") + s.name + "]";
		for (size_t i = 0; i < keys.size(); i++)
			report += (i == 0 ? " " : ", ") + keys[i];
	}
	if (!report.empty())
		logf("stage: %s: not read yet: %s", defPath.c_str(), report.c_str());
}

View Stage::view(float camX, float camY) const
{
	View v;
	v.camX = camX;
	v.camY = camY;
	const float screenAspect = (float)platform::SCREEN_WIDTH / platform::SCREEN_HEIGHT;
	if ((float)localWidth / (float)localHeight < screenAspect - 0.01f) {
		// A narrower stage (4:3) keeps its shape, as Ikemen's fight aspect follows the stage: its full height, black
		// bars at the sides.
		v.scale = (float)platform::SCREEN_HEIGHT / 240.0f;
		v.width = gameWidth * v.scale;
		v.originX = ((float)platform::SCREEN_WIDTH - v.width) / 2;
		return v;
	}
	v.scale = (float)platform::SCREEN_WIDTH / gameWidth;
	// The stage height the screen can't show, taken off the top and bottom as cuthigh and cutlow divide it.
	const float cut = (float)localHeight * localScale - platform::SCREEN_HEIGHT / v.scale;
	const float high = (float)std::max(0, camera.cuthigh), low = (float)std::max(0, camera.cutlow);
	v.topCut = high + low > 0 ? cut * high / (high + low) : cut / 2;
	return v;
}

void Stage::reset()
{
	if (resetBg)
		bgs_.reset();
}

void Stage::update()
{
	bgs_.update();
}

void Stage::draw(int32_t layer, const View &view)
{
	// The backgrounds are in stage units: the same view, measured in them.
	View local = view;
	local.camX = view.camX / localScale;
	local.camY = view.camY / localScale;
	local.topCut = view.topCut / localScale;
	local.ground = view.ground / localScale;
	local.scale = view.scale * localScale;
	bgs_.draw(layer, local);
}

} // namespace m360
