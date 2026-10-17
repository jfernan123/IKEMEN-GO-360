// Focused milestone 6e tests: the small combat rules are tested directly, while the scripted suite exercises them
// through KFM's real CNS/CMD files.

#include <cmath>
#include <cstdio>
#include <string>

#include "core/hitdef.h"
#include "core/game_config.h"
#include "core/coords.h"
#include "core/states.h"
#include "core/projectile.h"

using namespace m360;

namespace {
int passed, failed;

void expect(bool value, const char *name)
{
	if (value) passed++;
	else { failed++; std::printf("FAIL  6e: %s\n", name); }
}
}

int run6eTests()
{
	passed = failed = 0;

	HitDef legacy;
	legacy.attrState = 1;
	legacy.attrAttack = 1;
	legacy.guardVel[0] = -4;
	legacy.active = true;
	legacy.finalize(true);
	expect(std::fabs(legacy.groundCornerpushVeloff - -5.2f) < .001f, "legacy cornerpush derives from guard velocity");
	expect(legacy.airCornerpushVeloff == legacy.groundCornerpushVeloff, "air cornerpush inherits ground");
	expect(std::fabs(legacy.groundCornerpushVelmul - .7f) < .001f, "legacy cornerpush friction is 0.7");

	HitDef explicitPush;
	explicitPush.attrState = 1;
	explicitPush.attrAttack = 1;
	explicitPush.guardVel[0] = -4;
	explicitPush.groundCornerpushVeloff = -15;
	explicitPush.groundCornerpushVelmul = .5f;
	explicitPush.active = true;
	explicitPush.finalize(true);
	expect(explicitPush.groundCornerpushVeloff == -15, "explicit cornerpush offset wins");
	expect(explicitPush.guardCornerpushVeloff == -15, "guard cornerpush inherits explicit offset");
	expect(explicitPush.groundCornerpushVelmul == .5f, "explicit cornerpush friction wins");

	HitDef reversal;
	reversal.active = true;
	reversal.reversalAttrState = 1 | 4;
	reversal.reversalAttrAttack = (1 << 0) | (1 << 3) | (1 << 6);
	expect(reversal.reverses('S', 1 << 0), "reversal matches standing normal attack");
	expect(reversal.reverses('A', 1 << 3), "reversal matches aerial special attack");
	expect(!reversal.reverses('C', 1 << 0), "reversal rejects wrong state type");
	expect(!reversal.reverses('S', 1 << 1), "reversal rejects throw attribute");

	HitOverride overrideSlot;
	overrideSlot.time = 8;
	overrideSlot.stateno = 1310;
	overrideSlot.attrState = 1 | 4;
	overrideSlot.attrAttack = (1 << 2) | (1 << 5) | (1 << 8);
	expect(overrideSlot.matches('S', 1 << 2), "hit override matches projectile");
	expect(!overrideSlot.matches('C', 1 << 2), "hit override rejects wrong state type");
	overrideSlot.time = 0;
	expect(!overrideSlot.matches('S', 1 << 2), "expired hit override does not match");

	StateTable states;
	std::string error;
	expect(states.load("data/chars/kfm/kfm.cns", error), "KFM states load for 6e checks");
	int reversalControllers = 0, overrideControllers = 0;
	for (const auto &[number, def] : states.all()) {
		(void)number;
		for (const StateController &sc : def.ctrls) {
			reversalControllers += sc.type == CtrlType::ReversalDef;
			overrideControllers += sc.type == CtrlType::HitOverride;
		}
	}
	expect(reversalControllers == 6, "all six KFM ReversalDefs loaded");
	expect(overrideControllers == 4, "all four KFM HitOverrides loaded");

	std::printf("6e combat: %d passed, %d failed\n", passed, failed);
	return failed;
}

int run7Tests()
{
	passed = failed = 0;
	GameConfig config;
	std::string error;
	expect(config.load("data/mugen360.ini", error), "game configuration loads");
	expect(config.player1 == "data/chars/kfm/kfm.def", "game configuration selects P1");
	expect(config.player2 == "data/chars/kfm/kfm.def", "game configuration selects P2");
	expect(config.stage == "data/stages/kfm.def", "game configuration selects the stage");
	expect(std::fabs(coords::scaleForWidth(1280) - .25f) < .001f, "1280 localcoord scale is one quarter");
	expect(std::fabs(coords::toWorld(280, .25f) - 70) < .001f, "local position converts to stage position");
	expect(std::fabs(coords::toLocal(70, .25f) - 280) < .001f, "stage position converts to local position");
	expect(std::fabs(coords::between(9.6f, .25f, 1) - 2.4f) < .001f, "velocity converts between characters");

	Projectile projectile;
	projectile.x = 10;
	projectile.vx = 2;
	projectile.ax = 1;
	projectile.velmulX = .5f;
	projectile.removeTime = 2;
	projectile.advance();
	expect(std::fabs(projectile.x - 12) < .001f, "projectile advances by velocity");
	expect(std::fabs(projectile.vx - 1.5f) < .001f, "projectile applies acceleration and velocity multiplier");
	expect(projectile.removeTime == 1, "projectile removal timer advances");
	projectile.localScale = .25f;
	projectile.x = 1000;
	expect(projectile.outside(0, 160), "projectile edge bound uses local scale");
	projectile.hitPause = 2;
	const float pausedX = projectile.x;
	projectile.advance();
	expect(projectile.x == pausedX && projectile.hitPause == 1, "projectile hit pause freezes motion");
	std::printf("7a-7b content and projectiles: %d passed, %d failed\n", passed, failed);
	return failed;
}
