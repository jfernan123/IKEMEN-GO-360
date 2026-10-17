MUGEN360 - milestone 6e: KFM combat compatibility (homebrew test build)
======================================================================

Kung Fu Man against Kung Fu Man on his stage, run by his own MUGEN state
files (kfm.cns, kfm.cmd, common1.cns). A real match now: intros, "Round 1",
"Fight!", lifebars, power gauges, a 99 second clock, KO and time over, win
poses, best of three rounds, then a new match starts.
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

INSTALL
1. Copy the whole "mugen360" folder to the hard drive, e.g.  Hdd1:\Homebrew\mugen360\
   (or run it from a USB stick, which makes grabbing the log easier).
   Keep the "data" folder next to default.xex.
   Do NOT put it in Content\0000000000000000.
2. Launch it from Aurora's file manager: ...\mugen360\default.xex

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
