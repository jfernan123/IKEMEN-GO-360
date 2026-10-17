#include "core/hud.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/debug_text.h"
#include "platform/platform.h"

namespace m360 {

namespace {

constexpr float BAR_W = 520, BAR_H = 26, BAR_Y = 26, BAR_MARGIN = 40;
constexpr float POWER_W = 300, POWER_H = 12, POWER_Y = 660;
constexpr uint32_t FRAME = 0xFF101018, EMPTY = 0xC0302830, LIFE = 0xFFF0D030, LIFE_LOW = 0xFFF06030,
				   RED = 0xFFC02020, POWER = 0xFF3090F0, POWER_FULL = 0xFF60D0FF, TEXT = 0xFFFFFFFF, SHADOW = 0xC0000000;

float textWidth(const char *s, float scale)
{
	return std::strlen(s) * 6 * scale - scale;
}

// Big centred text with a drop shadow.
void banner(const char *s, float y, float scale, uint32_t argb)
{
	const float x = (platform::SCREEN_WIDTH - textWidth(s, scale)) / 2;
	drawText(s, x + scale, y + scale, scale, SHADOW);
	drawText(s, x, y, scale, argb);
}

// A bar filled to a share, from the inner end (by the clock) for player 1 or toward it for player 2: both drain
// toward the outside, like most fighting games.
void bar(float x, float y, float w, float h, float share, bool fromRight, uint32_t argb)
{
	share = std::clamp(share, 0.0f, 1.0f);
	const float fw = w * share;
	platform::fillRect(fromRight ? x + w - fw : x, y, fw, h, argb);
}

} // namespace

void FightHud::reset()
{
	redLife_[0] = redLife_[1] = -1;
	redWait_[0] = redWait_[1] = 0;
}

void FightHud::update(const Character &p1, const Character &p2)
{
	const Character *p[2] = { &p1, &p2 };
	for (int i = 0; i < 2; i++) {
		const float life = (float)p[i]->life;
		if (redLife_[i] < 0 || redLife_[i] < life) {
			redLife_[i] = life;
			continue;
		}
		// While being hit (and still in the fight) the trail waits; then it drains to the life left.
		if (p[i]->moveType == 'H' && p[i]->alive()) {
			redWait_[i] = 30;
		} else if (redWait_[i] > 0) {
			redWait_[i]--;
		} else if (redLife_[i] > life) {
			redLife_[i] = std::max(life, redLife_[i] - p[i]->lifeMax() / 120.0f);
		}
	}
}

void FightHud::draw(const Match &match, const Character &p1, const Character &p2) const
{
	const Character *p[2] = { &p1, &p2 };
	if (!p1.asserts(Special::NoBarDisplay) && !p2.asserts(Special::NoBarDisplay)) {
		for (int i = 0; i < 2; i++) {
			const Character &c = *p[i];
			// Player 1's bar on the left, filled from the clock outward; player 2's mirrored.
			const bool right = i == 1;
			const float x = right ? platform::SCREEN_WIDTH - BAR_MARGIN - BAR_W : BAR_MARGIN;
			const float max = (float)c.lifeMax();
			platform::fillRect(x - 3, BAR_Y - 3, BAR_W + 6, BAR_H + 6, FRAME);
			platform::fillRect(x, BAR_Y, BAR_W, BAR_H, EMPTY);
			bar(x, BAR_Y, BAR_W, BAR_H, (redLife_[i] < 0 ? c.life : redLife_[i]) / max, !right, RED);
			bar(x, BAR_Y, BAR_W, BAR_H, c.life / max, !right, c.life * 4 < max ? LIFE_LOW : LIFE);
			// Name under the bar, and a marker per round won.
			const float nameY = BAR_Y + BAR_H + 8;
			if (right)
				drawText(c.name.c_str(), x + BAR_W - textWidth(c.name.c_str(), 2), nameY, 2, TEXT);
			else
				drawText(c.name.c_str(), x, nameY, 2, TEXT);
			for (int w = 0; w < match.wins(i); w++) {
				// Under the bar's inner end, by the clock.
				const float mx = right ? x + w * 22 : x + BAR_W - 14 - w * 22;
				platform::fillRect(mx - 2, nameY - 2, 18, 18, FRAME);
				platform::fillRect(mx, nameY, 14, 14, 0xFFF0C030);
			}
			// Power gauge at the bottom: the level (each 1000) and the progress to the next.
			const float px = right ? platform::SCREEN_WIDTH - BAR_MARGIN - POWER_W : BAR_MARGIN;
			const int32_t level = c.power / 1000, maxLevel = c.powerMax() / 1000;
			const bool full = c.power >= c.powerMax();
			platform::fillRect(px - 2, POWER_Y - 2, POWER_W + 4, POWER_H + 4, FRAME);
			platform::fillRect(px, POWER_Y, POWER_W, POWER_H, EMPTY);
			bar(px, POWER_Y, POWER_W, POWER_H, full ? 1 : (c.power % 1000) / 1000.0f, right, full ? POWER_FULL : POWER);
			char lv[16];
			std::snprintf(lv, sizeof lv, "%d", (int)std::min(level, maxLevel));
			const float lx = right ? px - 40 : px + POWER_W + 14;
			drawText(lv, lx + 3, POWER_Y - 9 + 3, 4, SHADOW);
			drawText(lv, lx, POWER_Y - 9, 4, level > 0 ? POWER_FULL : TEXT);
		}
		// The clock between the bars, in seconds.
		char t[16];
		std::snprintf(t, sizeof t, "%02d", (int)((match.timeLeft() + 59) / 60));
		banner(t, BAR_Y - 6, 6, match.timeLeft() <= 10 * 60 ? 0xFFFF6040 : TEXT);
	}

	// Announcements.
	const int32_t a = match.announceTime();
	if (match.roundState() == 1 && a >= 0) {
		char r[32];
		std::snprintf(r, sizeof r, "ROUND %d", (int)match.roundNo());
		if (a < 60)
			banner(r, 280, 10, TEXT);
		else
			banner("FIGHT!", 280, 12, 0xFFFFD040);
	}
	const int32_t over = match.overTime();
	if (over >= 0 && match.finish() != Match::Finish::None) {
		const char *what = match.finish() == Match::Finish::KO ? "K.O."
			: match.finish() == Match::Finish::DoubleKO      ? "DOUBLE K.O."
															  : "TIME OVER";
		if (over < Match::OVER_WAIT + Match::OVER_WIN) {
			banner(what, 280, 12, 0xFFFF4030);
		} else {
			char who[64];
			if (match.winner() < 0)
				std::snprintf(who, sizeof who, "DRAW GAME");
			else
				std::snprintf(who, sizeof who, "%s WINS", p[match.winner()]->name.c_str());
			banner(who, 280, 7, TEXT);
			if (match.matchOver() && match.winner() >= 0)
				banner(match.winner() == 0 ? "PLAYER 1 TAKES THE MATCH" : "PLAYER 2 TAKES THE MATCH", 350, 4, 0xFFFFD040);
		}
	}
}

} // namespace m360
