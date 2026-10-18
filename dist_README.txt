MUGEN360 - milestone 8b: fight screen (homebrew test build)
===========================================================

The fight now looks like Ikemen: its default skin's lifebars, power bars,
portraits, names, clock, combo counter, win icons, and the "Round 1",
"Fight!", "K.O." and "Winner!" calls with their sounds.


The game opens on a character select screen: pick Kung Fu Man or the
high-resolution Kung Fu Man 720 (the attack button you pick with chooses his
colours), the computer picks for player 2 unless controller 2 is plugged in,
then the stage, then the fight. After the match it goes back to select.

Each Kung Fu Man is run by his own MUGEN state
files (kfm.cns, kfm.cmd, common1.cns). A real match: intros, "Round 1",
"Fight!", lifebars, power gauges, a 99 second clock, KO and time over, win
poses, best of three rounds.
Hold back to block (down-back for low attacks). Sweeps and specials knock
down, and the throw (forward + Y up close) works. Power fills as you attack
and get hit; supers need a full level (1000).
Hits and blocks draw the shared MUGEN fight sparks. KFM's intro wood pieces,
landing dust and shared wall effects now animate and move as Explods.
Powered moves have coloured afterimages and palette flashes. Screen shakes,
pauses, super freezes and the shared super flash work. Character voices,
attack sounds, shared hit/guard sounds and landing sounds play through the
PC or Xbox 360 audio backend.
Kung Fu Blocking (forward + light punch) now parries attacks. Hits at the
edge of the screen apply MUGEN corner push, and air-combo juggle limits are
enforced.
The numbers at the top show each player's state, like Ikemen's debug view;
red boxes are attacks, blue boxes are where a player can be hit.
LB+Start cycles: stage -> animation player -> sprite viewer.
Needs a softmodded/RGH console (Bad Update + XeUnshackle + Aurora works).
The roster and stages are listed in data/mugen360.ini ([Characters] and
[Stages], like MUGEN's select.def). Add other characters' folders under
data/chars and list them there; old WinMUGEN characters (SFF v1 sprites,
.act palettes) load too. Not every MUGEN character will work yet.
Characters with non-320 localcoord values are scaled into the stage space;
the 1280x720 Kung Fu Man reference is covered by the compatibility tests.
Characters can create persistent projectiles with their own animation, motion,
hits, guards, contact triggers, removal effects and projectile priority trades.

INSTALL
1. Copy the whole "mugen360" folder to the hard drive, e.g.  Hdd1:\Homebrew\mugen360\
   (or run it from a USB stick, which makes grabbing the log easier).
   Keep the "data" folder next to default.xex.
   Do NOT put it in Content\0000000000000000.
2. Launch it from Aurora's file manager: ...\mugen360\default.xex

CONTROLS (select screen)
  D-pad              move the cursor; on the stage screen, change stage
  A B RT X Y RB      pick the character with colours 1-6; confirm the stage

CONTROLS (stage)
  D-pad              walk, jump, crouch (controller 2 plays player 2)
  X  Y  RB           punches (light, medium, strong)
  A  B  RT           kicks (light, medium, strong)
  LB                 hide / show the info text
  LT                 start a new match
  Start              taunt / character Start button
  LB + Start         next screen (animations, then sprites)
  Back               quit

CONTROLS (animation player)
  D-pad left/right   previous / next action      LB / RB   10 at a time
  D-pad up/down      palette                     A         pause / play
  B                  one frame while paused      X         turn around
  Y                  collision boxes on / off

IF IT DOESN'T WORK
  The game writes default.log next to default.xex. Send that file back,
  plus a photo of the screen.
