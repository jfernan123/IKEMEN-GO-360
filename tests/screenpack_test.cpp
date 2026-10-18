// Milestone 8a: screenpack foundations. Loads the 6x9 font as FNT v1 (.fnt) and FNT v2 (.def + .sff), loads one of
// the ikemen1 fonts, and reads system.def's title backgrounds.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/air.h"
#include "core/bgdef.h"
#include "core/file.h"
#include "core/font.h"
#include "core/ini.h"
#include "core/fight_screen.h"
#include "core/layout.h"
#include "core/game_config.h"
#include "core/motif.h"
#include "core/select_screen.h"
#include "core/sprite_renderer.h"
#include "core/title_screen.h"
#include "core/versus_screen.h"
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

// Milestone 8b: the fight screen. fight.def's [Round] timings, the layouts' colour pulse, and how long a call shows.
int run8bTests()
{
	passed = failed = 0;
	std::string error;

	FightScreen fight;
	expect(fight.load("data/fight.def", error), "fight.def loads as a fight screen");
	const RoundTimings &t = fight.timings();
	expect(t.ctrlTime == 50 && t.overTime == 300 && t.startWait == 30, "[Round] ctrl.time 50, over.time 300");
	expect(t.slowTime == 60 && t.slowFade == 45 && t.slowSpeed == 0.25f, "[Round] KO slowdown");
	expect(t.overHit == 10 && t.overWait == 45 && t.overWin == 45 && t.forceWin == 900, "[Round] over.* times");
	expect(t.matchWins == 2 && t.fadeOutTime == 30 && t.roundOverTime() == 300, "[Round] match.wins and fade-out");

	// "palfx.mul = 200,200,200" with "sinmul = 32,32,32,5": 200 at the start of the wave, then 200 + 32 sin(72 deg).
	IniSection section;
	section.values["p1.mid.palfx.mul"] = "200, 200, 200";
	section.values["p1.mid.palfx.sinmul"] = "32, 32, 32, 5";
	PalFX pulse;
	pulse.read("p1.mid.palfx.", section);
	expect(pulse.time == -1 && pulse.cycle[1] == 5, "palfx lasts forever with a period of 5");
	pulse.step();
	expect(pulse.effectMul[0] == 200, "palfx starts at its multiply");
	pulse.step();
	expect(pulse.effectMul[0] == 230, "palfx sinmul swings it up");
	SpriteDraw how;
	pulse.apply(how);
	expect(how.mulR == 232, "palfx is drawn in steps of 8");
	for (int i = 0; i < 5; i++)
		pulse.step();
	expect(pulse.effectMul[0] == 230, "palfx repeats every period");

	// A call shown for displaytime 55 is done on its 55th tick.
	IniSection round;
	round.values["round.default.displaytime"] = "55";
	AnimTextSnd call;
	AnimationTable none;
	call.read("round.default.", round, none, 2);
	expect(!call.end(54, true) && call.end(55, true), "displaytime ends a call");
	AnimTextSnd empty;
	expect(empty.end(1, true), "a call with nothing to show is done at once");

	std::printf("8b fight screen: %d passed, %d failed\n", passed, failed);
	return failed;
}

