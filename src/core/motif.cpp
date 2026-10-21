#include "core/motif.h"

#include <algorithm>

#include "core/file.h"
#include "core/log.h"
#include "core/text.h"

namespace m360 {

bool Motif::load(const std::string &defPath, std::string &error)
{
	std::vector<uint8_t> bytes;
	if (!readFile(defPath, bytes)) {
		error = "can't read " + defPath;
		return false;
	}
	const std::string body(bytes.begin(), bytes.end());
	sections_ = parseIni(body);
	anims_.parse(defPath, body);
	path_ = defPath;
	if (const IniSection *info = section("info"))
		info->ints("localcoord", { &localWidth_, &localHeight_ });
	if (localWidth_ <= 0 || localHeight_ <= 0) {
		localWidth_ = 320;
		localHeight_ = 240;
	}
	space_ = ScreenSpace::forLocalcoord(localWidth_, localHeight_);

	const IniSection *files = section("files");
	if (!files) {
		error = defPath + " has no [Files]";
		return false;
	}
	// Files next to the def, else under data/.
	const std::string dir = directoryOf(defPath);
	const std::string spr = files->text("spr");
	std::string sffError;
	if (spr.empty() || (!sff_.load(dir + spr, sffError) && !sff_.load("data/" + spr, sffError))) {
		error = defPath + ": " + (spr.empty() ? "no spr in [Files]" : sffError);
		return false;
	}
	renderer_ = std::make_unique<SpriteRenderer>(sff_);
	const std::string snd = files->text("snd");
	std::string sndError;
	if (!snd.empty() && !snd_.load(dir + snd, sndError) && !snd_.load("data/" + snd, sndError))
		logf("motif: %s: no sounds: %s", defPath.c_str(), sndError.c_str());
	loadFonts(*files, defPath, fonts_);
	logf("motif: %s: %dx%d, %zu fonts", defPath.c_str(), (int)localWidth_, (int)localHeight_, fonts_.size());
	return true;
}

View Motif::view() const
{
	View v;
	v.scale = space_.scale;
	v.topCut = -space_.originY / space_.scale;
	v.originX = space_.originX;
	v.width = space_.width;
	return v;
}

void Motif::loadBg(const std::string &prefix, BgDef &bg, uint32_t &clearColour) const
{
	bg.load(sections_, prefix, sff_, anims_, path_);
	clearColour = 0xFF000000;
	if (const IniSection *def = section(prefix + "def")) {
		int32_t c[3] = { 0, 0, 0 };
		if (def->ints("bgclearcolor", { &c[0], &c[1], &c[2] }))
			clearColour = 0xFF000000 | (uint32_t)std::clamp(c[0], 0, 255) << 16 | (uint32_t)std::clamp(c[1], 0, 255) << 8 |
				(uint32_t)std::clamp(c[2], 0, 255);
	}
}

void Motif::play(const int32_t snd[2]) const
{
	if (snd[0] >= 0)
		snd_.play(snd[0], snd[1]);
}

MusicTrack Motif::music(const std::string &screen) const
{
	MusicTrack track;
	if (const IniSection *s = section("music"))
		track.readMotif(screen + ".bgm", *s);
	track.resolve(path_);
	return track;
}

} // namespace m360
