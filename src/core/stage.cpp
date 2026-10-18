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

	if (const IniSection *info = findSection(sections, "info"))
		name = info->text("name");
	if (const IniSection *si = findSection(sections, "stageinfo")) {
		si->readInt("zoffset", zoffset);
		si->ints("localcoord", { &localWidth, &localHeight });
	}
	if (localWidth <= 0 || localHeight <= 0) {
		localWidth = 320;
		localHeight = 240;
	}
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
	bgs_.load(sections, "bg", sff_, anims_, defPath);
	logf("stage: %s: \"%s\", %zu backgrounds", defPath.c_str(), name.c_str(), bgs_.size());
	return true;
}

View Stage::view(float camX, float camY) const
{
	View v;
	v.camX = camX;
	v.camY = camY;
	const float screenAspect = (float)platform::SCREEN_WIDTH / platform::SCREEN_HEIGHT;
	if ((float)localWidth / (float)localHeight < screenAspect - 0.01f) {
		// A narrower stage (4:3) keeps its shape, as Ikemen's fight aspect follows the stage: its full height, black
		// bars at the sides. The play area is as wide in stage units as when it filled the width.
		v.scale = (float)platform::SCREEN_HEIGHT / (float)localHeight;
		v.width = (float)localWidth * v.scale;
		v.originX = ((float)platform::SCREEN_WIDTH - v.width) / 2;
		return v;
	}
	v.scale = (float)platform::SCREEN_WIDTH / (float)localWidth;
	// The stage height the screen can't show, taken off the top and bottom as cuthigh and cutlow divide it.
	const float cut = (float)localHeight - platform::SCREEN_HEIGHT / v.scale;
	const float high = (float)std::max(0, camera.cuthigh), low = (float)std::max(0, camera.cutlow);
	v.topCut = high + low > 0 ? cut * high / (high + low) : cut / 2;
	return v;
}

void Stage::update()
{
	bgs_.update();
}

void Stage::draw(int32_t layer, const View &view)
{
	bgs_.draw(layer, view);
}

} // namespace m360