// Milestone 8c: the title screen. system.def as a motif, its title background, the menu built from ikemen1's
// itemname keys (with the modes mugen360 can't run left out), fonts named by file, and moving around the menu.
int run8cTests()
{
	passed = failed = 0;
	std::string error;

	const std::vector<IniSection> ordered = parseIni("[A]\nb = 1\na = 2\nb = 3\n");
	expect(ordered.size() == 1 && ordered[0].keys == std::vector<std::string>{ "b", "a" } && ordered[0].text("b") == "1",
		"ini keys keep file order, first value wins");

	Motif motif;
	expect(motif.load("data/ikemen1/system.def", error), "system.def loads as a motif");
	expect(motif.localWidth() == 1280 && motif.localHeight() == 720, "motif localcoord 1280x720");
	BgDef titleBg;
	uint32_t clear = 0;
	motif.loadBg("titlebg", titleBg, clear);
	expect(titleBg.size() == 9 && clear == 0xFFFFFFFFu, "TitleBG: 9 elements on white");
	expect(titleBg.size() > 7 && titleBg.element(7).sinRadius[1] == 15 && titleBg.element(7).sinPeriod[1] == 264,
		"the logo bobs: sin.y = 15, 264");

	TextLayout footer;
	footer.fontFile = "default-3x5.def";
	FontTable fonts;
	FontFiles files;
	resolveFontFile(footer, fonts, files, "data/ikemen1/system.def");
	expect(footer.font == 1000 && fonts.count(1000) == 1, "a font named by file loads under a free id");

	TitleScreen title;
	expect(title.load(motif, error), "[Title Info] loads");
	const std::vector<TitleScreen::Item> &menu = title.menu();
	std::string top;
	for (const TitleScreen::Item &item : menu)
		top += item.display + "|";
	expect(top == "ARCADE|VS MODE|PRACTICE|OPTIONS|EXIT|", "the menu keeps what can run, in order");
	std::string versus;
	if (menu.size() > 1)
		for (const TitleScreen::Item &item : menu[1].children)
			versus += item.name + "|";
	expect(versus == "versus|freebattle|back|", "VS MODE: 1P VS 2P, QUICK MATCH, BACK");
	const TitleScreen::Item *arcade = menu.empty() ? nullptr : TitleScreen::singleAction(menu[0]);
	expect(arcade && arcade->name == "arcade", "ARCADE runs its one mode directly");
	expect(menu.size() > 2 && !TitleScreen::singleAction(menu[1]), "VS MODE opens as a submenu");

	// Wait out the fade, then down five times wraps back to the top, and up once goes to the end.
	platform::Pad idle, downPad, upPad;
	downPad.down = true;
	upPad.up = true;
	for (int i = 0; i < 15; i++)
		title.update(idle, idle);
	for (int i = 0; i < 5; i++) {
		title.update(downPad, idle);
		title.update(idle, idle);
	}
	expect(title.selected() == "menuarcade", "down past the end wraps to the top");
	title.update(upPad, idle);
	expect(title.selected() == "exit", "up from the top wraps to the end");

	std::printf("8c title screen: %d passed, %d failed\n", passed, failed);
	return failed;
}

// Milestone 8d: the select screen. The renderer's 3D turn, ikemen1's grid with its per-row overrides, the cursor's
// movement, the palette menu's cycling and the random cell.
int run8dTests()
{
	passed = failed = 0;
	std::string error;

	// A point 50 right and 20 below an axis at (400, 300).
	SpriteDraw flat;
	float ox = 0, oy = 0;
	SpriteRenderer::turnPoint(flat, 400, 300, 450, 320, ox, oy);
	expect(ox == 450 && oy == 320, "no angles: points stay put");
	SpriteDraw edgeOn;
	edgeOn.yangle = 90;
	SpriteRenderer::turnPoint(edgeOn, 400, 300, 450, 320, ox, oy);
	expect(std::fabs(ox - 400) < 0.01f && oy == 320, "yangle 90, flat: the width collapses");
	SpriteDraw turned;
	turned.yangle = 70;
	turned.perspective = true;
	turned.focalLength = 400;
	float nearTop = 0, nearBottom = 0, farTop = 0, farBottom = 0, x = 0;
	SpriteRenderer::turnPoint(turned, 400, 300, 350, 200, x, nearTop);
	SpriteRenderer::turnPoint(turned, 400, 300, 350, 400, x, nearBottom);
	SpriteRenderer::turnPoint(turned, 400, 300, 450, 200, x, farTop);
	SpriteRenderer::turnPoint(turned, 400, 300, 450, 400, x, farBottom);
	expect(farBottom - farTop < nearBottom - nearTop, "yangle 70 in perspective: the far edge is shorter");

	Motif motif;
	expect(motif.load("data/ikemen1/system.def", error), "system.def loads");
	GameConfig config;
	config.characters = { "data/chars/kfm/kfm.def", "data/chars/kfm720/kfm720.def", "randomselect" };
	MotifSelect select;
	expect(select.load(motif, config, error), "[Select Info] and the roster load");
	expect(select.rosterSize() == 3 && select.isRandom(2) && !select.isRandom(0), "randomselect is the third cell");

	// ikemen1: cell.size 24; "cell.*-0" spacing 7.5 (both ways), offset -11,-46; "cell.*-9" spacing 23,5, scale 0.46.
	const MotifSelect::Point first = select.cellPosition(0, 0), last = select.cellPosition(3, 9);
	expect(first.x == -11 && first.y == -46, "cell 0,0 position with its row's offset");
	expect(last.x == 3 * (24 + 23) - 73 && last.y == 9 * (24 + 5), "cell 3,9 position with its row's spacing");
	expect(std::fabs(select.cellScale(3, 9) - 0.46f) < 0.001f, "row 9 cells are scaled 0.46");

	int32_t cx = 7, cy = 0;
	select.cellMovement(cx, cy, 1, 0);
	expect(cx == 0 && cy == 0, "right from the last column wraps");
	cx = 0;
	cy = 9;
	select.cellMovement(cx, cy, 0, 1);
	expect(cx == 0 && cy == 0, "down from the last row wraps");
	cx = 2;
	cy = 0;
	select.cellMovement(cx, cy, 1, 0);
	expect(cx == 3 && cy == 0, "the cursor moves onto an empty cell");

	// Three palettes: 1, 2, 3, then "?" (4).
	expect(MotifSelect::cyclePalette(3, 3, 1) == 4 && MotifSelect::cyclePalette(4, 3, 1) == 1, "palette menu: next wraps through ?");
	expect(MotifSelect::cyclePalette(1, 3, -1) == 4, "palette menu: previous from 1 goes to ?");

	std::printf("8d select screen: %d passed, %d failed\n", passed, failed);
	return failed;
}

