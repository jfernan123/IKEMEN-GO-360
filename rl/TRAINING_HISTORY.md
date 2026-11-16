# Training history: the neural Captain America (and Thanos)

Everything done to train mugen360's neural fighters, in order, from the first pipeline to the current run. Each entry
says what changed, why, and what came of it. **Add to this file after every training step** (new run, setting change,
result, bug that affects training).

Since the cleanup after PR #34 (2026-11-16), the launchers still in use are in `rl\scripts\`, scan results in
`rl\reports\`, and every run uses `build\rl` (from `build_rl.bat`). The other launchers and `train_ppo.py` named below
were removed; git history has them.

Times are local (Toronto). Run ids are the TensorBoard folders in `rl/runs/`, named by start time. "Decisions" are
policy steps: one decision = one direction + one button held for 3 engine ticks (1/20 s), so a 99 s match is up to
about 2,000 decisions. Win rates are over the last 200 matches against that opponent unless a test is named.

---

## Where things stand (2026-10-31)

The method: `rl/PIPELINE.md`. This file is the diary.

| | |
|---|---|
| **In the game** | Swapped by `play_vs_neural_thanos.bat` (Thanos main agent at 178.7M) / `play_vs_neural_cap.bat` (Cap 297M, `league_cap_final.pt`). The game exe has 8 buttons until rebuilt |
| **Best general Cap** | `rl/checkpoints/cap_scratch_league/snap_000491520000.pt`, from scratch, win/loss only: Doom 98%, Nightwing 95%, Venom 92% (run-end checks), Thanos 0% |
| **Best vs Thanos** | `rl/checkpoints/cap_thanos_history/`, copies between 76M and 105M: 95-97% rolling wins at full Thanos life, not yet evaluated over 64 matches. Not `cap_thanos.pt`: the live run has collapsed |
| **Best Thanos** | `rl/checkpoints/league_thanos.pt`, about 157M+: 96–98% in his league, 92–100% against every SmartAI check |
| **Training now** | Six Cap specialists from scratch, 3 at a time: `train_scratch_all.bat`. Queue state in `rl/checkpoints/scratch_queue.log` |

---|---|
| **In the game** | Swapped by `play_vs_neural_thanos.bat` (Thanos main agent at 178.7M) / `play_vs_neural_cap.bat` (Cap 297M, `league_cap_final.pt`) |
| **Best Cap** | `rl/checkpoints/league_cap_final.pt`, 297M: beats the 139M one 77% head to head (the 268M copy), stock Cap 74% |
| **Best Thanos** | `rl/checkpoints/league_thanos.pt`, about 157M+: 96–98% in his league, 92–100% against every SmartAI check |
| **Training now** | Cap vs Cap, win/loss only, random sides: `train_cap_mirror.bat` → `rl/checkpoints/cap_mirror.pt` (Phase 11). Thanos main agent finishing to 300M (`main_thanos.pt`) |

---

## The current setup

### What the policy sees (217 numbers, `src/core/rl_observation.cpp`)

For each fighter, itself first and then the opponent (106 each):

- **Basics (16):** life and power (as fractions), world x/y, velocity x/y, facing, ctrl, guarding, in guard distance,
  hitpause time, movecontact, state number, time in state, statetype (S/C/A), movetype (I/A/H).
- **Boxes (21 + 21):** up to 4 attack boxes (Clsn1) and 4 hurt boxes (Clsn2) of the current animation frame, in
  world coordinates, each with a present flag, plus a count.
- **"Little guys" (48):** up to 4 helpers or projectiles that side owns, attacking ones first, then nearest. Each:
  present, projectile or helper, x, y, velocity x/y, facing, has an attack box, and the box around its attack boxes.

Then 5 shared numbers: the distance between the fighters, whether the round is on, the time left, and the last
direction and button it pressed.

Alongside the numbers, the network gets **who it plays and who it fights** as two learned 24-number character
embeddings (151 characters in the roster), and, with skills, a **16-number style code**.

### What it does

Each decision is one of **9 directions** (neutral, forward, back, down, down-forward, down-back, up, up-forward,
up-back, relative to facing) and one of **8 buttons** (none, x, y, z, a, b, c, start), held for **3 ticks**. It
plays through the character's own CMD file exactly like a person, so it has to learn the motions (quarter circles,
charges) and timing itself. No character-specific code.

### Reward

Every decision: damage dealt ÷ opponent's max life − damage taken ÷ own max life. At the end: +1 win, −1 loss. Best
possible match: +2.

### Network: "large" (`rl/model.py`), 704,826 parameters

```
observation (217) + self embedding (24) + opponent embedding (24) + style code (16) = 281
  → Linear 256, LayerNorm, ReLU
  → + block (Linear 256, LayerNorm, ReLU)          skip connection
  → + block (Linear 256, LayerNorm, ReLU)          skip connection
  → GRU cell 256 (memory across the match, reset when a match ends)
  → direction head: Linear 128, ReLU, Linear 9
  → button head:    Linear 128, ReLU, Linear 8
  → value head:     Linear 128, ReLU, Linear 1
