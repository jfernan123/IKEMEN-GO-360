// Milestone 8a: screenpack foundations. Loads the 6x9 font as FNT v1 (.fnt) and FNT v2 (.def + .sff), loads one of
// the ikemen1 fonts, and reads system.def's title backgrounds.

#include <cstdio>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/bgdef.h"
#include "core/file.h"
#include "core/font.h"
#include "core/ini.h"
#include "core/layout.h"
#include "core/sff.h"

using namespace m360;

namespace {
int passed, failed;

void expect(bool value, const char *name)
{
	if (value)
		passed++;
	else {
		failed++;
		std::printf("FAIL  8a: %s\n", name);
	}
}
} // namespace

int run8aTests()
{
	passed = failed = 0;
	std::string error;

	Font v1;
	expect(v1.load("data/font/f-6x9.fnt", error), "FNT v1 f-6x9.fnt loads");
	expect(v1.width() == 5 && v1.height() == 8, "FNT v1 size is 5x8");
	// From its [Map]: "A 0 5", "I 49 3".
	expect(v1.charWidth('A', 0) == 5 && v1.charWidth('I', 0) == 3, "FNT v1 glyph widths come from the map");
	expect(v1.charWidth(' ', 0) == 5, "a space is the font's width");
	// Five letters of 5, 3, 5, 5, 5 plus four gaps of 1.
	expect(v1.textWidth("HIAAA", 0) == 5 + 3 + 5 + 5 + 5 + 4, "FNT v1 text width adds spacing between glyphs");

	Font v2;
	expect(v2.load("data/font/f-6x9.def", error), "FNT v2 f-6x9.def loads");
	expect(v2.width() == 5 && v2.height() == 8 && v2.spacingY() == 2, "FNT v2 [Def] size and spacing");
	// Glyph widths are the sprites' own (Ikemen's LoadFntSff); this .sff's glyphs are padded to the full 5 pixels.
	expect(v2.charWidth('A', 0) == 5 && v2.charWidth('I', 0) == 5, "FNT v2 glyph widths are the sprite widths");

	Font menu;
	expect(menu.load("data/ikemen1/fonts/Menu2.def", error), "ikemen1 Menu2 font loads");
	expect(menu.textWidth("VERSUS", 0) > 0, "ikemen1 Menu2 has glyphs");

	Font missing;
	expect(!missing.load("data/font/Open_Sans.def", error), "a TrueType or missing font is refused");

	std::vector<uint8_t> bytes;
	expect(readFile("data/ikemen1/system.def", bytes), "system.def reads");
	const std::string body(bytes.begin(), bytes.end());
	const std::vector<IniSection> sections = parseIni(body);
	Sff sff;
	expect(sff.load("data/ikemen1/system.sff", error), "system.sff loads");
	AnimationTable anims;
	anims.parse("data/ikemen1/system.def", body);
	BgDef title;
	title.load(sections, "titlebg", sff, anims, "data/ikemen1/system.def");
	expect(title.size() == 9, "system.def has 9 TitleBG elements");
	BgDef none;
	none.load(sections, "nosuchbg", sff, anims, "data/ikemen1/system.def");
	expect(none.size() == 0, "an unused prefix finds no elements");

	// fight.def's [Files] names nine ikemen1 fonts, all bitmap.
	std::vector<uint8_t> fightBytes;
	expect(readFile("data/fight.def", fightBytes), "fight.def reads");
	const std::vector<IniSection> fight = parseIni(std::string(fightBytes.begin(), fightBytes.end()));
	FontTable fonts;
	if (const IniSection *files = findSection(fight, "files"))
		loadFonts(*files, "data/fight.def", fonts);
	expect(fonts.size() == 9 && fonts.count(1) && fonts.count(9), "fight.def's nine fonts load");

	// A text layout: "p1.name.font = 3,0,1" style keys.
	IniSection layoutSection;
	layoutSection.values["p1.name.font"] = "3,2,-1,255,0,128";
	layoutSection.values["p1.name.offset"] = "10,-4";
	layoutSection.values["p1.name.text"] = "\"Hi\"";
	TextLayout name;
	name.read("p1.name.", layoutSection, 1);
	expect(name.font == 3 && name.bank == 2 && name.align == -1, "text layout font, bank and align");
	expect(name.colour == 0xFFFF0080u && name.text == "Hi", "text layout colour and text");
	expect(name.layout.offsetX == 10 && name.layout.offsetY == -4, "text layout offset");

	std::printf("8a screenpack foundations: %d passed, %d failed\n", passed, failed);
	return failed;
}
