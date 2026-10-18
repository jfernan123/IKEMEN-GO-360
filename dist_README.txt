MUGEN360 - milestone 10d: Survival (homebrew test build)
========================================================

Which build is this? During a fight the version shows faintly in the
bottom-right corner, like V2026.10.18-1701 7F39FDD. The V part matches the
GitHub release (xbox-2026.10.18-1701); DEV means a build made by hand.

New: SURVIVAL, under MISSION on the title menu. Pick a fighter and a stage,
then fight one-round matches against the roster for as long as you can last.
Your life and power carry over between matches; the first loss ends the run.

New: TEAM VERSUS and TEAM ARCADE use two-character Turns teams. Pick two
fighters and colours for each side. A bout is one round; when a fighter loses,
that side's next member enters. The last team with a fighter wins the series.
With two controllers, both sides choose at the same time.
The winner carries life and power forward and recovers some life based on the
time left, following Ikemen's default Turns recovery rule.

New: Arcade is a full route through the installed roster. Pick your fighter
once; each win advances to the next CPU opponent and a new random stage.

New: Training keeps the timer at 99, refills life and power, and uses a
stationary player-2 dummy. Start Training from PRACTICE on the title menu.

New: stage and menu backgrounds honor their clipping windows and scaling.
Parallax floors widen toward the screen as real trapezoids, and stage BG
controllers can animate, show, hide, enable, move and oscillate elements.

New: SuperPause darkens the stage while keeping the fighters and lifebars
bright. Character BGPalFX / AllPalFX controllers and stage BGCtrl PalFX now
change stage colours with add, multiply and sine effects.

New: the camera works like Ikemen's: it glides after the fighters instead of
snapping, and Training Stage Z and the Interactive Training Stage zoom out as
the fighters move apart.

Five more stages from Ikemen are on the stage screen (Training Room, Training
Room 720, Training Stage, Training Stage Z, Interactive Training Stage). The
720 one is widescreen and fills the whole screen.

New: Ikemen's VS screen between select and the fight (both fighters in their
colours, names, the next stage and its picture, "Match 1" in Arcade). It
lasts about 7 seconds; any attack button or Start skips it.
New: music. The game plays MP3, OGG or WAV songs, but none come with it (the
Ikemen release has none). To add some, put them at:
  data/ikemen1/sound/Title.mp3     title menu (loops)
  data/ikemen1/sound/Select.mp3    select screen (loops)
  data/ikemen1/sound/Versus.mp3    VS screen (plays once)
  and for the fight, set "bgmusic =" under [Music] in data/stages/kfm.def to
  an .mp3 or .ogg next to it (its kfm.mid is MIDI, which isn't supported).
Also: less disk loading and texture stutter (it ran slow, worst from the
hard drive), and a perf line in default.log every 10 seconds.

The game opens on Ikemen's title screen and main menu. VS MODE has 1P VS 2P,
TEAM VERSUS and QUICK MATCH. ARCADE has SINGLE MODE and TEAM ARCADE. PRACTICE
starts Training with a refilling dummy. OPTIONS isn't there yet. EXIT quits.

The fight looks like Ikemen: its default skin's lifebars, power bars,
portraits, names, clock, combo counter, win icons, and the "Round 1",
"Fight!", "K.O." and "Winner!" calls with their sounds.

From the menu you get the character select screen: pick Kung Fu Man or the
high-resolution Kung Fu Man 720, then his colour in the Color menu; you pick
for the computer too unless controller 2 is plugged in (Arcade picks a random
opponent); then the stage, the VS screen, and the fight. After the match it
goes back to select.

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

CONTROLS (title menu)
  D-pad              move
  A B RT X Y RB Start  choose
  Back               back out of a submenu; on the main menu, jump to EXIT

CONTROLS (select screen)
  D-pad              move the cursor; on the stage screen, change stage
  A B RT X Y RB      pick the character, then confirm its colour; confirm the stage
  D-pad (Color menu) next / previous colour;  Start: random colour
  Back               out of the Color menu; else back to the title menu
  Against the computer you pick its character after yours.
  In a TEAM mode, repeat the character and colour pick for member 2.

CONTROLS (VS screen)
  A B RT X Y RB Start  skip to the fight

CONTROLS (stage)
  D-pad              walk, jump, crouch (controller 2 plays player 2)
  X  Y  RB           punches (light, medium, strong)
  A  B  RT           kicks (light, medium, strong)
  LB                 hide / show the info text
  LT                 start a new match
  Start              taunt / character Start button
  LB + Start         next screen (animations, then sprites)
  Back               leave the fight for the select screen

CONTROLS (animation player)
  D-pad left/right   previous / next action      LB / RB   10 at a time
  D-pad up/down      palette                     A         pause / play
  B                  one frame while paused      X         turn around
  Y                  collision boxes on / off

IF IT DOESN'T WORK
  The game writes default.log next to default.xex. Send that file back,
  plus a photo of the screen.