```

The two action heads start near uniform (orthogonal init, gain 0.01).

### Algorithm: recurrent PPO (`rl/train_pool.py`)

| Setting | Value |
|---|---|
| Fights at once | 512, in `mugen360 --rl-pool` processes (headless, binary protocol, deterministic per seed) |
| Rollout | 64 decisions per fight → 32,768 decisions per update |
| Discount / GAE | γ 0.997, λ 0.95 |
| PPO | clip 0.2, value coefficient 0.5, gradient clip 0.5, Adam |
| Epochs / minibatch | 4 epochs, 4,096 decisions per gradient step (128 chunks of 32) |
| Sequence training | BPTT 32: whole 32-decision chunks, the GRU run through each from its stored memory |
| Learning rate | KL-adaptive: starts 1e-3, halved when KL > 0.04, ×1.5 when KL < 0.01, between 1e-5 and 3e-3 |
| Entropy bonus | 0.003 (0.01 made multi-opponent training drift toward random) |
| Styles | 8 skills (SMERL/DIAYN), style bonus 0.001, only against opponents beaten 60%+ |
| ReDo | every 20 updates: dormant units recycled |
| Self-play league | AlphaStar-style, scaled down (below) |
| Speed | about 8,900 decisions/s (Thanos league, 10-27) |

**Self-play league:** league fights are a mirror; player 2 is played by the trainer. 35% of the time P2 is the
current policy; otherwise a frozen snapshot picked by PFSP, weight (1 − win rate)² + 0.02, so the snapshots it loses
to come up most. A snapshot joins every 300 updates (~10M decisions); 16 are kept, snapshot 0 (the start) always.
Only player 1 learns.

### In the game (`src/core/neural_policy.cpp`)

`rl/export_policy.py` writes the network to `.m3nn` (no PyTorch needed); the C++ copy runs on the CPU (PC and 360),
reads the same observation code as training and **samples** its moves like training (so it varies). It always plays
**style 0**. VS NEURAL CAP (`data/neural/neural.ini`): the policy is P1, the person P2. The mode waits out the intros
and round calls before the policy plays, because it never saw them in training.

---

## Timeline

### Phase 0: the first pipeline (Codex, Fri 10-23, 22:00–23:52)

The user asked for "a reinforcement learning pipeline that learns how to win a match against the current bot"
(SmartAI), on the Marvel game, with two random characters, internal state as features and character identity.

**Built:** `train_ppo.py`, `mugen_env.py`, `mugen360 --rl-server` (one process per fight, text protocol).

- **Observation:** 121 numbers: the basics and boxes above, no helpers yet.
- **Network:** "small", 262,074 parameters: 169 → 192 tanh → GRU 192 → linear heads.
- **Settings:** the same actions and reward as today; γ 0.99, entropy 0.01; random roster pairs; self-play against
  frozen snapshots 35% of the time after 300k decisions.
- **Simulation-only loading:** no sprites or sound.
- **Hardware:** 8 environments, CUDA (RTX 3070 Ti); about 200–330 decisions/s with PPO.

**Runs** (`marvel_policy.pt`, full random roster): `20261023-224237` through `231938`, about 780k decisions. Training
said 30–50% wins; a deterministic test said **13%** (8 of 60).

**Changes:** self-play off, an 85% qualification gate on 100 fixed matches, probe counters (policy/SmartAI,
policy/policy, policy/random, SmartAI/random). A from-scratch roster run (`marvel_policy_scratch.pt`,
`20261023-234517`, `235144`) reached 508k decisions.

### Phase 1: diagnosis and the Cap mirror sanity check (Sat 10-24, 00:06–07:00)

**What was wrong:**
- The probes and `evaluate.py` played **argmax** while training **samples**: two different players (bug #10).
- γ 0.99 only looks about 5 s ahead.
- Far too little data: about 300 matches spread over 151 characters.
- Only about 232 decisions/s.

**Changes:** γ **0.997**; `--p1/--p2/--stage` for a fixed matchup that reuses the process; no probes.

**Wolverine mirror** (`20261024-002423`): dropped in minutes. SmartAI plays Wolverine so badly that random inputs win
7 of 8, so it proved nothing.

**Cap vs SmartAI Cap** (`train_sanity_check.bat`, `20261024-003318`, `sanity_captainamerica.pt`): the first "cap vs
cap".
- Setup: from scratch, small network, entropy 0.01, 8 environments, 1M decisions (about 35 min). Random inputs lose
  8 of 8 here.

| Decisions | 28k | 82k | 322k | 426k | 567k | 740k | 1M |
|---|---|---|---|---|---|---|---|
| Wins | 0% | 12% | 46% | 75% | 91% | 96% | **99–100%** |

- **Verdict:** the pipeline learns; the roster run was starved of data.
- Clips: this Cap beat SmartAI Spider-Man and Hulk, and lost to Batman and Thanos.

**SmartAI scan** (`scan_smartai.py` → `smartai_scan.md`/`.csv`): every character's SmartAI against random controls,
50 games each.
- **85 characters ≥75%**; 32 below 25%.
- Broken or stalling: Beyond, Shuma-Gorath, Grundy, Luke Cage, Doomsday.

**Replays didn't reproduce:** the same seed played differently headless and with graphics.
- `--rl-record` was added so clips are recorded through the RL server.
- Later traced to bug #1 (explods missing headless) and #9b (resets didn't restore all state).

### Phase 2: one network for 10 characters, then Cap against 10 (10-24, 07:04–15:10)

**roster10, attempt 1** (`20261024-070706`, now `roster10_old.pt`): 10 strong-SmartAI characters (Cap, Spider-Man,
Hulk, Superman, Batman, Joker, Thanos, Darkseid, Doom, Venom), random pairs. From scratch, small network.
**Flat at 23–28% for 4.3M decisions.**

**Speed work:**
- The engine was a Debug build. The optimized `build_rl.bat` build is **10× faster per tick** and was checked
  tick-identical.
- The trainer synced GPU→CPU about 70 times per step; now once.
- Result: 494 → 1,226 decisions/s.

**roster10, attempt 2** (`train_roster10.bat`, `20261024-092358`):
- Settings: entropy **0.003**, rollout **512**, 16 environments, `--init-from` the sanity Cap, 10M decisions.
- **Still stuck at about 29%.** Cap himself fell from 98–100% solo to 24%.
- **Lesson:** many characters in one small network interfere.

**SmartAI weak causes** (`smartai_weak_causes.md`, diagnosis only): engine bugs E1–E4. The big one, **E1: explods
didn't exist headless**, and 146 of 150 characters test `NumExplod`. So training was playing a different game from
the real one.

**cap_vs10** (`train_cap_vs10.bat`, `20261024-144110`):
- Setup: Cap always P1 against a random one of the 10 SmartAIs, same settings, 3M decisions.
- About **69% overall**: Cap, Spider-Man 100; Darkseid 98; Batman, Hulk 92; Doom 86; Venom 66; Joker 30;
  Superman 10; **Thanos 2%**.
- **Lesson:** one character against many opponents learns.
- A Thanos clip showed **double jump wasn't implemented** (72 characters use it, Cap included).

### Phase 3: Cap against Thanos (10-24, 15:12–22:57)

**From scratch, small** (`train_cap_vs_thanos.bat`, `20261024-151231`, then `163410`): 0% until 2M, about 13% by
10M.
- Unlike the mirror, Cap never won by luck, so he rarely saw what a win looks like.
- Saved wins replayed in a fresh process turned into losses (bug #9b). `watch_win.py` was made to record wins live.

**Problems found (17:05):**
- **#10b:** the observation had only the main fighters' boxes. 78 of Thanos's 143 hits came from his **helper**,
  invisible to the policy.
- **#10c:** the GRU was trained one step at a time, with no sequence training.

**Changes (17:15–17:47):**
- **Double jump** in the engine.
- **"Little guys" in the observation** (121 → **217**): "if someone has a little guy, add it."
- **BPTT 32.**
- The user asked about an LSTM; we kept the GRU and fixed its training instead.

**v2** (`train_cap_vs_thanos_v2.bat`, `20261024-181106`): the user wanted aggressive updates.
- lr 3e-4 → **1e-3**, minibatch 512.
- `--init-from` **widening**: the old 121 inputs keep their weights, the new ones start at zero (identical play).
- KL was about 0.10 and wins slid from about 15% to 5%.

**Deeper network** (`--size large`, `train_cap_vs_thanos_large.bat`, `20261024-184025`): "might not be deep enough."
- Added: 3×256 with LayerNorm and skips, GRU 256, two-layer heads (700,602 parameters), and the **KL-adaptive
  learning rate**.
- Ran at about 1,620/s; replaced by the pool at 1.2M decisions.

**The RL pool (18:49–19:03).** "16 envs is killing the training… at least 512."
- The 16 processes used only 2.5 of 16 cores.
- Built: `mugen360 --rl-pool N` (N fights in one process, characters loaded once, a thread pool, a binary protocol,
  each match rebuilt and reseeded so a seed is deterministic), `pool_env.py` and `train_pool.py`. Two pools take
  turns so the GPU picks one pool's moves while the other simulates.
- **Bug #1 fixed:** explods spawn headless.
- **About 11,200 decisions/s with 512 fights in 315 MB** (it had been about 1,600/s in 870 MB).

**The Thanos specialist** (`train_pool_cap_vs_thanos.bat`, `20261024-190244`, then `202615`): from scratch, large
network, 512 fights.

| Decisions | 10M | 20M | 30M | 40M | 50M | 55–60M | 60–90M |
|---|---|---|---|---|---|---|---|
| Wins | 4% | 18% | 28% | 39% | 50% → 63% | 70% | plateau 65–68% |

- **Test at 85M** (512 matches): sampled 65%, greedy 78%, random 0%.
- **Transfer test** (about 53M): Superman 78, Hulk 73, Joker 59, Batman 31, Darkseid 20, Doom 12, Cap 11,
  **Venom 6%**.
- **Pool SmartAI rescan** with explods working (`smartai_pool_scan.csv`): Nightwing the most dominant; Thanos 100%.
- **Entropy 0.01 → 0.003** (`20261024-214853`): **about 86%** at 133.7M. Saved as **`cap_thanos_specialist.pt`**.

**PalNo bug (#8d, 22:25):** the live viewer kept losing while training won.
- `PalNo` was always 1 headless. Cap picks his shield style by palette, so training and graphics were two different
  Caps.
- After the fix the viewer won 4 of 4.

**Variety:** the user felt Cap was "cheesing Thanos with the shield." We surveyed DIAYN, DADS, ASE, SMERL, DOMiNO,
RSPO, DvD, MAP-Elites and AMP, and parked it for multi-opponent training.

### Phase 4: Cap against 5 opponents (10-24 22:56 – 10-25 11:28)

**Opponents:** Thanos, Venom, Doom, Darkseid, Nightwing: strong SmartAIs in different styles that Cap lost to.
- One pool per opponent, about 102 fights each.
- `train_pool_cap_vs5.bat`, init from the specialist. "None of the fancy shit yet."

**Entropy 0.01 by mistake** (`20261024-225945`, `pool_cap_vs5_entropy01.pt`): **collapsed toward random** (entropy
3.0 → 3.9). Thanos 74 → 14%.

**Dormant-unit check:**
- Layer 3 was already **81% dead** from the Thanos-only run.
- This run didn't add to it. The Thanos drop was interference; the slide toward random was the entropy.

**Entropy 0.003 + Thanos anchor** (`20261025-002824`): the anchor is a CLEAR-style KL toward the frozen specialist's
moves, only in Thanos fights, coefficient 0.5. Ran 300M overnight.

| Opponent | Start | 50M | 100M | 200M | 300M |
|---|---|---|---|---|---|
| Doom | 12% | 70% | 92% | 95% | ~94% |
| Nightwing | 28% | 62% | 71% | 86% | ~90% |
| Thanos | 85% | 88% | 88% | 88% | ~88% |
| Darkseid | 17% | 50% | 63% | 75% | 75% |
| Venom | 2% | 12% | 17% | 30% | ~33% |
| **Overall** | 33% | 58% | 68% | 78% | **80%** |

- Resumed (`20261025-111020`) to about **309M** = `pool_cap_vs5.pt`, later the first in-game Cap (`cap_vs5.m3nn`).
- The user declined an extra throw/attack-type input ("leave it").

### Phase 5: ReDo and styles (10-25, 11:21–22:55)

"The policy is too deterministic, doesn't look fun."

**Added:**
- **8 skills:** a 16-number code per match, so the first layer takes 281 inputs and the network has 704,826
  parameters. Existing weights get zero weights on the code, so it plays the same at first.
- **A discriminator** (training only): guesses the skill from how Cap plays.
- **The bonus (SMERL's rule):** coefficient × (log q(skill) + log 8), paid only against opponents beaten 60%+.
- **ReDo every 20 updates.** Heads are recycled the paper's way; LayerNorm units are "reopened" (scale 0, offset
  1e-3).

**vs5 + skills** (`train_pool_cap_vs5_skills.bat`, `20261025-112827`, then `121936` with bonus 0.01):
- Win rates held, and **Venom 42 → 62%**.
- But the styles never separated: discriminator accuracy about 15% (chance 12.5%).
- Saved as `pool_cap_vs5_skills.pt` (33.6M into this run).
- Fixes added: `--skill-noise`, and **`--style-window 32`** (the discriminator judges a running average over about 32
  decisions, not one frame).

**Styles sanity: Cap vs SmartAI Cap from scratch** (`train_pool_styles_sanity.bat`):
- `123608`, bonus 0.01: styles separated (96% accuracy, 7.1 of 8 distinct), but the bonus outweighed winning; wins
  bounced 48–74%.
- `125308`, bonus **0.002**: **99% wins and 90% accuracy at 7.9M** (`styles_sanity_cap_mirror.pt`). **The mechanism
  works.**
- `style_report.py` names the styles (rushdown, defensive, air-heavy, grounded, patient…) and makes a confusion
  matrix.

**8 styles against Thanos** (`train_pool_thanos_styles.bat`):

| Run | Setup | Result | Saved as |
|---|---|---|---|
| `131356` | From the specialist | 89% wins, but ~1.1 distinct styles | `cap_thanos_styles_from_specialist.pt` |
| `132932` | From scratch with ReDo | 0% at 6M. ReDo kept re-zeroing LayerNorm units before they could grow (**#10d**); ReDo off for from-scratch runs | `cap_thanos_styles_redo.pt` |
| `134359` | Big random codes | **15–20× slower** than without styles (14–19% at 169M), and the styles merged | `cap_thanos_styles_bigcodes.pt` |
| `185513` | **`--skill-scale 0.01`** (tiny codes, all 8 start as one policy) | Stopped for the curriculum | `cap_thanos_styles_tinycodes.pt` |

### Phase 6: life curriculum, then styles that win (10-25 19:07 – 10-26 12:55)

Thanos's AI turns on within a second even at AI level 1, so the curriculum uses his **life** instead.
- Pool command `'L'` and `--curriculum 0.25,0.5,0.75,1`: each step once Cap wins 60% of 200 there.
- The style bonus only counts at full life.

**`20261025-191214`** (from scratch, tiny codes, entropy 0.01):
- Steps cleared: 25% at 1.8M, 50% at 7.5M, 75% at 31M.
- Against full Thanos it plateaued at **35–38%**, and the styles merged (1.4).
- Saved as `cap_thanos_styles_curriculum_ent01.pt`.

**`20261026-001501`** (entropy **0.003**, 150M → 300M):

| Decisions | 150–160M | 180–200M | 200–220M | 240–260M | 260–300M |
|---|---|---|---|---|---|
| Wins | 47% | 61% | 66% | 65% | **64–66%** |
| Distinct styles | – | 2.5 | 4.2 | 5.8 | **6.3–6.4** |

- **Style report at 300M:** back-off 72%, pressure 69%, mid-range 64%, jumper 67%, sniper 75%, patient 70%,
  high-flyer 66%, scrapper 58%.
- **Continued:**
  - `055909`: 401M; 7.1 distinct styles; style win rates ranged from 41% (rushdown) to 78% (balanced).
  - `100626`: style bonus 0.002 → 0.001.
  - `112228`: ran to **471.8M at about 70%**. This is `cap_thanos_styles.pt`, and the run was stopped for the league.

### Phase 7: the neural Cap in the game (10-26, ~10:14–13:02)

- **Built:** `export_policy.py` → `.m3nn` (read through ByteReader, so it works on the big-endian 360), a C++
  network unit-tested against PyTorch, the VS NEURAL CAP mode and select screen, and `data/neural/neural.ini`.
- **First policy:** `pool_cap_vs5.pt` (309M).
- **In-game:** 4 of 5 against SmartAI Thanos, after making him wait out the intros (2 of 5 before).
- **Engine fixes that touched training:**
  - #2 persistent in negative states
  - #3 AnimElem
  - #5 explod front/back
  - #8c afterimages
  - #9 the RL server rebuilds its fight every match
  - #10 probes sample instead of argmax
  - #12 an optimized PC build
  - #19 test and RL runs ignore the saved config
- The user found the AI weak against a human: it had only ever fought SmartAI. **Next: self-play.**

### Phase 8: the self-play league (10-26 12:46 – 10-27 11:18)

AlphaStar's league, scaled down (`--league`, `--league-self`, `--snapshot-every`, `--league-size`, PFSP). There are
no human replays: the trained checkpoint stands in for AlphaStar's imitation start.

**`train_league_cap.bat`, `20261026-131407`, `league_cap.pt`:**
- Init from `pool_cap_vs5_skills.pt`.
- 512 fights: 256 Cap mirror + about 51 each against SmartAI Thanos, Venom, Doom, Darkseid and Nightwing.
- Entropy 0.003, Thanos anchor 0.5, 8 skills, bonus 0.001, ReDo 20, 300M.
- Speed only about 3,800–4,700 decisions/s (explained under Phase 9).

| Decisions | League | Thanos | Venom | Doom | Darkseid | Nightwing | Note |
|---|---|---|---|---|---|---|---|
| 1.2M | 54% | 84 | 59 | 98 | 79 | 92 | |
| 57M | 70% | 85 | 52 | 98 | 74 | 94 | first league Cap in the game (`league_cap_57M.pt`) |
| 137M | 74% | 83 | 54 | 99 | 82 | 97 | "crazy good vs cap" |
| 139M | 79% | 86 | 61 | 98 | 85 | 96 | **the game's Cap now**; sent in the PC zip (`league_cap_shared.pt`) |
| 268M | 90% | 87 | 56 | 99 | 75 | 96 | `league_cap_268M.pt` |
| 297M | 85% | 88 | 45 | 98 | 89 | 98 | stopped: **`league_cap_final.pt`** |

**Test of 139M vs 268M** (`rl/eval_versus.py` head to head; `eval_pool.py` against SmartAI, 256 matches each):

| | 139M | 268M |
|---|---|---|
| Head to head (512 matches) | 23% | **77%** |
| Head to head as P1 | 42% | **96%** |
| vs stock Cap (never trained against) | 52% | **74%** |
| Thanos / Venom / Doom | 82 / 62 / 99 | 82 / 61 / 96 |
| Darkseid / Nightwing | 83 / 90 | 77 / 95 |

- **Lesson:** self-play kept improving the mirror; the SmartAI matchups stayed flat, and Venom is still the weak
  spot.
- **The styles didn't survive the league:** distinct styles ended at about 1.35 (the bonus is only paid against
  beaten opponents, and the league rarely counts as "beaten").

### Phase 9: Thanos (10-27, from 11:18)

**`train_league_thanos.bat`, `league_thanos.pt`: Thanos as P1, starting from `league_cap_final.pt`.**
- It's the same network; the character embedding tells it who it plays.
- Before any Thanos training, Cap's brain playing Thanos already beat stock Cap in 7 of 8 test matches.
- No anchor (Cap's anchor is a Cap policy). 8 skills, ReDo, entropy 0.003, 300M.

**First version** (`20261027-111858`): like Cap's league, 256 mirror + 51 each of SmartAI Cap, Venom, Doom,
Darkseid, Nightwing.
- At 3.5M: 91% overall (stock Cap 98, Venom 78, Doom 91, Darkseid 66, Nightwing 90, league 92%).
- Speed only about 4,800/s.

**Why the league runs were half speed** (TensorBoard `performance/` timings per update):

| Run | Decisions/s | Waiting on simulation | Network inference |
|---|---|---|---|
| Thanos styles (1 opponent, 2 pools) | 10,600 | 1.7 s | 0.3 s |
| Cap league (6 pools) | 3,850 | 3.9 s | **4.1 s** |
| Thanos league v1 (6 pools) | 4,800 | 4.1 s | 1.6 s |

1. **Threads were split per pool, not per fight.** 16 threads ÷ 6 pools = 2 each, so the league pool, with half of
   all fights, ran on 2 threads.
2. **League inference ran once per snapshot in play,** up to 17 network calls per step, each copying its moves back
   from the GPU twice.

**Changes (`train_pool.py`, restarted with `--resume` at 3.9M, run `20261027-113305`):**
- **Threads in proportion to each pool's fights.**
- **League-only training:** `--league 512` with `--pools 2`. The mirror splits into 2 pools of 256 with 8 threads
  each, so one simulates while the network picks the other's moves.
- **League moves stay on the GPU** until all snapshots have picked: one copy back per pool.
- **SmartAI only as a check** (the user's call: "we don't need to fight against the 5 guys every single time"):
  - `--check-every 150 --check-matches 64` plays 64 matches against each SmartAI opponent every 150 updates (about
    5M decisions, about a minute), as the game plays (style 0, sampled moves).
  - Shown as `check (...)` lines in the log and `check/` in TensorBoard; not counted in the speed.
  - Risk: without SmartAI fights he could learn habits that only beat Thanos. If the check numbers slide, put a small
    SmartAI share back.
- **Speed: about 8,900 decisions/s.**

**How it went** (to about 157M decisions, run `20261027-113305`, 7,100–8,900 decisions/s):

| Decisions | 52M | 66M | 92M | 118M | 144M | 154M |
|---|---|---|---|---|---|---|
| League wins | 84% | 96% | 96% | 96% | 99% | 96% |
| Check: SmartAI Venom | 64% | 81% | 97% | 95% | 94% | 98% |

- Later checks: Cap 98–100%, Venom 92–98%, Doom 97–98%, Darkseid 98–100%, Nightwing 86–98%.
- **The styles held this time:** discriminator accuracy 85–88% at 154M. Cap's league had ended at about 1.35
  distinct styles.
- The user: "Thanos is bodying Cap." So the next step pits the two against each other.

### Phase 10: two main agents, Cap and Thanos (10-27, from ~17:50)

**The question:** one network for both characters, or one each? The user wanted to follow AlphaStar.
- **AlphaStar:** each race (Protoss, Terran, Zerg) had its **own** main agent network. All of them trained **at the
  same time** against one shared league of frozen snapshots of every agent. A main agent's matches were about 35%
  against current agents, and the rest picked by PFSP over past snapshots, weighted (1 − win rate)², the weighting
  we already use. It also had main exploiters and league exploiters, which are left for later.
- **Choice: one network per character.** Two networks sharing one model had interfered before (roster10; Thanos
  14% in vs5 before the anchor). The two have already split (Thanos trained 157M decisions from Cap's weights), and
  each can be rolled back on its own.
- **Also considered:** alternating (train Cap for a while, then Thanos). AlphaStar trains all agents at once, so
  both run at the same time instead.

**Built (`train_pool.py`):** `--rival CHAR --rival-fights N --rival-checkpoint PATH --rival-start PATH
--rival-refresh 10`.
- N fights put P1 (the learner) against the rival character. The trainer picks P2's moves: the rival run's current
  network (re-read from its checkpoint every 10 updates when it changed; `--rival-start` until the rival run's file
  exists) 35% of the time, otherwise one of its snapshots from `<rival-checkpoint>_league/` by PFSP.
- The snapshot folder is rescanned too: new snapshots join, ones the rival deleted are dropped.
- Two trainer processes, one per character, share the league through those folders. That's AlphaStar's layout:
  separate learners, one league. Only one process ever learns each network.
- Checkpoints and snapshots are now written through a `.tmp` file and swapped in (`save_whole`), so a rival never
  reads half a file. Unreadable files are skipped until the next refresh.
- Smoke test passed: 32 fights, Cap vs the live Thanos run's checkpoint and its 16 snapshots, then `--resume`.

**The runs:** `train_main_agents.bat` starts `train_main_cap.bat` and `train_main_thanos.bat` in two windows.
- Each run: 512 fights, 256 mirror and 256 against the other character.
- Each run: `--threads 4` per pool (2 pools × 2 runs = 16 cores). Each should run at about half of one run's speed.
- Cap starts from `league_cap_final.pt` and Thanos from `league_thanos.pt`.
- No SmartAI fights. Each run checks its 5 SmartAI opponents every 150 updates.
- 8 skills, ReDo 20, entropy 0.003, snapshot every 300 updates, 16 kept. No Thanos anchor for Cap.
- Logs: `rl/checkpoints/main_cap.log` and `main_thanos.log` (`watch_main_agents.bat` shows both live; the training
  windows themselves stay blank). TensorBoard: `rl/runs/main_cap/` and `rl/runs/main_thanos/`; the `rival/` tags show
  win rates against each rival snapshot.
- **Started 17:57.** Both runs started in the same second and shared one TensorBoard folder
  (`rl/runs/20261027-175710`, ignore it). Restarted with `--resume` at about 0.3M with a folder each (`--logdir`).
- **First minutes:** Thanos beats Cap 100% in the cross fights and Cap wins 0% there. Cap's overall wins climbed from
  9% to 26% in a few updates as his mirror results came in. Both runs at about 4,800–4,900 decisions/s.

**Overnight (to 10-28; Cap 216M, Thanos 179M decisions in this run):**
- **Thanos held up.** Mirror 98%, Cap rival 99%. SmartAI checks: Cap 100%, Venom 70–86%, Doom 97–100%, Darkseid
  91–94%, Nightwing 84–94%.
- **Cap collapsed against SmartAI** while his training numbers looked good:

| Decisions | 20M | 39M | 79M | 118M | 157M | 197M |
|---|---|---|---|---|---|---|
| Rival (vs neural Thanos) | 5% | 52% | 81% | 88% | 53% | 76% |
| Mirror | 88% | 95% | 94% | 95% | 93% | 88% |

  SmartAI checks went from Thanos 69 / Venom 30 / Doom 94 / Darkseid 78 at the first check to about Thanos 0–9,
  Venom 5–20, Doom 17–50, Darkseid 16–45. Nightwing stayed 58–89.
- **Cause (likely): sides.** The learner is always P1, so every neural opponent plays as P2, a side it never trained
  on. In the 139M vs 268M Cap test, the 268M Cap won 96% as P1 but only 58% as P2. Cap learned to beat handicapped
  right-side copies and lost his real fighting. Thanos's checks didn't drop, maybe because the Cap he faces is weaker.
  Fix planned: random sides (the learner plays half its fights as P2).
- **For playing:** `play_vs_neural_thanos.bat` (Thanos from `main_thanos_play.pt` at 178.7M,
  `data/neural/thanos_main.m3nn`) and `play_vs_neural_cap.bat` (`league_cap_final.pt`, `data/neural/cap_final.m3nn`,
  not the collapsed one). Each copies `data/neural/neural_thanos.ini` or `neural_cap.ini` over `neural.ini`. The old
  139M setup is saved as `neural_cap_league139.ini`. The menu still says VS NEURAL CAP.

**Clips at 281M** (`rl/clips/main_cap_281M/`, style 0, seed 7, `record_clip.py`):

| Cap 281M vs | Result |
|---|---|
| SmartAI Thanos | lost, Thanos left at 76% |
| SmartAI Venom | lost, Venom at 100% |
| SmartAI Doom | lost |
| SmartAI Darkseid | won, 54% life left |
| SmartAI Nightwing | won |
| neural Thanos | lost |
| (old `league_cap_final` vs SmartAI Thanos) | won with 99% life |

- **Against Thanos**, the new Cap sits at the left wall and eats beams. The old one jump-throws his shield and
  pressures.
- **Against Venom**, the new Cap eats 10–22 hit combos.

### Phase 11: Cap vs Cap, win/loss only, random sides (10-28, from ~15:40)

**Why:** the clips showed the main-agent Cap blocking and sitting in the corner. The user: the reward pays as much
for not getting hit as for hitting, so he "maximized the second term", turtling against opponents that fail on
their own. The user doesn't care how much life he wins with.

**Reward options tried in the trainer** (`train_pool.py`, no engine change):
- `--damage-dealt-weight` / `--damage-taken-weight` re-weigh the engine's reward. They're split from the two life
  bars in the observation (own life is observation 0, the opponent's is at the fighter size); a match's last step
  splits the engine's number instead.
- Every log line now shows `dealt/taken` (life bars per match), and TensorBoard has `rollout/damage_dealt_100` and
  `damage_taken_100`.
- First idea: damage dealt ×1.5 (smoke-tested). The user then chose **win/loss only: +1 a win, −1 a loss**
  (weights 0 and 0), AlphaStar's reward. The discount goes 0.997 → **0.999**: at 0.997 a win 800 decisions away
  counts about 9%, at 0.999 about 45%.

**Random sides** (`--random-sides`): in mirror fights the learner plays P1 or P2 at random every match, from the
first match on.
- The trainer swaps the two views and the action slots, flips the engine's reward sign and swaps the winner.
- Log: `as P1/P2` win rates; TensorBoard `league/win_rate_as_p1` / `_p2`.
- First numbers confirm the side problem: `league_cap_final` vs itself wins **77–90% as P1 and 10–23% as P2**.

**Restarts on the way:**
- A main-agent Cap run with win/loss only, still against neural Thanos (`main_cap.pt`), ran for a few minutes and
  was stopped. The user changed the plan to "start a lil bit from scratch: Cap vs Cap only, only +1 or −1".
- The turtling main-agent Cap (finished at 300M) is kept as `main_cap_turtle.pt`, with `main_cap_turtle_league/`
  and `main_cap_turtle.log`.
- By mistake both trainers were stopped once: Thanos's command line names `main_cap.pt` as his rival. Thanos
  resumed (`--resume`, about 262.6M) and runs on with his old reward against the now-frozen `main_cap.pt`.

**The run:** `train_cap_mirror.bat` → `rl/checkpoints/cap_mirror.pt`, log `rl/checkpoints/cap_mirror.log`,
TensorBoard `rl/runs/cap_mirror/`.
- 512 mirror fights in 2 pools, random sides.
- Win/loss only, γ 0.999, no style bonus (`--diversity-coef 0`; the skill inputs stay).
- Entropy 0.003, ReDo 20, snapshots every 300 updates (16 kept).
- SmartAI Thanos/Venom/Doom/Darkseid/Nightwing checked every 150 updates.
- From `league_cap_final.pt`, 300M decisions. Speed about 5,300 decisions/s next to the Thanos run.

**At 19M (16:08, about 45 min, 5,300 → 7,300 decisions/s once Thanos's run stopped):**
- Against the league: 70%; 92% against the starting snapshot.
- As P1/P2: 80% / 64%, from 87% / 14% at the start. The side gap is closing.
- Damage per match: 0.86 dealt / 0.55 taken.
- **The SmartAI checks fell again** (64 matches each):

| Check | Thanos | Venom | Doom | Darkseid | Nightwing |
|---|---|---|---|---|---|
| 1 (~5M) | 47% | 39% | 94% | 67% | 83% |
| 2 (~10M) | 31% | 25% | 92% | 50% | 84% |
| 3 (~15M) | 20% | 8% | 78% | 25% | 61% |

- Likely cause: the run is mirror only, so what he knew about other characters fades (forgetting). The Cap league
  that kept 256 SmartAI fights next to the mirror held Thanos at 83–88%.
- Clips in `rl/clips/cap_mirror_19M/` (seed 11): beat himself in the mirror; lost to SmartAI Thanos (Thanos at 83%),
  Venom (41%) and Doom (72%).
- **Thanos's main-agent run was stopped** at 265.6M (the user: retrain him too). Frozen as `main_thanos_final.pt`.
  `train_thanos_mirror.bat` (same recipe as Cap's, from `main_thanos_final.pt`) is written but not started: waiting
  on the user's choice of a trained or random start.

**At 33M:**
- As P1/P2: 78% / 52%.
- SmartAI checks keep falling: Thanos 0%, Venom 2%, Doom 47%, Darkseid 2–6%, Nightwing 61–80%.
- Clips in `rl/clips/cap_mirror_33M/` (`build/pc_test`, with the palette fix #21):
  - SmartAI Cap: won seed 21 (45% life left), lost seed 22.
  - SmartAI Thanos: lost (Thanos at 95%).

---

## Lessons so far

- **Sanity-check new setups on an easy matchup first** (Cap vs SmartAI Cap learns in 1M decisions).
- **One character against many opponents learns; many characters in one small network didn't.**
- **Entropy 0.01 is too high for long or multi-opponent runs;** 0.003 works.
- **Headless training must match the real game exactly.** Explods, PalNo and resets each made training a different
  game. Check the viewer against training whenever something looks off.
- **Throughput matters most.** The jumps came from the optimized build (10×) and the pool (7×).
- **Styles:** start the codes tiny (`--skill-scale 0.01`), keep the bonus small (0.001–0.002) and only pay it
  against beaten opponents. Big random codes make training 15–20× slower.
- **ReDo with LayerNorm hurts from-scratch training** (#10d); it's fine on a trained network.
- **Self-play is what makes it hard for a person;** SmartAI-only training made it good against SmartAI only.

## Tried or discussed, not done

- **Algorithms and architecture:** DQN/R2D2, swapping the GRU for an LSTM, distributional RL, macro actions from the
  CMD, behaviour cloning of SmartAI, BPTT burn-in, BPTT 64.
- **Observation and timing:** throw/attack-type inputs, a state or animation embedding, wall distances, deciding
  every 2 ticks.
- **Weighting and styles:** PopArt, PCGrad, an AI-level curriculum (Thanos ignores the level), predetermined persona
  styles. Fight reweighting toward weak opponents (`--reweight-every`, built but never used).
- **Trimming Thanos's oversized jab boxes** (his own `.air`; the user said leave it).

## Tools

| File | What it does |
|---|---|
| `rl/train_pool.py` | The trainer (pool, PPO, league, rival main agents, skills, ReDo, anchor, curriculum, checks) |
| `rl/train_ppo.py`, `rl/mugen_env.py` | The original one-process-per-fight trainer (still used for `widen_observation`) |
| `rl/model.py` | The network, `add_skills`, ReDo |
| `rl/pool_env.py` | Talks to `mugen360 --rl-pool` |
| `rl/eval_pool.py` | Win rates against SmartAI opponents (sampled, greedy, random baseline) |
| `rl/eval_versus.py` | Two checkpoints head to head, each as P1 for half |
| `rl/style_report.py` | Names and scores each style |
| `rl/record_clip.py`, `rl/watch_live.py`, `rl/watch_win.py` | Videos and live viewing |
| `rl/export_policy.py` | Checkpoint → `.m3nn` for the game |
| `rl/scan_smartai.py`, `rl/scan_pool.py` | How strong SmartAI is per character |

**2026-10-28, league_cap_final clips** (`rl/clips/league_cap_final/`, style 0, seed 7, SmartAI on the right): won all 3. Thanos 98.8% life left, Venom 60.4%, Cap 78.4%. Same day the mirror run (cap_mirror.pt) is at 141M: as P1/P2 ~60%/65%, SmartAI checks Thanos 0%, Venom 6%, Doom 20%, Darkseid 2%, Nightwing 83%. Still forgetting.


**Neural vs neural clip** (`rl/clips/league_cap_final/neural_cap_vs_neural_thanos.mp4`, seed 7): league_cap_final (P1) vs main_thanos_final (P2). Thanos won in 279 decisions with 89.4% life left.


**cap_mirror at 141M vs SmartAI Cap** (`rl/clips/cap_mirror_141M/vs_smartai_cap.mp4`, style 0, seed 7): won with 50.5% life, 1193 decisions.


**cap_mirror at 168M** (`rl/clips/cap_mirror_168M/`, style 0, seed 7): beat SmartAI Cap (27.2% left), lost to Thanos (Thanos 72.7% left) and Venom (61.1% left). Log: as P1/P2 64%/72%, vs oldest snapshot 91%, SmartAI checks Thanos 0%, Venom 0%, Doom 33%, Darkseid 0%, Nightwing 61%.


**cap_mirror finished at 300M** (its --steps limit, 2026-10-29 9:05 AM). End: as P1/P2 92%/28% (the side gap came back), vs oldest snapshot 70%, SmartAI checks Thanos 0%, Venom 0%, Doom 36%, Darkseid 0%, Nightwing 23%. Mirror-only +1/-1 training from league_cap_final made him a worse fighter against everyone else; league_cap_final stays the best Cap.


**cap_mirror 300M clips** (`rl/clips/cap_mirror_300M/`, style 0, seed 7): beat the old neural Cap (league_cap_final) with 9.5% left; lost to SmartAI Cap (71.3% left) and SmartAI Thanos (96.5% left). Backup `cap_mirror_300M.pt`. **Resumed 2026-10-29 for 150M more** (`train_cap_mirror.bat --resume --steps 450000000`, nothing else changed, the user's choice).


**Corner camping check (2026-10-29)** (scratch script, 6 matches each, Gotham, style 0). Share of decisions P1 spends within 40 px of the left wall: cap_mirror 300M self-play 84% (P1 wins 6/6), vs SmartAI Cap 72% (1/6); league_cap_final self-play 64% (5/6), vs SmartAI 72% (5/6). P2 is near the right wall about 1% of the time: as P2 the policy also drifts left. Gotham is symmetric (bounds +-277, starts +-70). Cause: not the reward. The observation gives absolute world x (and facing, boxes, velocity) without mirroring for player 2. A net trained mostly as P1 learned 'be at the left wall', which is a corner defence as P1 and makes no sense as P2. That's the P1/P2 gap (92%/28% at 300M). Fix to consider: mirror the P2 view (x, vx, facing, boxes) so both sides look like P1.


**Waiting cost (2026-10-29, 6:07 PM).** The user wants quick wins. New `--step-penalty` in train_pool.py: every decision's reward loses 0.0005, about 0.5 over a typical 1,000-decision match and 1 by time over, so any win still beats any loss. cap_mirror stopped at 328M (as P1/P2 83%/56%; SmartAI checks Thanos 0%, Venom 0%, Doom 27%, Darkseid 0%, Nightwing 17%), then resumed with the penalty to 450M (in train_cap_mirror.bat now). Nothing else changed.


**Training setup review (2026-10-29).** Read train_pool.py, model.py, pool_env.py, eval_pool.py, mugen_env.py, runRlPool / RlFight (main.cpp), rl_observation.cpp, neural_policy.cpp, match.cpp. Code paths checked OK: random-sides swapping (obs, actions, reward sign, winner, hidden), GAE/done/hidden resets, the dealt/taken split, rlPad facing, game vs training consistency (3-frame decisions, skill 0, sampling), explods spawn headless now. Findings:
1. Time over is decided by life share (Match::decide), so +1/-1 at time over = "lead on life at 99 s". Measured, 48 self-play matches of cap_mirror_300M: 54% end by time over (old league_cap_final: 33%), mean length 1875 of ~1980 decisions (old: 1253), P1 wins 98%, P1 life 0.70 vs 0.27 at the bell. TensorBoard: episode length rose 1000 -> 1500-1780 after 225M as P2 collapsed. The corner camp is a clock-running strategy.
2. A drawn round (TimeDraw: equal life to 3 decimals, e.g. nobody hit; DoubleKO) never ends the match: Match::matchOver only counts wins, so NextRound starts round 2 with life refilled and the fight only reports done when someone wins. Stalling to a draw costs nothing (0, not -1). Not seen in these 48 (0% draws) but open.
3. gamma 0.999 over ~1600 decisions discounts the result to ~0.2 at the start of a match; a late loss is worth much less than an early one, so stalling while losing pays. The new step penalty (0.0005) only partly offsets it.
4. Observation x positions, velocities, facing and boxes are absolute (no mirroring for player 2): the net has to learn each side separately, hence the 92%/28% side gap and the left-wall habit.
5. Minor: --skills 8 with --diversity-coef 0 (style acc 16% ~ chance, discriminator trained for nothing); SmartAI checks test P1 only; reweight + random sides would leave obs unswapped after a rebuild (not used here); adaptive LR cap 3e-3 is high.


**Setup fixes applied (2026-10-30, 12:05 AM).** The user said "change them". Stopped cap_mirror at 414M (log: as P1/P2 86%/74%, dealt/taken 0.92/0.45, SmartAI checks Thanos 0%, Venom 5%, Doom 42%, Darkseid 19%, Nightwing 41%); backup `cap_mirror_414M.pt`. Changes:
1. Engine (`src/core/rl_observation.*`): `rlObservation(..., mirror)` reflects a view left to right (x, vx, facing, box left/right, objects, dx). The pool and the single-fight server give player 2 the mirrored view (`main.cpp` rlObservation(demo, side)), so P2's view at the start equals P1's (checked with a scratch script: all values within 0.02). The game's neural Cap plays P1, unchanged.
2. Engine (`StageDemo::trainingOver`): a training match also ends on a drawn round (`Match::draws() > 0`): done, winner -1, reward 0. Checked: 4 idle fights end at decision 1983 with winner -1 (before: round 2 with life refilled, never done).
3. Trainer: `--time-over game|loss|draw` (default game). With `loss`, a match whose previous view had the clock at 0 (observation -3) gives outcome -1 to the learner whoever led on life, so only a KO earns +1. New `rollout/time_over_100` TensorBoard scalar and `timeover=` in the status line. `--lr-max` (default 3e-3).
4. Recipe (`train_cap_mirror.bat`): `--gamma 0.9995`, `--time-over loss`, `--league 384` (128 of the 512 fights are the 5 SmartAI opponents again, learner as P1), `--lr-max 1e-3`, no fixed `--threads` (each pool gets threads in proportion to its fights), `--steps 600000000`. Step penalty 0.0005 kept; skills 8 kept (dropping them means surgery on the checkpoint; harmless).
Rebuilt `build\rl_new\mugen360.exe` (RelWithDebInfo). Resumed from `cap_mirror.pt` at 414M. The new P2 view means his P2 play starts over from his P1 habits; watch as P1/P2, timeover=, episode length and the SmartAI opponents' win rates.


**cap_mirror finished at 600M (2026-10-30, ~9 AM)** with the new rules (time over = loss, mirrored P2, SmartAI back in). Trend (log samples): 421M Doom 58% / Nightwing 60% / Darkseid 22% / Venom 3% / Thanos 2%, as P1/P2 82%/70%, timeover 0%; 529M Doom 96% / Nightwing 96% / Darkseid 39% / Venom 16% / Thanos 4%, P1/P2 66%/70% (gap gone), timeover 16%; 600M Doom 80% / Nightwing 88% / Darkseid 7% / Venom 12% / Thanos 0%, P1/P2 44%/28%, timeover 17-35%, league 36%. Peak around 520-530M, then slid as mirror matches drifted into time overs (both sides lose them). Last checks: Doom 75%, Nightwing 92%, Venom 16%, Darkseid 3%, Thanos 0%. Best candidates: league snapshots snap_000522452992.pt / snap_000532283392.pt.


**cap_mirror 532M clips** (`rl/clips/cap_mirror_532M/`, snapshot snap_000532283392.pt, style 0, seed 7, rl_new exe): beat SmartAI Doom (75.3% left, 729 decisions); lost to SmartAI Cap (Cap 44.9% left, 1211 decisions) and SmartAI Thanos (96.2% left, 293 decisions).


**cap_scratch: Cap from scratch, win/loss only (2026-10-30, 9:07 AM).** The user wants a policy that never saw damage rewards (cap_mirror started from league_cap_final, which did). First measured: random Cap vs random Cap (64 fights, Gotham) ends by KO 60/64, time over 4, so +1/-1 with time over as a loss still gives a fresh network outcomes to learn from. New trainer option `--win-time-cost C`: a win loses C per decision it lasted, losses stay -1 (the old `--step-penalty` also made a quick loss better than a slow one, an incentive to give up). `train_cap_scratch.bat`: fresh large network, no --init-from, no skills; +1/-1, `--time-over loss`, `--win-time-cost 0.0004` (1,000-decision win = +0.6), gamma 0.9995, random sides with the mirrored P2 view, 384 mirror (PFSP, 16 snapshots every 300 updates) + 128 SmartAI (Thanos, Venom, Doom, Darkseid, Nightwing), checks every 150 updates, ReDo 20, entropy 0.003, lr 1e-3 (max 1e-3), 600M steps. Checkpoint `rl/checkpoints/cap_scratch.pt`, log `cap_scratch.log`, TensorBoard `rl/runs/cap_scratch/`. First minutes: ~9,000 decisions/s (no style discriminator), as P1/P2 ~50%/44%, timeover 2%.


**cap_scratch 26M clip** (`rl/clips/cap_scratch/selfplay_early.mp4`, same policy both sides, seed 7): went to time over at 1990 decisions, P1 12.8% vs P2 70.2% (the game gives it to P2; training counts it a loss for both). Log at 26M: win100 30%, Nightwing 48%, others 0-2%.


**cap_scratch 31M vs SmartAI Nightwing** (`rl/clips/cap_scratch/vs_nightwing.mp4`, seed 7): Cap won by KO in 692 decisions with 41.8% life. Log at 31M: Nightwing 52%, vs oldest snapshot 100%, as P1/P2 46%/55%.


**cap_scratch finished at 600M (2026-10-31).** No errors. Last checks (64 matches each, Cap as P1): Venom 92%, Doom 98%, Nightwing 95%, Darkseid 0%, Thanos 0%. Trend: 82M Doom 4% / Venom 2%; 164M Doom 94% / Venom 27%; 246M Venom 95%; 492M win100 79%, dealt/taken 0.93/0.43, as P1/P2 75%/63%, timeover 2%. From ~570M the mirror drifted into time overs (12-17%) and league win fell 71% -> 53%, as cap_mirror did after its peak. Darkseid and Thanos never beaten (0-5%). Far better than cap_mirror against Venom (16%) and Doom; snapshots ~490M are the best candidates.


**cap_scratch 491M clips** (`rl/clips/cap_scratch_491M/`, snapshot snap_000491520000.pt, seed 7): beat Venom perfect in 178 decisions, Doom perfect in 376, SmartAI Cap with 60.4% left in 855; lost to Thanos without landing damage (Thanos 100%, 1124 decisions).


**Specials check + button combos (2026-10-31, ~11:45 AM).** Scratch script (specials.py, 32 matches each, cap_scratch 491M as P1): he does use specials. vs SmartAI Cap ~800 started (shield throws 1000-1020, Stars & Stripes 1200-1220, Somersault 1300, Charging Star 1400/1410, even Final Justice 3010-3018 through a chain); vs Venom 429 Charging Star A (his whole Venom plan); vs Thanos 19 normals and 1 special in 32 matches (he hides). What he could never do: any command needing two buttons at once (Cap's hypers x+y / a+b etc.), since a decision pressed one button. The AI-only entries in Cap's CMD (var(59): AI guarding, guard push 6300-6320, recovery rolls 932/955) all have player commands too.
Change: engine `RL_BUTTONS` 8 -> 65 (`rl_observation.h`): 0-7 unchanged, 8-64 = every press of 2+ of x y z a b c (`rlButtonMask`); the previous-button input keeps its old scale (a combo reads as its first button). `model.py`: `Policy(buttons=)`, `policy_from_checkpoint` reads the head size, `add_buttons` widens an 8-button checkpoint (each combo = average of its buttons' outputs, offset so combos start ~1.7% of choices; checked on real Thanos fights: the old eight's distribution unchanged, KL ~0). `train_pool.py`: button count from the pool header, widening on --init-from, --resume of an 8-button checkpoint refused with a message, "buttons" saved in snapshots. Rebuilt build\rl_new. The game exe (build\pc) still has 8 buttons: rebuild it before exporting a 65-button policy to data/neural.
**cap_thanos** (`train_cap_thanos.bat`): 491M from-scratch Cap widened to 65 buttons, all 512 fights vs SmartAI Thanos (Cap P1), same reward as cap_scratch, lr 3e-4 (max 1e-3), 200M steps, checks every 150 updates. A first 8-button start ran ~15 min (log kept as cap_thanos_8buttons.log) before the switch. ~9,900 decisions/s.


**cap_thanos restarted with a life handicap (2026-10-31, 12:10 PM).** The plain run (log kept as cap_thanos_nocurriculum.log) won 0 of ~15,000 matches in 16M decisions (dealt 0.02, taken 1.00, return exactly -1): with win/loss only and no wins there's no signal. Restarted from the same 491M snapshot (widened again) with `--curriculum 0.03,0.06,0.1,0.15,0.2,0.3,0.4,0.5,0.65,0.8,1 --curriculum-win 0.6`: Thanos starts with 3% of his life bar and grows each time Cap wins 60% of the last 200 matches at that size. The checks still fight full-life Thanos. First updates: 100% at 3%.


**cap_thanos 1.5M clips at the handicap** (`rl/clips/cap_thanos/thanos15_seed7.mp4`, `_seed8.mp4`, Thanos at 15% life): won both by KO in 58 and 71 decisions (91.7% and 90.9% left). Curriculum went 3% -> 15% in the first ~0.6M decisions; at 15% he wins 44% of 200 so far. For these, the single-fight RL server's `reset` takes an optional opponent life fraction (`mugen_env.reset(..., opponent_life)`, `record_clip.py --p2-life`), built into build\rl_next (rl_new is busy training); record_clip.py and evaluate.py now load networks with policy_from_checkpoint (any button count).


**cap_thanos 2.3M vs full-life Thanos** (`rl/clips/cap_thanos/thanos100_seed7.mp4`, seed 7): lost in 429 decisions, Thanos 84.7% left (Cap dealt ~15%). Curriculum already at 40% by 2.3M.


**cap_thanos collapse and third start (2026-10-31, afternoon).** The curriculum run climbed 3% -> 50% Thanos life in ~4M decisions (40% wins at 50%), then collapsed within ~1M: 0% wins, dealt 0.02, for 14M decisions. Clip at 50% (`rl/clips/cap_thanos/thanos50_seed7.mp4`): lost without hurting Thanos, 1166 decisions. Action scan (actions.py, 16 fights at 50%): direction Back 100% (entropy 0.01 of 2.20), buttons spread (48% combos). Cause: losing every match, gamma 0.9995 made a later loss cheaper, so delaying it (holding back/blocking) was the only gradient; lr sat at the 1e-3 ceiling all run and entropy 0.003 didn't stop the collapse; the curriculum never steps back. Kept as cap_thanos_collapsed.pt / .log.
Fixes: trainer `--curriculum-back X` (a step back when the step's last 200 matches are won < X) and `--keep-every N` (a network copy every N updates in <checkpoint>_history/; this run had no copy of its 4M peak). Restarted from 491M (widened again): gamma 1.0, lr 3e-4 (max 3e-4), entropy 0.01, --curriculum-back 0.2, --keep-every 100.


**cap_thanos third start at 37M: full-life Thanos beaten.** gamma 1 held: the curriculum reached 100% with no collapse; at 100% he wins 70% of 200 training matches, last check vs full-life SmartAI Thanos 58% (cap_scratch 491M: 0%). Clips (`rl/clips/cap_thanos/current_thanos100*.mp4`, 37M copy, four seeds): won seeds 8 (35.3% left, 873 decisions) and 10 (25.1% left, 1229); lost seeds 7 (Thanos 6.2% left, 879) and 9 (Thanos 52% left, 1406).


**cap_thanos at 42M vs everyone (eval_pool, 64 matches each, Cap P1, sampled):** Thanos 76.6%, Nightwing 57.8%, SmartAI Cap 23.4%, Doom 23.4%, Darkseid 1.6%, Venom 1.6%. He forgot: cap_scratch checks had Doom ~98%, Venom ~92%, Nightwing ~95% at 600M. Thanos-only training trades the rest away. The user is planning to merge a specialist for each opponent into one policy (planning only so far): proposed policy distillation, DAgger style, teacher picked by opponent id, then PPO fine-tune with a KL anchor.


**rl/PIPELINE.md written (2026-10-31).** The method stage by stage (general, specialist, merge, fine-tune, export), the rules with what went wrong without each, mid-run checks and known gaps. Kept current from now on.


**Toughest opponents scan (2026-10-31).** `rl/scan_toughest.py`, cap_scratch 491M as P1 vs all 151 characters, 32 matches each, SmartAI at the default level, Gotham; all ran. Ranking in `rl/toughest_scan.md` (raw `.csv`). Top 5: Thanos 0% (kept 100% life), doomsdayCG 0% (99%), GALACTUS 0% (98%), Superman 0% (78%), Darkseid 0% (48%); then Red Skull 3%, Batgirl 6%, Flash 6%, Punisher 9%, Thing 9%. Overall: 96 of 151 at 75%+, 18 under 25%, 5 at 0%. Stock Cap 100%, Doom 100%, Venom 97%, Nightwing 97%. cap_thanos at ~72M: checks vs full-life Thanos 78-89%.


**cap_thanos peaked, then keeps collapsing (2026-10-31, 4 PM, 124.5M).** The rolling win rate at full life rose to 97% at 76M and 96% at 104M (10-point dips at 68M and 100M, recovered). From about 105M: 96% to 39% to 10% within ~1.5M decisions, dealt 0.5 then 0.04, taken 0.95, time overs 28-35% (0% before). The curriculum stepped back 100% to 10%, he climbed all the way to 100% again, and it collapsed again at about 118M; at 124.5M he wins 2-3% and the curriculum is back at 30%. Cause not diagnosed (no action scan yet). kl and lr looked normal (0.007-0.009, 3e-4). Rate 11,000 decisions/s. One run uses about 3.5 GB RAM (trainer 3.1, two pools 0.16 each); 6.9 GB was free and CPU sat at 70-80%.
The user chose from scratch for the specialists and wants a scratch Cap on all 65 button choices as well. Estimate at 11,000/s: 200M 5 hours, 600M 15 hours; Cap plus 5 specialists about 40 hours in all, the same total whether they run together or in turn.


**Six from-scratch runs started (2026-10-31, about 4:40 PM).** The user wants maximum use of the PC. `train_scratch_all.bat` runs `rl/scratch_queue.py`, 3 at a time: Thanos specialist, general Cap (65 choices from the first decision, `train_cap_scratch65.bat`, `cap_scratch65.pt`, 600M), then Doomsday, Galactus, Superman, Darkseid (`train_cap_specialist.bat`, `scratch_vs_<name>.pt`, 200M, the cap_thanos recipe without --init-from). cap_thanos was stopped at 143.7M to make room; it had recovered again by then (90% rolling wins at full life, then 95% of 40 at 80%), so its pattern is repeated collapse and recovery, not a permanent collapse. Its last copy is cap_thanos_000140902400.pt.
First launches failed: this environment sets NoDefaultCurrentDirectoryInExePath, so cmd needs `.\` before a batch name. The first working launch gave the general run 3 threads on each of its 6 pools (30 threads over 16, CPU 100%, 8,700 decisions/s in all), so the trainer gained `--cores N` (`--threads 0` shares N by each pool's fights) and the queue restarted after 5 minutes: 6+6+9 threads. In those 5 minutes the Thanos specialist reached 57% wins at 20% Thanos life, and the Doomsday one 55% at 20%, dealing 3.00 (a life-bar multiple, odd: worth a look).


**Queue speed after the thread fix:** 3,164 + 3,167 + 2,733 = about 9,060 decisions/s in all, CPU 100%, RAM free 2.5 GB (page file has room), GPU 18%. So the CPU is the limit, and 3 at once is slower in total than one run alone (11,000/s for a specialist, 9,000/s for the general run): about 49 hours instead of about 40 for all six.


**Queue at 1.5 hours (2026-10-31, 6 PM).** Doomsday specialist 16.3M: reached 100% life, 81% of 200 there, last 64-match full-life check 59% (0% the check before; the 491M Cap never beat him). dealt 1.63 and up to 3.00 at the handicap: damage reads above 100%, not yet explained. Thanos specialist 16.5M: 36% at 65% life, last check 6%. General Cap 65 at 13.9M: league 48%, last check doom 25%, nightwing 25%, darkseid 5%, venom 3%, thanos 0%. No tracebacks, no collapses so far.


**Clips at about 16M (`rl/clips/scratch_1h/`, 2026-10-31).** Doomsday full life: seed 8 won in 475 decisions with 91.7% left; seed 7 lost in 975 with Doomsday at 100%. Thanos 65% life, seed 7: won in 701, 16.3% left. General Cap 13.1M vs Nightwing seed 7: lost in 832.


**Doomsday clips reviewed, engine fixed (2026-10-31, evening).** The user reported bugs in both clips. EnvColor never drew (`drawEnvColor` had no caller), and its countdown ran after the characters, so time = 1 would have ended before the frame. Now it counts down at the start of the tick and draws in Ikemen's order (System.draw). AllPalFX overwrote the fighters' own PalFX and missed helpers; now every fighter sprite stacks it when drawn (getFinalPalFx). Doomsday's hyper 3300 went from a black screen to its white, red and orange backdrop with black silhouettes. Drawing only: same match result on replay (seed 7, 975 decisions), so the running training is unaffected. Hidden suite passes on buildl_next. build\pc and the 360 build are not rebuilt, xenia_scan not run, nothing committed.
Scripted, not bugs: in hyper 3300 Doomsday and Cap leave the screen on purpose (`posset y = 230`, `screenbound value = 0`), about 26 seconds, and `lifeadd -1000` at time 1380 kills from any life (Cap had 58%). The win is a corner throw loop: Cap's throw (801, 160 damage), Doomsday lands, is regrabbed; 223 of 475 decisions in 801. Doomsday going half off the left edge is him at the stage bound (x -422) with his wide sprite past the screen edge. Doomsday regenerates (life climbs about 0.003 a decision), which is why dealt reads above 1.00.


**Queue at 2.2 hours (2026-10-31, 6:50 PM).** Doomsday specialist 25M: full-life check 94% (59, 88, 94 over the last three), 91% of 200 in training. Thanos specialist 25M: reached 100% life at about 24M with no step back (the from-Cap run got there at about 30M), last full-life check 59% (3, 6, 19, 59). General Cap 65 at 20.8M: league 58%, last check nightwing 27%, doom 25%, darkseid 12%, venom 3%, thanos 0%. No collapses, no time overs.


**General Cap 65 swapped out (2026-10-31, evening).** The user's "one scratch cap" meant Cap vs stock Cap, not a general policy. cap_scratch65 stopped at about 22M (`cap_scratch65.pt` kept, `--resume` works). `rl/scratch_queue.py` no longer tracks its own children: it counts trainers on the machine, skips a specialist that has a saved checkpoint, and can be restarted with runs going. Queue now: Thanos, Doomsday, Cap, Galactus, Superman, Darkseid. Thanos and Doomsday kept running; they stay attached to the first queue window, which must stay open.


**Doomsday clips at 26M (`rl/clips/scratch_doomsday_26M/`, full life, seeds 21-24).** Four wins: 390 decisions (100% left), 559 (84.7%), 411 (91.7%), 407 (91.7%).


**Doomsday throw loop checked (2026-10-31, evening): not an engine bug.** Three 26M replays (seeds 21-23) were each won with exactly 12 throws, 43-62% of the decisions spent in Cap's throw. Each loop: thrown, Doomsday falls guarding (132), lands, and on landing usually starts his super kick 1100 (10 of 12, 7 of 12, 10 of 12 grabs), which has about 11 ticks of startup, while Cap walks in and presses forward + z. Cap's throw needs only ctrl, P2 standing or crouching and not in a hit state, and P2BodyDist X < 10; his states and hitflag M- allow a throw during 1100's startup, and neither character has a throw break or landing invulnerability. Rules are followed; the hole is Doomsday's AI, which repeats a slow move next to Cap.


**Specialists vs Nightwing (eval_pool, 64 matches, sampled / greedy, 2026-10-31).** Doomsday 29.5M 53% / 83%; Thanos 29.5M 45% / 41%; Cap 3.3M 30% / 0%. Preview clip vs Galactus (`rl/clips/galactus_look/`, Doomsday specialist): lost in 479 decisions, Galactus 98.3% left.


**Galactus checked against the real game (2026-10-31, night).** The user asked to compare with the original: `DC vs Marvel.exe` (MUGEN 1.0) ran Cap vs Galactus by command line, AI on both sides. `data/mugen.cfg` was set to windowed and silent for the run, then restored; only that window was captured (PrintWindow), no input sent. Real Ikemen (`tools/ikemen-dc`) was run the same way.
Engine fixes, in the working tree and in buildl_next only: helpers and fighters now draw sorted together by sprpriority with Explods between them (before, a helper's children drew under it and sprpriority was ignored, so Galactus's head, second hand and team helpers were hidden behind his body); real matches seed the characters' random numbers from frames played (always 12345 before, so the same intro every match); ModifyExplod keeps sprpriority when no postype is given, which matches MUGEN 1.0 here and differs from Ikemen (floor stopped covering Cap). Test flag `--charseed N`. Hidden suite passes. All drawing except the seed, which only applies to real play, so training is unaffected.
Not bugs: each hit on Galactus costs about 1.7% (value -200 through defence 300, about 60 hits to KO); his face over the life bar is the same in the original; Cap's flag intro is one of four and works (`--charseed 101`). Open: black shadows on the floor that the original doesn't show, and his head rising in front of the floor during the intro. `train_cap_specialist.bat` now puts Galactus on GalactusOutpost, as select.def pairs them. Neither build\pc nor the 360 build has these, xenia_scan not run, nothing committed.


**Queue at 3.4 hours (2026-10-31, 8 PM).** All at full opponent life, no step-backs, no time overs. Doomsday 37M: check 94%, rolling 98%. Thanos 37M: check 75%, rolling 72% (the from-Cap run had 72% at 36M). Cap vs stock Cap 13.5M: reached full life by 5.4M, checks 64% then 59%, rolling 56%.


**Thanos specialist clips (`rl/clips/scratch_thanos_37M/`, full life, seeds 31-34, the 36M copy).** Won seed 31 (458 decisions, 68.3% left) and 34 (515, 32.4%); lost seed 32 (644, Thanos 0.8% left) and 33 (433, Thanos 28%).


**Thanos hyper background (2026-10-31, night).** The user reported a bug on his special. The original game (run windowed and silent as before, `mugen.cfg` restored) shows muted reds and purples; ours showed hot pink. Thanos's helper 8005 runs EnvColor with `value = ceil(random/1),0,ceil(random/1)`, values up to 999. `startEnvColor` clamped to 255, so it only became visible once today's EnvColor fix drew it: red and blue at full, magenta. Ikemen masks each value to its low byte; now we do too. Palette was ruled out first (`--p2pal 1` gave the same pink, though player 2's palette number still differs from the original). The Avengers-logo background in those clips is Cap's own hyper, with Thanos in Cap's states 3010-3017. Suite passes on buildl_next. Drawing only.


**Thanos super portrait (2026-10-31, night).** The user showed his face cut off behind the dark band, against the original's screenshot. Two engine fixes: Explod now reads the older `supermove` (any true value is supermovetime -1), so the portrait moves during the SuperPause instead of showing late; Explods draw lowest sprpriority first instead of in creation order, so the portrait (-1) stays over the band (-2). Suite passes on buildl_next, match result unchanged, drawing only.
Open: Thanos flickers black during his dash. Anim 3150/3151 alternates every 2 ticks with sprite 180,0, so the flicker is scripted, but that sprite decodes to normal colours with his .act (blue 14, gold 5), so drawing it black looks like a palette bug in SFF v1 sprites marked same palette. Not fixed, not yet compared with the original at that moment.


**Queue at 3.9 hours (2026-10-31, 8:30 PM).** Doomsday 41.9M: checks 94, 95, rolling 96. Thanos 42.1M: checks 75, 75, rolling 70. Cap vs Cap 19.4M: checks 64, 59, 47, rolling 57. No step-backs, no tracebacks, no time overs.


**Thanos black flicker checked against the original (2026-10-31, night).** A fast capture of the real game (403 frames, about every 90 ms, windowed and silent, `mugen.cfg` restored) shows Thanos flickering dark during this hyper there too, so the flicker is real behaviour. My palette guess was wrong: sprites 180,0 and 3150,n both share the main palette (0,0). Remaining difference: in the original the blue goes black but his gold stays; ours goes fully black. Cause not found, nothing changed in the engine for this. The capture also shows the original's backdrop going through bright reds, pinks, blues and purples, like ours since the EnvColor fix.


**Thanos black frames resolved (2026-10-31, night).** Not a bug. His sprite file holds two versions of this dash: anim 3150 in colour and anim 3151 as solid black silhouettes (sprites 3150,7 to 11), and state 3000 picks one with `anim = 3150+(random%2)`. Ours draws what the file holds. The earlier note about gold staying was a bad comparison: those original frames show him standing and firing a beam, not this dash. A test of last-value palinvertall changed nothing visible and was removed. Kept: AfterImage reads palinvertall and palcolor, first value wins as in Ikemen (Thanos's stays off). Suite passes on buildl_next.


**Queue at 4.8 hours (2026-10-31, 9:20 PM).** Doomsday 50.5M: checks 100, 92, rolling 96. Thanos 50.7M: checks 75, 75, 64, rolling 72. Cap vs Cap 29.7M: checks 47, 61, 61, 73, rolling 76. No step-backs, no tracebacks, no time overs.


**Doomsday stopped, Galactus started (2026-10-31, 9:25 PM).** The user chose to stop Doomsday at about 50.5M (checks 92-100%, flat since about 25M, mostly the throw loop) so the next run could start. Copies kept in `scratch_vs_doomsday_history/`. The queue started Galactus by itself on GalactusOutpost.


**Queue at 5.6 hours (2026-10-31, 10:10 PM).** Thanos 58.9M: checks 64, 70, rolling 82. Cap vs Cap 39.3M: checks 73, 75, 77, rolling 74. Galactus 9.1M: already at 100% life, rolling 57%, time overs 2%, first check 22% (taken earlier). Suspiciously fast for a boss with about 12x normal durability; not yet looked at. RAM free 2.8 GB. About 890M left, about 27.5 hours at 9,000/s.


**Queue at 6 hours (2026-10-31, 10:40 PM).** Thanos 63.6M: checks 64, 70, 77, rolling 82. Cap vs Cap 44.3M: checks 75, 77, 78, rolling 87. Galactus 13.9M: at 100% life, checks 22, 56, rolling 60. RAM free 1.1 GB: not the trainers (each flat at 3.1 GB private); VS Code 7.6 GB, Edge 6.9 GB in 53 processes, NVIDIA Overlay 7.2 GB private, nonpaged pool 4.7 GB. Commit free 27 GB of 88 GB, so the page file has room. About 880M left, about 27 hours.


**Galactus clips at 16M (`rl/clips/scratch_galactus/`, full life, own stage, seeds 51-54).** Won 3 of 4: seed 54 (1,057 decisions, 79% left), 52 (942, 57.9%), 51 (1,214, 39.8%); lost 53 (1,455, Galactus 10.9% left). Each match took 49-55 life drops, so no one-trick finish: Cap spends his time in jumping attacks (600, 610, 625, 650) and special 1100/1110, not a throw loop. Not watched frame by frame.


**Galactus's AI checked: power bug found (2026-10-31, 11:30 PM).** His AI runs (var(40) on, moves 215, 225, 220, 230, 105, 200), but his power stayed exactly 0 all match while the original reaches level 3 quickly. Hits come from helpers, and our engine kept their power on the helper; in Ikemen power belongs to the root (`powerOwner`). Fixed in the working tree and buildl_next: every power change and the Power trigger use the root; being hit only pays a player, not a normal helper (`isPlayerType`, `playerHelper_`). This is game logic, not drawing. Replay of the 16M copy, seeds 51-54, fixed build: power full within 150-300 decisions, Cap 2-2 (was 3-1). Move use barely changed (Nova 0.3%, The One 0% in 4 matches), so why he doesn't spend the power is unexplained. Suite passes on buildl_next. The runs on buildl_new don't have this: Galactus has trained against a powerless opponent, and Thanos and Cap vs Cap may be affected for helper hits (not measured).


**Power bug: reach checked (2026-10-31, near midnight).** Galactus's hypers: The One needs `random = 999` and Cap lying down with Galactus in control, which never overlapped in two logged fights (he has ctrl on about 1.5% of decisions); Nova needs Cap behind him, Surfer a distance of 200-300. So rare hypers are scripted, not a bug. Thanos and stock Cap on the old build (rl_new) both earn power normally (max 0.40-0.85), so neither is starved like Galactus. Same three seeds on old vs fixed build: Thanos 2-1 vs 3-0, Cap vs Cap 3-0 vs 2-1. Too few to mean anything, and rl_new (built 11:45 AM) differs from rl_next by more than the power fix, so matches diverge anyway.


**Galactus restarted on the fixed build (2026-10-31, 11:17 PM).** The user agreed. Old run kept as `scratch_vs_galactus_nopower.*` (and its history and runs folders), about 16M. `buildl_fix` is a copy of rl_next with the power fix, so rl_next stays free to rebuild. `rl/scratch_queue.py` passes `--exe` to `train_cap_specialist.bat` as an extra option, because cmd reads a batch file as it runs and editing one under a running run can upset it (the Galactus stage edit did touch it while Thanos ran; the worst case is a harmless error when that run ends). Thanos and Cap vs Cap continue on rl_new. Queue: Galactus now, then Superman and Darkseid on rl_fix.


**Cap vs Cap 82M clips (2026-11-01, 1:23 AM)** (`rl/clips/scratch_cap_82M/`, seeds 61-64, the 81.9M history copy on buildl_next, SmartAI Cap on the right at full life): won all 4, with 24.5%, 25.8%, 11.2% and 35.7% life left in 1123, 1127, 1022 and 1197 decisions. Status at 1:20 AM: Thanos 102M checks 75, 89, 86; Cap vs Cap 83M checks 75, 83, 81; Galactus 31M (full life since 5M) checks 69, 78, 70. Pace about 14M an hour each, so the queue should end around 11 PM on 2026-11-01.


**Cap vs Cap 154M clips (2026-11-01, 8:07 AM)** (`rl/clips/scratch_cap_latest/`, seeds 71-74, the 154.0M history copy on buildl_next, SmartAI Cap on the right at full life): won all 4 by KO in 195-271 decisions (about 10-14 s), 94-100% life left, against 11-36% left at 82M. Checks at 154M: 97, 83, 97, 100, 100, 100. Queue pace overnight was about 10.5M an hour, so the end moved to about 7 AM on 2026-11-02.


**Cap vs Cap's fast wins are a real move quirk, not an engine bug (2026-11-01).** At 154M he spends about 75% of a match (147 of 195 decisions, seed 73) in state 1400, Charging Star, into a blocking SmartAI Cap. On colours 1-6 Cap runs var(16) = 1, whose HitDef fires while `Hitcount < 1`. Guarded hits don't add to HitCount, in Ikemen's char.go or ours, so the HitDef re-fires every tick until a hit connects: 8 chip damage a tick, 7 hits in a scripted trace and about 15 in the match, plus meter. Colours 7+ use the var(16) = 0 HitDefs, at most 2 hits. Checked in the original game on a throwaway copy (scratchpad, game folder untouched): a patched Cap repeating the move with no input, against the original AI at level 8, took a 1029-life Cap to 0 in about 11 clock seconds, meter 0 to 3, the same picture as the clip, with 44 guarded hits counted (the per-move reset didn't fire, so that's a total over the fight). Chip per guarded hit looked nearer 10 there than our 8; not pinned down. So it's legal and exploits blocking: SmartAI keeps blocking through the gaps, where it gets control back for a few ticks. It should work on any opponent that blocks it. The move has not been tested against people. Clip checks at this level mean "solved this bot".


**Queue at 20 hours (2026-11-01, 12:55 PM).** Cap vs Cap finished 200M at 12:24 PM (last checks 94-100, rolling 98-100), Darkseid started in its slot. Thanos finished 200M at 10:25 AM (last checks 84-94, rolling 89). Galactus 146.7M: last checks 73-91, rolling 83. Superman 21.7M, from scratch (no --init-from, first log line at step 32,768, opponent life 3%): full life by about 4M, checks 45, 84, 95, 86, rolling 96. Darkseid 5.6M: at 65% life, rolling 53. Both finished runs printed a stray cmd error (`'-over' is not recognized`) after saving; their checkpoints are written. Thanos and Cap vs Cap still need testing on the fixed build.


**Queue at 23.5 hours (2026-11-01, 4:10 PM).** Galactus 178.0M: last checks 86, 78, 83, 86, 84, 73, rolling 84, flat since about 145M. Superman 50.3M: checks 84, 86, 91, 89, 97, 89, rolling 97-98. Darkseid 42.6M: at full life, checks 42, 45, 48, 59, 66, 70 and rising, rolling 96-97, the checks trailing the rolling figure.


**Galactus 177M clips (2026-11-01, 4:21 PM)** (`rl/clips/scratch_galactus_177M/`, seeds 81-83, the 176.9M history copy on build\rl_fix, SmartAI Galactus at full life on his stage): won 2 of 3. Seed 83 won with 96.7% life left (1,076 decisions), seed 82 with 47.9% (992), seed 81 lost (931). Not watched frame by frame.


**Teacher-student merge built, not run (2026-11-01).** The user chose it after the SOTA talk. `train_pool.py --teachers FILE`: a fresh student plays, the teacher for each opponent gives move probabilities stored during the rollout, and KL(teacher || student) on both heads sits on top of PPO with the same reward (MOPD style). Each teacher is measured over 256 matches at the start; its pull fades 0.995 an update once the student's last 200 matches are within 5 points, floor 0.02, and climbs back below that. Files: `rl/pick_teachers.py` (writes `rl/merge_teachers.txt`), `train_cap_merge.bat` (12 opponents, 300M, `--target-kl 0`, entropy 0.003). The 491M general Cap (8 buttons) is the teacher for everyone else, with the other 57 choices padded as never pressed. Smoke tests on build\rl_fix beside the queue: a student started as a copy of Thanos's teacher showed KL 0.00, 0.01, 0.02, so the teacher feed matches play; a blank student on Thanos + Doom (32 fights) went from KL 2.5/5.7 to 2.2/3.1 and took its first wins off Doom within 26k decisions; resume keeps scores and pulls. The picker ran end to end at 4 matches. Not tested: the pull fading (needs 100 finished matches per opponent) and the 12-pool launcher itself (its opponents are all in the roster). PIPELINE stage 3 rewritten. Queue untouched; run after it ends, Monday morning.


**Galactus finished (2026-11-01, 6:27 PM).** 200M on build\rl_fix: last checks 84, 73, 83, 80, 75, 86, flat in the low 80s since about 145M. Superman 70.9M: checks 97, 89, 92, 94, 95, 89, rolling 97. Darkseid 70.0M: checks 42-70 at 43M climbed to 97, 100, 100, 100, 98, 98, rolling 98. How Darkseid gets there hasn't been looked at.


**Queue stopped for the merge (2026-11-01, 7:05 PM).** The user's call: "They all trained up". Superman stopped near 80M (last checks 95, 89, 88, 92) and Darkseid near 82M (98, 98, 97, 100), both saved cleanly at their last update. `rl/pick_teachers.py` started on the free CPU (log `rl/checkpoints/pick_teachers.log`); `train_cap_merge.bat` follows when it writes `rl/merge_teachers.txt`.


**Teachers picked, merge started (2026-11-01, about 7:30 PM).** `rl/pick_teachers.py`, 256 matches each on build\rl_fix: Thanos 101.6M 92.6% (final 89.1%), Doomsday 42.6M 98.4%, Cap vs Cap final 100%, Galactus final 86.7%, Superman 78.6M 96.5%, Darkseid 62.3M 99.6%. Noise is about 3 points, so the small gaps between copies mean little. Thanos and Cap vs Cap on the fixed build: no collapse. Then `train_cap_merge.bat`: fresh student, checkpoint `rl/checkpoints/cap_merged.pt`, log `cap_merged.log`.


**Merge crashed at its first step, trainer bug fixed, restarted (2026-11-01, 7:40 PM).** With several pools the teacher's next memory has to travel in the `pending` tuple with the pool's other state; it didn't, so every pool took the last pool's. Uneven pools crashed (`cap_merged_crash1.log`). The earlier blank-student smoke (Thanos + Doom, 16/16) didn't crash but mixed the two teachers' memories, so its KL numbers are not valid; the one-opponent copy test was. Fixed. New test on three uneven opponents (12/11/11), student a copy of Thanos's teacher: Thanos KL 0.00, Doom 7.88, Red Skull 7.74 at the first update, falling to 5.84 and 5.21 in four updates while Thanos rose to 0.54 as the shared network moved. Each teacher measured at the merge's start, 256 matches: Thanos 89, Doomsday 99, Cap 99, Galactus 82, Superman 97, Darkseid 99, Doom 99, Nightwing 97, Venom 99, Red Skull 11, Flash 9, Joker 20.


**Merge past its crash point (2026-11-01, 7:45 PM).** 295k decisions in, no errors, 6,627 decisions/s (about 24M an hour, so 300M about 8 AM on 2026-11-02). Teacher KL already 0.8-2.6 across the twelve (Doom 5.70 to 2.63, Joker 5.84 to 2.36). First wins seen against Doom and Nightwing, on a handful of matches.


**Merge restarted on the six specialists only (2026-11-01).** The user: "I want to train against specialists only". The six extra opponents (Doom, Nightwing, Venom, Red Skull, Flash, Joker, general Cap teaching) were my addition and are gone. The 12-opponent run, a few minutes old, is kept as `cap_merged_12opponents.*`. Fresh student, same settings, same 300M steps, so each opponent now gets 1/6 of the fights instead of 1/12.


**Merge at 49M: all six in one network (2026-11-01, 9:35 PM).** Six-specialist run, no errors, 8,500 decisions/s (about 30.6M an hour, so 300M about 5:45 AM on 2026-11-02). Rolling wins vs each teacher's measured rate: Thanos 86 (89), Doomsday 95 (99), Cap 96 (99), Galactus 82 (82), Superman 94 (97), Darkseid 96 (99), so all within the 5-point margin. Pulls have faded on their own: Cap and Darkseid 0.02 (the floor), Galactus 0.03, Doomsday 0.06, Superman 0.12, Thanos 0.17. The fade works as built. Checks (64 matches, about 6 points of noise) at 9.8M / 19.6M / 29.5M / 39.3M / 49.1M: Thanos 88/91/84/72/88, Doomsday 92/95/95/94/100, Cap 94/97/95/98/95, Galactus 89/86/77/67/72, Superman 97/95/97/94/92, Darkseid 97/95/95/97/92. Galactus is the one to watch: checks have slid from 89 to about 70 while the rolling number holds at 82.


**Merged 49M clips (2026-11-01, 9:41 PM)** (`rl/clips/merged_49M/`, seed 91 for all six, one take each, the 49.15M history copy on build\rl_fix, Galactus on his own stage, SmartAI at full life): won 6 of 6. Life left and decisions: Thanos 76.3% (522), Doomsday 94.8% (418), Cap 17.7% (945), Galactus 82.5% (1,031), Superman 84.6% (882), Darkseid 100% (290). Not watched frame by frame.


**Superman specialist clip (2026-11-01)** (`rl/clips/scratch_superman_78M/`, seed 91, the 78.6M history copy that teaches the merge, on build\rl_fix, SmartAI Superman at full life): won with 70.6% life left in 723 decisions. Same seed as the merged 49M clip, which won with 84.6% in 882. Not watched frame by frame.


**Queue for all 151 specialists, 95% stop (2026-11-01, 11 PM).** The user: "I want to have all 151 specialists for cap, and then unify them all into one network", and to keep choosing and training while away. `train_pool.py` gained `--stop-win` / `--stop-checks`: the run ends after two checks in a row (64 matches, full life bar whatever the curriculum step) at or above the mark; tested, a Darkseid copy with the mark at 50% stopped at its second check. `train_cap_specialist.bat` now uses build\rl_fix and `--stop-win 0.95`, 200M stays the limit. `rl/scratch_queue.py` runs everyone left in `toughest_scan.csv` order (Red Skull, Batgirl, Flash, Punisher, Thing, Joker, Agent Venom, Firestorm, Zatanna, Invisible Woman, ...), three at a time, starts nothing while `cap_merged` runs, and starts nothing below 30 GB free. 145 to do at the start, plus Ivy. Started, waiting on the merge (about 6 AM).


**Poison Ivy move check (2026-11-01).** Inputs played through the RL server at the network's speed (a decision every 3 frames) and action set, against an idle or blocking Cap, state numbers read from the observation. Reached: normals 200, 210, 220, 280, 400, 410, 420, air 600, 610, 620, dashes 100, 105, super jumps 700, 702, 704, throws 800, 850, taunt 196, specials 1000 (qcf), 1100 (qcb), 1200 (dx3 then a punch), 1300 (c, gated on var(50) >= 600), hypers 3200 (qcf x2), 3400 (qcb x2), guard push (standing, went past state 150, 151 into a clamped 6000+ state, so 8011). Not proven: 3100 (needs power 3000, a round ended at 61% meter), crouch and air guard push (same x+y input as the standing one). States 230-250, 430-450, 630-650, 1050, 1250 and 1400 have only `var(59)` AI triggers: no human can do them either. The CPU1-30 commands that set var(59) and lock out !Var(59) moves are `time = 1`, impossible for any input, so the RL player can't switch them on; tested, typing U,D,F left her idle. Ivy's run: `scratch_vs_ivy_vs_cap`, Ivy as P1 from scratch vs SmartAI Cap, same recipe, first in the queue. Her moves' inputs go through helper 10371's input buffer, and it works at this speed.


**Ivy run started early (2026-11-02, 12:55 AM).** The user asked to see her fight, so `scratch_vs_ivy_vs_cap` started beside the merge instead of waiting for it; the queue was restarted once her checkpoint existed, so it won't start her twice. Her log's Cap-named opponent column is SmartAI Cap, her opponent. She passed the 3%, 6% and 10% curriculum steps in her first 230k decisions. Merge at 147M: average of the last 7 checks Thanos 86, Doomsday 93, Cap 93, Galactus 79, Superman 92, Darkseid 96, flat since about 10M, 2-6 points under every teacher.


**Ivy first clip (2026-11-02, 12:57 AM)** (`rl/clips/ivy_early/`, seed 91, about 300k decisions in, SmartAI Cap at full life): won by KO with 43.9% life left in 854 decisions, one sample, not watched frame by frame. Surprising that early, so don't trust it until the checks at full life agree.


**Overnight (2026-11-02, 8:13 AM).** Ivy stopped at 95% twice in a row at 14.7M (2:02 AM); her full-life checks rose 83, 97, 95, so the early clip was real: SmartAI Cap is easy for her. The merge reached its 300M limit at 6:23 AM: last three checks Thanos 72/80/86, Doomsday 83/97/95, Cap 97/98/97, Galactus 83/88/75, Superman 89/89/88, Darkseid 92/100/98; still about level with its teachers, not past them. The queue then started Red Skull, Batgirl and Flash; Red Skull hit 95% (100, 100) at 14.7M in 80 minutes and Punisher took its slot. At 8:13 AM: Batgirl 20M, opponent life only 65%, full-life checks 16-17%; Flash 20M at full life, checks 33 then 80; Punisher 5.5M, 80% life. About 3,150 decisions/s each with three running.


**Ivy final clips (2026-11-02, morning)** (`rl/clips/ivy_final/`, final 14.7M checkpoint, SmartAI Cap at full life, `--exe build/rl_fix/mugen360.exe`): seed 91 won with 79.6% life (665 decisions), seed 92 won with 54.6% (623), seed 93 won with 83.5% (388). A first try with record_clip's default exe, `build/pc_test` (built 2026-10-28, older than rl_fix), lost seed 91 (Cap kept 88%); deleted. Record clips with the training build: pc_test predates the rl_fix fixes.