int run8eTests()
{
	passed = failed = 0;
	std::string error;
	Motif motif;
	expect(motif.load("data/ikemen1/system.def", error), "system.def loads");
	VersusScreen versus;
	versus.load(motif);
	expect(versus.faceAnim() == 0, "ikemen1's VS portraits are the stance animation (p1.anim = 0)");
	expect(versus.stageSprite(0) == 9000 && versus.stageSprite(1) == 1, "the stage portrait is 9000,1");

	// ikemen1: p1.offset 444, spacing -100, num 4, padding 1: one character is centred in the room for four.
	expect(VersusScreen::paddedX(444, -100, 4, 1, 1, true) == 244, "player 1's portrait x with padding");
	expect(VersusScreen::paddedX(836, 100, 4, 1, 1, true) == 1036, "player 2's portrait x with padding");
	expect(VersusScreen::paddedX(444, -100, 4, 1, 1, false) == 444, "no padding: the first character at pos");

	// It runs [VS Screen] time (390) after its fade in (20), then fades out (25).
	VersusScreen::Fighter fighters[2];
	VersusScreen::StagePick stage;
	const bool humans[2] = { true, false };
	platform::Pad idle, press;
	press.a = true;
	versus.start(fighters, stage, 0);
	expect(versus.counter() == -20, "the counter starts at minus the fade in");
	versus.update(press, idle, humans);
	expect(versus.counter() == -19 && !versus.done(), "a button held over from the select screen doesn't skip");
	int ticks = 1;
	while (!versus.done() && ticks < 1000) {
		versus.update(idle, idle, humans);
		ticks++;
	}
	expect(ticks == 20 + 390 + 25, "time out: 20 fading in, 390 shown, then the 25 fading out");
	versus.start(fighters, stage, 1);
	versus.update(idle, idle, humans);
	versus.update(press, idle, humans);
	ticks = 2;
	while (!versus.done() && ticks < 1000) {
		versus.update(idle, idle, humans);
		ticks++;
	}
	expect(ticks == 2 + 25, "a done key fades it out at once");
	versus.start(fighters, stage, 1);
	versus.update(idle, idle, humans);
	versus.update(idle, press, humans);
	expect(versus.counter() == -18 && !versus.done(), "the computer's pad doesn't skip it");

	std::printf("8e VS screen: %d passed, %d failed\n", passed, failed);
	return failed;
}
