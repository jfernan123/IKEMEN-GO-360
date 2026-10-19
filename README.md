# MUGEN360

MUGEN360 is a standalone, MUGEN-compatible fighting-game engine for Xbox 360, rewritten in C++ using
[Ikemen-GO](https://github.com/ikemen-engine/Ikemen-GO) as the behavior reference. It also has a Windows build for
fast debugging and automated tests. The Xbox 360 port uses the open-source
[xsdk](https://github.com/xenosworks/xsdk) toolchain.

Kung Fu Man versus Kung Fu Man is the current launcher configuration and compatibility target. Character behavior
comes from normal MUGEN data files rather than hardcoded moves, so the engine can support other characters and games
as compatibility grows.

## Current status

Milestones 1 to 10 are complete. KFM can fight full matches with commands, attacks, guarding, throws, falls, life and
rounds, a CPU opponent, hit effects, Explods, helpers, projectiles, palette effects, pauses, afterimages,
environmental shake, and sound. The game runs through Ikemen's default screenpack (ikemen1): title menu, select
screen with the Color menu, VS screen, Ikemen's fight screen, and music for each when songs are added. The modes
are Arcade, Versus, Quick Match, Training, Survival and Watch, and the team modes Simul (2v2 on screen) and Turns
through Ikemen's team menu. Both the
Windows and Xbox 360 builds pass the current test and build checks.

The launch content is no longer compiled into the engine. Edit `data/mugen360.ini` to choose P1, P2 and the stage;
the PC build also accepts `--p1`, `--p2` and `--stage` path overrides. This is the first part of making the executable
usable for games other than the included KFM test setup.

## Build and run

| Target | Build | Run |
|---|---|---|
| Windows debug build | `build_pc.bat` -> `build\pc\mugen360.exe` | `run_pc.bat` |
| Xbox 360 | `build_360.bat` -> `dist\mugen360\` | `run_xenia.bat`, or copy the folder to a console |

The Windows build needs Visual Studio 2022 Build Tools, CMake, Ninja, and SDL3 at `..\tools\sdl3`. The Xbox 360
build needs WSL Debian with xsdk at `~/xsdk` and its GCC toolchain at `~/xsdk-gcc`. The build creates
`dist\mugen360\default.xex` and `dist\mugen360_360.zip`.

## Controls

| PC keyboard | Action |
|---|---|
| Arrows or WASD | Move |
| U / I / O or C / V / E | Punches x / y / z |
| J / K / L or Z / X | Kicks a / b / c |
| Q | Toggle debug information and hitboxes |
| R | Start a new match |
| Enter | Taunt / character Start button |
| Tab | Change debug screen |
| Esc | Back: fight to select, select to title; on the title, to EXIT |

On a controller, use X/Y/RB for punches, A/B/RT for kicks, LB for debug information, LT for a new match, Start for
the character's Start button or taunt, LB+Start to change screens, and Back as Esc. In the menus, the attack buttons
choose; on the select screen the Color menu takes left / right and Start for a random colour; on the VS screen an
attack button or Start skips to the fight.

## Tests

Build the Windows version, then run:

```bat
build\pc\mugen360_tests.exe
tests\run_all.bat
```

The suite includes unit coverage for expressions, state loading, effects, sound decoding and mixing, reversals,
HitOverride, corner-push defaults, camera behavior, background controllers and palette effects. Hidden scripted matches cover commands, attacks, guarding, throws, falls,
juggling, rounds, time over, SuperPause, AfterImage, Kung Fu Blocking, corner push and taunting, and the title,
select and VS screens. Unit tests also cover the screenpack pieces and music decoding. Scripted runs never send
keyboard or mouse input to the desktop.

## Project layout

- `src/core/` contains platform-independent engine code translated from Ikemen-GO.
- `src/platform/platform.h` defines the platform boundary.
- `src/platform/pc/` implements Windows video, input, and audio with SDL3.
- `src/platform/x360/` implements the same interface with xsdk.
- `data/` contains the current KFM character, stage, common states, effects, and sounds.
- `data/mugen360.ini` chooses the two character definitions and stage loaded at startup.
- `tests/` contains unit tests and scripted fight regressions.
- `data/ikemen1/` is Ikemen's default screenpack (system.def), `data/fight.def` its fight screen; songs go in
  `data/ikemen1/sound/` (none ship with Ikemen).
- `third_party/` contains lodepng, dr_mp3 and stb_vorbis.

MUGEN files are little-endian while Xbox 360 is big-endian. Binary data is read through `core/bytes.h`, and runtime
paths use forward slashes.

## Milestones

1. **Sprite viewer** (done) - SFF v2 sprites (LZ5, RLE8, RLE5, PNG) and palettes, on PC and in Xenia.
2. **Animation player** (done) - AIR actions with Ikemen's frame timing, loops, flips, offsets, offset/scale
   interpolation and collision boxes. Not yet drawn: additive/subtractive blending and rotation.
3. **Stage and camera** (done) - stage .def: normal, parallax and animated backgrounds, tiling, masking, add/addalpha/sub
   blending, velocity, position links; 4:3 stages fitted to 16:9 with cuthigh/cutlow; MUGEN's camera (tension, bounds,
   vertical follow) and screen edges; shadows. Milestone 9 later added zoom, BG controllers, windows, sine motion,
   reflections and parallax `width` trapezoids.
4. **Hand-written movement** (done) - walking, neutral / forward / back jumps (direction fixed at takeoff), landing,
   crouching, with the speeds, gravity and body sizes from the character's CNS and its real animations; players push
   each other apart, jump over each other, and stop at the screen's edges and the stage's bounds. Stands in for the
   state machine until milestone 5.
5. **CNS/CMD state machine** (done)
   - 5a commands (done): input buffer and CMD commands, Ikemen's matching rules.
   - 5b expressions (done): the trigger language; `mugen360_tests.exe` checks it and parses every trigger.
   - 5c state loading (done): statedefs and controllers from st, cmd and stcommon files, first definition wins.
   - 5d state machine (done): Ikemen's frame order (built-in moves, states -3/-2/-1, current state, physics,
     landing), ChangeState/SelfState, ChangeAnim, Vel*/Pos*, CtrlSet, Var*, Turn, StateTypeSet, PosFreeze,
     SprPriority, Width, ScreenBound, PlayerPush. The characters move by their own files; `--trace` logs every
     state change.
   - 5e attacks (done): all 12 normals, dashes, specials and the throw start and finish in the right states, each
     lasting exactly its animation (`tests/inputs/attacks.txt`, `tests/inputs/specials.txt` with `--trace`).
6. **Fighting**
   - 6a hits (done): HitDef with Ikemen's defaults; attack boxes (Clsn1) against hurt boxes (Clsn2), hitflags,
     priority trades; hitpause for the attacker, hitshake, get-hit values (GetHitVar, HitVelSet), damage; the get-hit
     states 5000/5010/5020 and getting up from lying down; MoveContact/MoveHit/HitOver/HitShakeOver. Boxes shown with
     the info text (`tests/inputs/hits.txt` with `--trace` logs each hit).
   - 6b getting hit (done): guarding (holding back near an attack: 120-155, guardflag, guard damage and pause);
     NotHitBy/HitBy; falls (HitFallVel/Set/Damage, CanRecover, fall defence, lying down and getting up); throws
     (targets, TargetBind/State/LifeAdd/Facing/Vel*, custom states: the thrown player runs the attacker's states and
     ChangeAnim2 plays the attacker's actions); juggle points; AssertSpecial (walk, turn, guard, visibility flags);
     DefenceMulSet; players push only where hurt boxes touch. `tests/inputs/guard.txt` (with `--input2
     tests/inputs/guard_p2.txt` for player 2), `falls.txt` and `specials.txt` (the throw).
   - 6c life and rounds (done): the round in Ikemen's order and timings (`core/match`): state 5900, intros
     (AssertSpecial Intro holds the round), "Round N" / "Fight", the 99-second clock, KO (5030, KO slowdown), double
     KO and time over (decided on life), win / lose / draw poses (180 / 170 / 175), best of three, then a new match;
     RoundState, RoundNo, RoundsExisted, Win, Lose, DrawGame, MatchOver, Alive. Power from hits (getpower /
     givepower and MUGEN's defaults). A plain fight screen (`core/hud`): lifebars with a red damage trail, power
     gauges with levels, the clock, round wins, announcements. `tests/inputs/match.txt` plays a whole match,
     `timeover.txt` a time over; the other scripts take `--quickstart` to skip the intros.
   - 6d effects and sound (done for KFM)
     1. Shared `fightfx.sff` / `fightfx.air`, hit sparks and guard sparks (done).
     2. Explod and shared `F` animations (done): character or fightfx animations, screen/player position types,
        facing, velocity, acceleration, scale, removal time and draw priority. KFM's intro wood and landing effects
        now play.
     3. MakeDust and EnvShake / FallEnvShake (done). KFM has no Projectile controller; the word "projectile" in
        his AIR only labels the intro wood animation, which is an Explod. Projectile remains a later general-character
        compatibility feature.
     4. AfterImage / AfterImageTime, PalFX and RemapPal (done): KFM's powered moves leave additive coloured trails
        and flash with their palette effects.
     5. Pause and SuperPause (done): character and round timing freeze, the shared F100 flash appears, and KFM's
        background-freeze setting is respected.
     6. PlaySnd plus HitDef hit and guard sounds (done): Elecbyte SND and embedded PCM WAV parsing, channel mixing,
        SDL3 audio on PC, and xsdk's homebrew XAudio render callback on Xbox 360.
     The focused 6d unit tests cover SND/WAV decoding, resampling, mixing, fightfx loading, Explod lifetime,
     EnvShake and pause timing. `tests/inputs/effects.txt` covers KFM's SuperPause and AfterImage in a hidden run;
     `tests/run_all.bat` runs the unit tests and every scripted fight regression.
   - 6e KFM combat compatibility (done): ReversalDef and HitOverride make Kung Fu Blocking intercept matching attacks;
     MUGEN's ground, air, down and guard corner-push offsets and friction run when a target is pinned to a screen edge;
     AssertSpecial `nocornerpush` is supported. Focused unit tests cover controller loading, attribute matching,
     override expiry and corner-push defaults. Hidden fight regressions exercise a real parry, visible corner push,
     juggle-point spending, and the state 195 taunt. Enter/Start is once again available as MUGEN's
     `s` button; Tab or LB+Start changes the debug screen.
7. **General game compatibility**
   - 7a configurable content and local coordinates (done): P1, P2 and the stage are selected by
     `data/mugen360.ini`, with optional PC command-line overrides. Character positions, velocities, collision boxes,
     widths, effects and rendering convert through Ikemen's `localscl`; the official 1280x720 KFM variant loads from
     the reference release, draws at the same world size, and passes the normal-attack and real-hit regressions.
   - 7b projectiles (done): Projectile controllers create independent character or shared animations with localcoord-aware
     position, velocity, acceleration and scale. Projectiles hit and guard through regular HitDef rules, pause on contact,
     use hit/remove/cancel animations, expire at configured bounds, trade by priority, and expose NumProj plus the
     ProjHit/Guarded/ContactTime triggers. Focused fixtures test hits after the creator leaves its state and symmetric
     projectile cancellation.
   - 7c helpers (done): Helper creates a child that shares its root's loaded files (sprites, animations, states,
     sounds) and runs its own state machine; DestroySelf; BindToParent / BindToRoot; ParentVarSet/Add and
     RootVarSet/Add; parent / root / helper(id) redirects; IsHelper, NumHelper(id), ParentDist, RootDist; keyctrl
     helpers read the root's commands and run states -1/-2/-3. Helpers hit through their own HitDefs; they can't be
     hit yet. `tests/fixtures/helper` spawns nested helpers that write through every redirect.
   - 7d broader state compatibility (done): LifeAdd, LifeSet, PowerAdd, PowerSet, AttackMulSet, AttackDist, Gravity,
     Offset, Trans, AngleDraw / AngleSet / AngleAdd / AngleMul (sprites rotate about the character), VarRandom,
     MoveHitReset, HitAdd, BindToTarget, EnvColor, GameMakeAnim, AllPalFX (fighters, effects and stage),
     Explod ids, bindtime, removeongethit, ontop, vfacing and trans, with ModifyExplod, RemoveExplod and
     ExplodBindTime; ForceFeedback, VictoryQuote, SndPan and the clipboard controllers are accepted. Triggers: Name,
     P1Name, P2Name, AuthorName, TeamMode, P2Life, P2StateNo, P2Ctrl, HitPauseTime, ID, PlayerIDExist, MatchNo,
     TicksPerSecond, IsHomeTeam, the screen edges and sizes, NumExplod / NumHelper / NumTarget / IsHelper with an
     optional id, NumProjID, and the old ProjHit/ProjContact/ProjGuarded[id] = value[, op time] forms.
     `tests/fixtures/misc` runs them all and only reaches its last state if every one reads back correctly.
   - 7e legacy characters (done): SFF v1 (WinMUGEN) sprite files: the linked list of sprite headers, 8-bit RLE PCX
     images, shared ("same as previous") palettes, the character's first-sprite palette and linked sprites; .act
     palette files (pal1 .. pal12 in the .def) become palettes 1,n, and in an SFF v1 the chosen one colours the
     sprites sharing palette 0, as MUGEN does. WinMUGEN characters (no `mugenversion = 1.x`) take the WinMUGEN
     hitpause and guard-time rules already in the hit code. `tests/legacy_test.cpp` builds an SFF v1 byte by byte and
     reads it back. No WinMUGEN character ships with the package, so this is tested on that synthetic file only.
   - 7f game flow (done): `data/mugen360.ini` takes a `[Characters]` roster and `[Stages]` list in MUGEN select.def
     style. With a roster the game opens on a select screen: a grid of small portraits (9000,0), each player's big
     portrait (9000,1, scaled by localcoord, in their chosen colours) and name; a, b, c, x, y, z pick palettes 1-6;
     player 2 picks with a second controller or the computer picks; then the stage, a VS screen, the fight, and back to
     the select screen after the match. Without a roster (or in test runs, unless `--select`) the [Game] fight starts
     at once. KFM720 joins the roster. `tests/inputs/select.txt` plays through it.

8. Screenpack, menus and music (done): the fight screen, title menu, select and VS screens from a MUGEN /
   Ikemen screenpack, Ikemen's default "ikemen1" skin (`data/fight.def`, `data/ikemen1/system.def`).
   - 8a foundations (done): MUGEN bitmap fonts (`core/font`: FNT v1 .fnt files and FNT v2 .def + .sff, colour banks,
     alignment; TrueType fonts are refused), backgrounds as a reusable `BgDef` read by section prefix ("BG" for
     stages, "TitleBG" / "SelectBG" / "VersusBG" for screenpacks), and the screenpack layout keys (`core/layout`:
     .spr / .anim / .font / .text with .offset, .facing, .scale, .layerno). Sound files are kept as they are and each
     sound decodes the first time it plays, so the 6 MB fight.snd doesn't become tens of MB. The ikemen1 screenpack
     files and the MUGEN fonts are in `data/`. `tests/screenpack_test.cpp` loads them.
   - 8b fight screen (done): `core/fight_screen` draws Ikemen's fight screen from `fight.def` (`[Game] fight` in
     `data/mugen360.ini`): slanted lifebars whose red part waits out the combo then drains, power bars with the level
     number and level-up sounds, portraits (9000,0 in the character's palette), names, the clock, win icons, the combo
     counter sliding in with hits / damage / percent, and the round calls ("Round N", "Fight", KO, double KO, time
     over, the winner) with their animations and fight.snd sounds, plus the round's fade in and out. Layouts gained
     xshear, palfx (add, mul, sinadd, sinmul; rounded to steps of 8 so pulsing makes few textures) and clipped bar
     fills (`platform::setClip`). The round's pacing now comes from `[Round]` (ikemen1: control 50 ticks after
     "Fight", 300 ticks of ending) and waits for the calls as Ikemen's does. Without a fight.def the plain HUD and
     default timings remain. Not yet: team modes, guard / stun bars, red life, [Action] messages, perfect / clutch
     wins, the vs-CPU win and lose calls.
   - 8c title screen (done): `core/motif` loads a screenpack's system.def (`[Game] motif` in `data/mugen360.ini`), and
     `core/title_screen` draws Ikemen's title from `[Title Info]` and TitleBG: the background (with sin.x / sin.y
     motion, now in `BgDef`), the menu built from the `menu.itemname.` keys in file order (`IniSection::keys`), its
     scrolling window and tween, active-item font, move / done / cancel sounds, fades and the footer; fonts can be
     named by file. Modes that can't run yet are left out of the menu; a submenu left with one mode runs it directly
     (Ikemen's main.lua). ikemen1 gives ARCADE, VS MODE (1P VS 2P, QUICK MATCH), PRACTICE, OPTIONS, EXIT; arcade,
     quick match and training are a versus match against the computer until stages 10 and 11, options shows a notice.
     Back leaves a fight for the select screen and the select screen for the title. `--title` runs it in a test
     (`tests/inputs/title.txt`). Not yet: the logo storyboard, demo and attract modes, the info box, music.
   - 8d select screen (done): `core/select_screen` (`MotifSelect`) is Ikemen's select screen from `[Select Info]` and
     SelectBG (start.lua): the grid with ikemen1's per-row overrides (spacing, offset, scale, 3D tilt), each side's
     eased cursor, the big portraits (the character's stance animation and 9000,1 turned in perspective, then its
     181 "done" animation), names, the mode's title, the Color palette menu (next / previous / "?" random), the stage
     line with its blinking font, fades. A `randomselect` roster entry is the "?" cell. Against the computer, player 1
     picks player 2's character too (Ikemen's rule); Arcade picks a random opponent. The renderer gained 3D turning
     (xangle / yangle, perspective from a focal length, drawn in strips) and layouts their `.window` clip. The plain
     screen stays as the fallback. Memory for the 360: SFF v2 sprites now stay packed and decode only while a texture
     is made (ikemen1's system.sff was 426 MB decoded, more than the console has), and all textures together stay
     under a 224 MB budget, the least recently drawn freed first (the 360 frees them a few frames later, once no draw
     in flight uses them). Not yet: team modes, the select timer.
   - 8e VS screen (done): `core/versus_screen` (`VersusScreen`) is Ikemen's VS screen from `[VS Screen]` and
     VersusBG (start.lua's f_selectVersus): both characters' portraits (ikemen1: their stance animation in the chosen
     palette, centred by `padding` and clipped to their windows), the names, the stage's portrait in its frame and
     "Next Stage: ...", Arcade's "Match 1", between VersusBG's layers, with its fades. It lasts `time` (390 ticks in
     ikemen1) after fading in; an attack button or Start cuts it short (a computer's pad doesn't). The select screen
     hands it the picks after its own fade out, replacing the plain VS text. Not yet: team order select, the timer,
     background loading, face2.
   - 8f music (done): `core/music` is Ikemen's Bgm: one song at a time, MP3 (dr_mp3), Ogg Vorbis (stb_vorbis) or WAV,
     decoded about 85 ms at a time into a one-second queue each platform mixes under the sounds
     (`platform::queueMusic`), so a song never sits decoded in memory. It's resampled to 48 kHz, played at Ikemen's
     music volume (2 ^ (-5 + 75 * 0.06 * volume / 100)) and loops between its loop points. The screenpack's `[Music]`
     plays on the title and select screens (replacing what plays; none means silence) and on the VS screen (only if
     it names one), and the stage's `[Music]` bgmusic starts over with each match; files are found as Ikemen's
     SearchFile finds them. The Ikemen release ships no music (ikemen1 names sound/Title.mp3, Select.mp3,
     Versus.mp3; KFM's stage names kfm.mid), so nothing plays until songs are added. Not yet: MIDI and tracker
     files, round / final / low-life / victory music, the pause volume, options for the volumes.
   - Checked against real Ikemen-GO screenshots (a scripted copy of the release): focal lengths are in Ikemen's
     240-high game space; select elements draw by layerno around SelectBG layer 1; both sides show from the start;
     the stage portrait shows in its frame. In the fight a 4:3 stage is pillarboxed at 960x720 as Ikemen keeps the
     stage's shape, with the fight screen fitted to that area; `[WinCount]` shows matches won in a row; stage
     `[Reflection]` mirrors the fighters on the floor; the debug text starts hidden (Q / LB).
   - 8g wiring and docs (done): the title footer, version line and READMEs say milestone 8; `AGENTS.md` (outside the
     repository) has the screens, music, 360 disk / memory notes and the publish scripts.

9. Stages and rendering (done): the stage features the Ikemen release's stages use.
   - 9a stages (done): stage0, stage0-720, stage1, stageZ and interactivestage from the Ikemen release are in
     `data/stages/` and the stage menu. A stage's shape now sets the fight's game space as Ikemen's does: 240 units
     high and 240 x aspect wide (320 for 4:3, 426.7 for 16:9); the stage's own units convert by its localscl
     (game width / localcoord width) everywhere they're used (camera, start positions, bounds, screen edges, zoffset,
     shadow fade), and characters are scaled by game width / 320 on top of their localcoord, so on the 1280x720
     stage0-720 Kung Fu Man is drawn 4/3 as big as on a 4:3 stage. Each stage logs the keys it sets that aren't read
     yet ("not read yet: ..."). The select screen reads a stage's portraitscale from
     [StageInfo], as Ikemen does. Not included: the 3D stages (glTF models) and interactivestage's attached
     character (a ZSS script).
   - 9b camera (done): `core/camera` is now a translation of Ikemen's fighting camera (camera.go Reset / Init /
     action / reduceZoomSpeed / keepScreenEdge / boundY, without the Z axis or autozoom) instead of the simplified
     one: the edges ease toward keeping both players inside the tension margins, the camera zooms between the
     stage's zoomout and zoomin (startzoom, zoomindelay, zoominspeed, zoomoutspeed), follows the highest player by
     verticalfollow or, with "tensionlow", the vertical tension, and keeps x to quarter units on stages that don't
     zoom. Players are held inside the camera's screen less screenleft / screenright (Ikemen's xmin / xmax) and the
     stage's bounds. Characters, effects and projectiles draw zoomed about the ground line; background elements zoom
     by their delta (an element that doesn't scroll doesn't zoom); screen-space effects don't zoom. Training Stage Z
     and the Interactive stage zoom out to 0.75 as the players separate. The scripted KFM fights are unchanged.
   - 9c background elements (done): `window`, `maskwindow` and `windowdelta` clip stage and screenpack elements;
     `scalestart` / `scaledelta` resize them as the camera moves; parallax `width = top,bottom` floors draw as a
     subdivided trapezoid without a diagonal seam. Stage BGCtrlDef / BGCtrl timelines support Anim, Visible, Enable,
     PosSet/Add, VelSet/Add and SinX/Y, including target IDs, loop times and resetbg. Ikemen-only layer -1 draws on
     stages that declare an Ikemen version. Focused tests cover the window rectangle, floor convergence, controller
     timing, disabled motion and round reset.
   - 9d stage palette effects (done): SuperPause darkens only the stage on MUGEN's next-frame timing, leaving the
     fighters and fight screen bright; `darken` and `brightness` select its strength. BGPalFX changes the stage,
     AllPalFX changes both fighters, effects and the stage, and an active AllPalFX takes precedence over BGPalFX.
     Stage BGCtrl PalFX applies per-element add, multiply, sinadd and sinmul effects. Palette effects stack in
     Ikemen's order and retain the renderer's texture-cache rounding. Tests cover timing, expiry, precedence,
     stacking and round reset.
   - 9e wrap-up (done): the visible and logged version, both READMEs and the handoff describe milestone 9. The full
     hidden Windows suite passes, the Xbox build produces `default.xex`, and the Xenia scanner remains at the 13
     known instructions in third-party/library code. The console ZIP was checked to contain neither the Ikemen-GO
     reference tree nor the Pong project.
   - Next: milestone 10 match modes and teams.

10. Match modes and teams (done)
   - 10a Arcade and Training (done): Arcade builds a deterministic shuffled route through every real roster entry.
     The player selects once, wins advance through new CPU opponents and random stages with the correct "Match N"
     VS label, and a loss or the last win ends the route. Training keeps the clock at 99, restores both life bars and
     power every tick, and leaves player 2 as a stationary dummy unless `--cpu` is explicitly used. Quick Match
     remains a one-off CPU fight and Versus still uses controller 2. Focused tests cover route contents, mirror
     avoidance, progression, completion, a single-character roster, Training's clock and the title's direct Arcade
     launch.
   - 10b Turns teams (done): TEAM VERSUS and TEAM ARCADE are active in the title menu. Each side selects two
     characters and palettes; two connected controllers select at the same time, while one controller selects the
     CPU side after player 1. Each bout needs one round, the losing side advances to its next member, and the series
     ends when a team runs out. Team Arcade repeats that series across the Arcade route. `--mode teamversus` and
     `--mode teamarcade` open these modes directly. Focused tests cover wins, losses, draws, completion and invalid
     team sizes; `tests/inputs/team.txt` drives both controllers through selection, knocks out the first opposing
     member and verifies the next pairing starts.
   - 10c Turns persistence (done): the winning member carries life and power into the next pairing. During the
     result period it receives Ikemen's default life recovery: 27.5% of maximum life scaled by the fraction of
     round time remaining (unlimited time grants the full bonus). Incoming members start with full life and empty
     power. Focused tests cover full, half-time, expired and unlimited-time recovery; the team integration trace
     verifies recovery happens before member two enters.
   - 10d Survival (done): MISSION > SURVIVAL on the title (Ikemen's survival mode settings: one-round matches,
     persistent life, a stage menu, roster opponents). The player picks a fighter and a stage once; each won match
     brings the next roster opponent (reshuffled after every full pass, `SurvivalLadder`) on the same stage through a
     numbered VS screen, with life and power carried over and no recovery; the first loss ends the run and logs its
     wins. `--noai` keeps player 2 idle in modes against the computer, for test scripts. `tests/inputs/survival.txt`
     wins match 1 and enters match 2.
   - 10e Simul engine (done): a fight can hold four fighters, players 1 and 3 against 2 and 4 (Ikemen's player
     order), the partners played by the computer. Each fighter's P2 is its nearest enemy still in the fight
     (`Character::chooseP2`, Ikemen's p2 / enemyNear), its HitDefs check every enemy and hit each once
     (`hitdefTargets_`), opponents push each other while teammates pass through, the camera tracks all four, and a
     team is KO only when both members are; time over compares each team's average life (Ikemen's roundEndDecision).
     Partners start partnerspacing behind their leader ([PlayerInfo] p3/p4 keys). NumEnemy, NumPartner, Enemy(n),
     EnemyNear(n) and Partner see the teams. `--mode simul` / `simularcade` select two members a side and fight them
     together; `tests/inputs/simul.txt` checks that the round goes on after P2's KO and ends at P4's.
   - 10f team menu and Simul screen (done): TEAM VERSUS and TEAM ARCADE open Ikemen's team menu on each side first
     (f_teamMenu from ikemen1's `pN.teammenu.*`): TEAM MODE, Single / Simul / Turns with the blinking active item,
     cursor and member icons; player 1 sets the computer's side too. Single picks one fighter, Simul and Turns two
     (Tag isn't offered). Simul is fought when either side chose it (a side of one against two works: fighters have
     explicit sides), Turns when either chose Turns. In the fight a Simul side shows [Simul Lifebar], [Simul Face] and
     [Simul Name] (p1 / p3 and p2 / p4). `--mode turns` / `simul` preset the menus for tests; `tests/inputs/team.txt`
     now chooses Turns in both menus.
   - 10g Watch (done): WATCH MODE > CPU MATCH (Ikemen's watch settings: the computer plays both sides, the team
     menus and stage menu still apply). Player 1 picks both sides' fighters, team modes and the stage. Fixed on the
     way: a Single pair in a team mode is now a normal match (it fell into one-round Turns bouts); TEAM ARCADE with
     Single sides is a normal Arcade fight. `tests/inputs/watch.txt` checks that the computer plays player 1.
   - Not included in stage 10: Tag teams (an Ikemen system), more than two members a side, the smarter AI option.

11. Options and saving (done): Ikemen's options and config.ini.
   - 11a settings (done): `core/settings` holds the options mugen360 uses, with Ikemen's defaults and ranges
     (defaultConfig.ini, config.go): Difficulty (the computer's AI level; 5, where mugen360 used 8 before), Life %,
     Time (counts, -1 none, drawn as "o"), GameSpeed, Match.Wins, Match.MaxDrawGames, Simul.Match.Wins, Master / Wav
     / BGM volume, and each player's button mapping ([Joystick_Pn]). They load from and save to `save/config.ini`
     (`platform::writeWholeFile`; the 360 package ships `save/` because xsdk can't make folders) and apply where
     Ikemen applies them: rounds to win and draws replace fight.def's match.wins ("Replaced by ingame options"), the
     clock is Time x framespercount, life is scaled, game speed changes ticks a second, volumes scale music and
     sounds, the pads go through the mapping. Test runs keep the defaults whatever is saved.
   - 11b options screen (done): the title's OPTIONS opens Ikemen's options screen (`core/options_screen`, from
     [Option Info] and OptionBG; options.lua's menu and handlers, main.lua's f_menuCommonDraw): Game Settings
     (Difficulty, Time Limit, Life, Game Speed, Rounds to Win, Max Draw Games, Simul Settings), Audio Settings
     (Master / BGM / SFX volume, applied at once), Input Settings (Default), Default Values, Save and Return, Return
     Without Saving, with the value column, the dimmed box and the pulsing box cursor. Fonts: TrueType .def fonts now
     load (stb_truetype; ikemen1's Open Sans for the options and info text, its .ttf and licence in
     `data/font/Open_Sans/`), glyphs rasterized at the size drawn. `tests/inputs/options.txt` changes two options and
     saves (a test run saves to tests/out/config.ini).
   - 11c joystick config (done): Input Settings > Joystick Config is Ikemen's key menu (options.lua f_keyCfg,
     [Option Info] keymenu.*, which inherit the menu.* keys): a pane per player with Config all, A B C X Y Z Start
     and Back; left / right switch the player, a button on a row waits for every button to be let go and takes the
     next pad button pressed, Config all goes down every row. A button another row of the same player had leaves
     that row "Not used" (menu.valuename.nokey), saved that way in [Joystick_Pn]. Directions stay on the d-pad.
     `tests/inputs/keys.txt` gives player 1's A to RB and saves.

## Asset notice

Kung Fu Man, his stage, `common1.cns`, and the shared fight resources are Elecbyte/Ikemen release assets included
for private compatibility testing. Review their original terms before redistributing this repository or publishing
builds containing them.
