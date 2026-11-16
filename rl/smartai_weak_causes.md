# Why SmartAI loses to random inputs with some characters

Investigation of 2026-10-24, following `rl/smartai_scan.md`. Nothing here is fixed yet.

## How SmartAI plays

SmartAI (`src/core/smart_ai.cpp`) is two things:

1. **A button presser**: walks in, attacks when its moves reach, blocks, anti-airs.
2. **The command cheat** (`Character::turnAndInput`): now and then a random command counts as entered. This is what
   switches on a character's own built-in AI (most MUGEN AIs hide behind `var(59)`-style flags set by impossible
   commands, or turn on through `AILevel`).

**Every weak character's SmartAI beats an opponent who does nothing** (3/3 each, graphics on). So the button presser
works. Against constant random attacks it keeps choosing to guard and loses the exchanges. **Only characters whose own
AI actually fights beat random mashing.** Nearly every weak character is weak because its own AI is missing, broken,
or turns off its moves.

## Engine bugs found along the way (cross-cutting)

| # | Bug | Where | Effect |
|---|---|---|---|
| E1 | **No explods without graphics.** `FightEffects::spawnExplod` returns early when there's no renderer, so with `simulationOnly` (the RL server: training and the SmartAI scan) explods never exist and `NumExplod` is always 0. | `src/core/effects.cpp:105` | 146 of 150 characters test `NumExplod`. The headless fight differs from the real game. The scan itself was skewed: Luke Cage and Shuma-Gorath are fine with graphics (8/10 each); Spider-Man flips the other way (100% headless, 1/10 with graphics). |
| E2 | **`persistent` honored in negative states.** Ikemen's compiler only reads `persistent` when `stateNo >= 0`, so in states -1/-2/-3 a controller runs every tick. mugen360 applies it everywhere. | `src/core/states.cpp:427`, `character.cpp:572`; Ikemen `compiler.go:7281` | 137 characters have such controllers. Wildcat's combo-damage reset runs once and never again, so his damage stays at x0.1 all match. |
| E3 | **`AnimElem = 20 = 2`** (MUGEN reads it like `AnimElem = 20, = 2`) is always false in mugen360. | expression evaluation | Rogue's Rising Repeating Punch (state 1020) never switches to air physics: she rises forever (y = -36,000 after 6,700 ticks) and the match ends on time over. |
| E4 | *(unverified)* A grab can be cancelled the tick it connects, before its own state runs. Beast's 1050 hits with `p1stateno = 1052, p2stateno = 1053`; on the next tick his CMD cancels into Jump Strong Kick (650), whose `trigger4 = MoveContact` has no state check, because the character still counts as airborne. 1052's `TargetState 1054` never runs and the opponent stays in 1053 (no exit) for the rest of the match. | state-type timing on a `p1stateno` change | Needs checking against Ikemen: when does the new state's `type` apply? |

## Each weak character

Headless = the 50-game scan (RL server, E1 applies). Graphics = 10 games in scripted test mode with graphics loaded.

### Fine once E1 is fixed

| Character | Headless | Graphics | Cause |
|---|---|---|---|
| Luke Cage | 2% | 8/10 | E1. |
| Shuma-Gorath | 0% | 8/10 | E1: his AI template jumps to a "Press Start" state (3500) whenever `NumExplod(9001) = 0`; headless the explod never exists, so he loops into 3500 every tick and never fights. |

### Engine bugs

| Character | Graphics | Cause |
|---|---|---|
| Wildcat | 0/10 | E2: damage scaled to x0.1 for the whole match. |
| Rogue | 0/10 (time over) | E3: Rising Repeating Punch flies off forever. Her AI switch (`var(13)` via CPU commands) does work. |
| Beast | 2/10 | E4 (unverified) strands the opponent in his grab state. His template AI has 42 attack rules otherwise. |

### No AI of its own (only the button presser plays)

Jean Grey (AvX), Omega Red, Silver Samurai, Deathstroke (old), Metallo, Spider-Woman, Dormammu, Superboy, Rorschach,
Gambit, War Machine (its one "AI switch" is just the guard-push command).

### AI in the files, but broken by the character's author

| Character | Problem in its files |
|---|---|
| Storm | AI rules test `var(59)`, but nothing ever sets it. |
| Robin | "AIActivate" (CPU1-CPU30) sets `var(59) = 0`: switching the AI "on" keeps it off. |
| Psylocke | Activation sets `var(59) = 10`; all ~90 AI rules test `var(59) = 1`. Never fires. |
| Invisible Woman | Activation sets `var(0)` (`v = 0`) instead of `var(59)`. Never fires. |
| Black Manta | Ships his own `common.cns` where walking, crouching and jumping follow the controller only when `!AILevel`; with AI they make random choices. SmartAI's movement and guarding are overridden, and he has no attack AI. |

### AI switches on, but there's nothing behind it (or it hurts)

| Character | Problem |
|---|---|
| Namor, Wolverine, Black Widow, Peacemaker, Ant-Man | The Marvel template's switch (`var(59)`, on ~30% of ticks at AI level 5) has **0 AI rules that attack**. |
| Rhino | The AI flag (set by the cheat via AI_1..AI_15) **disables his specials and hypers** (`Var(59)!=1 && command=...`), and no AI rules replace them. Turning his AI on makes him weaker. |
| Surtur | AI works (22 rules, uses specials and hypers), but the flag also turns off 31 command moves. Thin, defensive AI. |
| Nightcrawler | Only 4 AI attack rules. |

### AI works, just weak against mashing

Hawkeye (30 AI attack rules, 2/10), Apocalypse (33, 2/10), Havok (15, 2/10). Their template AI is on only about 30% of
ticks at level 5 (`Random <= (AILevel-2)*100`), and these AIs don't hold up against constant random attacks.

## Method

- Scripted test runs with `--trace` (P1 a random input script, P2 `--cpu --ai smart`), state and hit counts per side.
- Static checks of each character's CMD/CNS: AI switch commands (`Character::findAiSwitches`), `AILevel` use, AI-gated
  `ChangeState`s into attack states, `NumExplod`, `persistent` in negative states.
- Graphics-on rescans (10 games each) and an idle-opponent test (3 games each).
