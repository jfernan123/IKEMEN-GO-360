# MUGEN360

MUGEN360 is a standalone, MUGEN-compatible fighting-game engine for Xbox 360, rewritten in C++ using
[Ikemen-GO](https://github.com/ikemen-engine/Ikemen-GO) as the behavior reference. It also has a Windows build for
fast debugging and automated tests. The Xbox 360 port uses the open-source
[xsdk](https://github.com/xenosworks/xsdk) toolchain.

Kung Fu Man versus Kung Fu Man is the current launcher configuration and compatibility target. Character behavior
comes from normal MUGEN data files rather than hardcoded moves, so the engine can support other characters and games
as compatibility grows.

## Current status

Milestone 6e is complete and milestone 7a has started. KFM can fight full matches with commands, attacks, guarding, throws, falls, life and rounds,
a CPU opponent, hit effects, Explods, palette effects, pauses, afterimages, environmental shake, and sound. Kung Fu
Blocking now parries attacks through ReversalDef and HitOverride, and attacks apply MUGEN corner push. Both the
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
| Esc | Quit |

On a controller, use X/Y/RB for punches, A/B/RT for kicks, LB for debug information, LT for a new match, Start for
the character's Start button or taunt, LB+Start to change screens, and Back to quit.

## Tests

Build the Windows version, then run:

```bat
build\pc\mugen360_tests.exe
tests\run_all.bat
```

The suite includes unit coverage for expressions, state loading, effects, sound decoding and mixing, reversals,
HitOverride and corner-push defaults. Hidden scripted matches cover commands, attacks, guarding, throws, falls,
juggling, rounds, time over, SuperPause, AfterImage, Kung Fu Blocking, corner push and taunting. Scripted runs never
send keyboard or mouse input to the desktop.

## Project layout

- `src/core/` contains platform-independent engine code translated from Ikemen-GO.
- `src/platform/platform.h` defines the platform boundary.
- `src/platform/pc/` implements Windows video, input, and audio with SDL3.
- `src/platform/x360/` implements the same interface with xsdk.
- `data/` contains the current KFM character, stage, common states, effects, and sounds.
- `data/mugen360.ini` chooses the two character definitions and stage loaded at startup.
- `tests/` contains unit tests and scripted fight regressions.
- `third_party/` contains lodepng.

MUGEN files are little-endian while Xbox 360 is big-endian. Binary data is read through `core/bytes.h`, and runtime
paths use forward slashes.

## Milestones

1. **Sprite viewer** (done) - SFF v2 sprites (LZ5, RLE8, RLE5, PNG) and palettes, on PC and in Xenia.
2. **Animation player** (done) - AIR actions with Ikemen's frame timing, loops, flips, offsets, offset/scale
   interpolation and collision boxes. Not yet drawn: additive/subtractive blending and rotation.
3. **Stage and camera** (done) - stage .def: normal, parallax and animated backgrounds, tiling, masking, add/addalpha/sub
   blending, velocity, position links; 4:3 stages fitted to 16:9 with cuthigh/cutlow; MUGEN's camera (tension, bounds,
   vertical follow) and screen edges; shadows. Not yet: zoom, BG controllers, windows, sine motion, reflections,
   parallax "width" trapezoids (drawn at their average width).
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
   - 7a configurable content (in progress): P1, P2 and the stage are selected by `data/mugen360.ini`, with optional
     PC command-line overrides. Next, non-320 character coordinates will be corrected and checked against the
     official 1280x720 KFM variant.
   - 7b projectiles: Projectile controllers, animation, movement, collision, hits and projectile triggers.
   - 7c helpers: Helper, DestroySelf, parent/root binding and parent variables.
   - 7d broader state compatibility: remaining common controllers and triggers, selected from real character usage.
   - 7e legacy characters: SFF v1 and PCX support for WinMUGEN-era content.
   - 7f game flow: roster/select flow and a fuller per-game configuration.

Screenpack-driven `fight.def`, polished menus and the remaining stage presentation features are planned after the
general character-compatibility work.

## Asset notice

Kung Fu Man, his stage, `common1.cns`, and the shared fight resources are Elecbyte/Ikemen release assets included
for private compatibility testing. Review their original terms before redistributing this repository or publishing
builds containing them.
