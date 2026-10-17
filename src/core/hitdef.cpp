#include "core/hitdef.h"

namespace m360 {

void HitDef::finalize(bool mugen10)
{
	if (attrAttack == 0 && reversalAttrAttack == 0)
		active = false;
	// Throws hit once by default.
	if (hitonce < 0)
		hitonce = isThrow() ? 1 : 0;

	auto orInt = [](int32_t &dst, int32_t src) {
		if (dst == NOT_SET)
			dst = src;
	};
	auto orFloat = [](float &dst, float src) {
		if (!isSet(dst))
			dst = src;
	};
	orInt(guardPausetime[0], pausetime[0]);
	orInt(guardPausetime[1], pausetime[1]);
	// MUGEN does this differently from its documentation; Ikemen characters follow the documentation.
	orInt(guardHittime, mugen10 ? groundSlidetime : groundHittime);
	orInt(guardSlidetime, guardHittime);
	orInt(guardCtrltime, guardSlidetime);
	orInt(airguardCtrltime, guardCtrltime);

	orFloat(guardVel[0], groundVel[0]);
	orFloat(airguardVel[0], airVel[0] * 1.5f);
	orFloat(airguardVel[1], airVel[1] * 0.5f);
	orFloat(downVel[0], airVel[0]);
	orFloat(downVel[1], airVel[1]);

	// MUGEN characters use its legacy corner-push defaults. Ikemen characters instead inherit ground.velocity and
	// target friction, but this port currently loads MUGEN characters.
	if (!isSet(groundCornerpushVeloff))
		groundCornerpushVeloff = (attrState & 4) ? 0 : guardVel[0] * 1.3f;
	orFloat(airCornerpushVeloff, groundCornerpushVeloff);
	orFloat(downCornerpushVeloff, groundCornerpushVeloff);
	orFloat(guardCornerpushVeloff, groundCornerpushVeloff);
	orFloat(airguardCornerpushVeloff, groundCornerpushVeloff);
	orFloat(groundCornerpushVelmul, 0.7f);
	orFloat(airCornerpushVelmul, groundCornerpushVelmul);
	orFloat(downCornerpushVelmul, groundCornerpushVelmul);
	orFloat(guardCornerpushVelmul, groundCornerpushVelmul);
	orFloat(airguardCornerpushVelmul, groundCornerpushVelmul);

	if (airAnimtype == Reaction::Unknown)
		airAnimtype = animtype;
	if (fallAnimtype == Reaction::Unknown)
		fallAnimtype = airAnimtype >= Reaction::Up ? airAnimtype : Reaction::Back;
	if (airType == HitType::Unknown)
		airType = groundType;
	// Knocking upward stands a crouching opponent up.
	orInt(forcestand, groundVel[1] != 0 ? 1 : 0);
	orInt(forcecrouch, 0);
	orInt(airFall, groundFall ? 1 : 0);

	// Power from damage (MUGEN's default.attack.lifetopowermul .7, gethit .6; supers give the attacker none), half on
	// guard.
	orInt(hitGetPower, (int32_t)((isHyper() ? 0.0f : 0.7f) * hitDamage));
	orInt(hitGivePower, (int32_t)(0.6f * hitDamage));
	orInt(guardGetPower, (int32_t)(hitGetPower * 0.5f));
	orInt(guardGivePower, (int32_t)(hitGivePower * 0.5f));

	// A throw leaves both untouchable through its pause, so nothing interrupts the grab.
	if (unhittabletime[0] == NOT_SET || unhittabletime[1] == NOT_SET) {
		if (isThrow()) {
			unhittabletime[0] = pausetime[0] + 1;
			unhittabletime[1] = pausetime[0] + 1;
		}
		orInt(unhittabletime[0], -1);
		orInt(unhittabletime[1], -1);
	}
}

void GetHitVars::newHit()
{
	GetHitVars kept = *this;
	*this = GetHitVars{};
	damage = kept.damage;
	downRecovertime = kept.downRecovertime;
	fallcount = kept.fallcount;
	fallflag = kept.fallflag;
	guardcount = kept.guardcount;
	hitcount = kept.hitcount;
	kill = kept.kill;
	xoff = kept.xoff;
	yoff = kept.yoff;
}

} // namespace m360
