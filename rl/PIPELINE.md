# The training pipeline

How to make a neural fighter for one character, as we currently understand it. This is the method;
`TRAINING_HISTORY.md` is the diary of what happened and when. Update this file whenever a run changes the method.
Numbers come from logs and 64-match evals.

The goal is one policy per character that wins by KO, wins quickly, plays either side, can use every move, and beats
every opponent. Today each stage below is run by hand. The aim is to join them into one run.

## The user's plan (2026-10-31, not started)

1. Find the 5 toughest AI opponents in the game and rank them. Done: `rl/toughest_scan.md` (Thanos, doomsdayCG, GALACTUS, Superman, Darkseid; boss characters may need a look first).
2. Train a separate Cap specialist against each of the 5, one policy per opponent, each from scratch (the user's
   choice, 2026-10-31).
3. Train a sixth specialist from scratch against stock Cap. The general teacher for everyone else in the merge is the
   491M Cap (8 buttons) for now.
4. Merge the specialists into one policy (stages 3 and 4 above).

## The stages

| # | Stage | Launcher | Status |
|---|---|---|---|
| 1 | General policy from scratch | `train_cap_scratch.bat` | done for Cap, 600M; a 65-button rerun (`train_cap_scratch65.bat`) was stopped at about 22M |
| 2 | Specialist for each opponent it can't beat | `train_cap_specialist.bat`, queue `train_scratch_all.bat` | running from scratch, 3 at a time |
| 3 | Merge the policies into one (distillation) | not built | planned |
| 4 | Fine-tune the merged policy against everyone | not built | planned |
| 5 | Export to the game | `rl/export_policy.py` | works, but `build\pc` needs a rebuild for 65 buttons |

### 1. General policy from scratch

A fresh network, 512 fights, 600M decisions (about 18 hours at 9,000 decisions/s).

- 384 fights are the mirror: the current policy (35%) or a snapshot picked by PFSP, with a snapshot every 300 updates
  and 16 kept. Random sides.
- 128 fights are the SmartAI opponents, so it doesn't overfit to itself.
- Entropy 0.003, gamma 0.9995, no style codes.

Result for Cap: Doom 98%, Nightwing 95%, Venom 92%, Thanos 0%, Darkseid 0%. The best copy was at about 490M. After
about 570M the mirror drifted into time overs (12-17%) and the league win rate fell from 71% to 53%, so take the best
snapshot, not the last one.

### 2. Specialist for an opponent it can't beat

Start from the best general snapshot. All 512 fights are that opponent, with a life handicap.

- `--curriculum 0.03,0.06,0.1,0.15,0.2,0.3,0.4,0.5,0.65,0.8,1 --curriculum-win 0.6 --curriculum-back 0.2`: the
  opponent's life bar grows when he wins 60% of 200 matches at a step, and shrinks when he wins under 20%.
- `--gamma 1.0`, `--entropy-coef 0.01`, `--lr 3e-4 --lr-max 3e-4`, `--keep-every 100`.

Result for Cap vs Thanos (started from the general snapshot): 0% to 77% in 42M decisions (about 1 hour), 95-97% from
76M to 105M. Then it swung between 0-3% and 90%+ (cause not found), so stop at the peak and
use the best `--keep-every` copy, never the last one. It costs everything else: at 42M he had Doom 23%,
stock Cap 23%, Venom 2%, Nightwing 58%. A specialist is a teacher for stage 3, not a final policy. The same recipe is
the next thing to try for Darkseid.

### 3. Merge (built, not run yet)

Weights can't be averaged: units in two networks don't line up. Distillation trains a student to copy the teachers. This
is the on-policy kind with reward (MOPD), so the student can pass its teachers instead of only copying them.

1. `rl/pick_teachers.py` (nothing else running) tries the last network and 6 copies from the later half of each
   specialist over 256 matches, and writes the best to `rl/merge_teachers.txt`. Its `*` line (the 491M general Cap, 8
   buttons, the other 57 choices counted as never pressed) only applies to opponents without a specialist; this run
   has none.
2. `train_cap_merge.bat` starts a fresh student against the six specialists' characters only, the user's choice.
   Galactus on his own stage.
3. The student plays every match. The teacher for that opponent gives its move probabilities at each decision, stored
   during the rollout, and the student minimises KL(teacher || student) on both heads on top of PPO with the same
   +1/-1 reward. The value head learns from reward from the start.
4. Each teacher is measured over 256 matches at the start. When the student's last 200 matches are within 5 points of
   that rate the pull fades (0.995 an update, floor 0.02); below it the pull climbs back. `--target-kl 0`, because the
   early jump toward the teachers would otherwise cut the learning rate.
5. Done when each score is within about 5 points of its teacher. Watch `teachers/<name>_kl`, `teachers/<name>_pull`, and how often each move is used (Charging Star may spread).

The network already takes the opponent's id as input, which is what lets one network hold several plans. Open: against
an unseen character it may fall back on one plan, and all of this is as player 1 (fights don't change sides yet).
An offline head start from the `_history` copies (REGEN style) isn't built; try it only against a blank student.

### 4. Fine-tune against everyone (planned)

PPO on the merged policy against all opponents and the league, with a KL penalty to the teachers that fades out, so
it improves without forgetting. `--anchor` does this for one teacher; it needs extending to one teacher per opponent,
plus a fade. Mix in Thanos-style opponents at full life from the start, and keep entropy at 0.003 (0.01 made a
multi-opponent run collapse to random before; it has only been tested on single-opponent runs).

### 5. Export

`rl/export_policy.py` writes a `.m3nn`. A 65-button policy needs a game build that includes the 65-button `rlPad`.

## Rules that apply to every stage

| Rule | Setting | Why (what went wrong without it) |
|---|---|---|
| Win/loss only | `--damage-dealt-weight 0 --damage-taken-weight 0` | The user's choice: no reward shaping |
| Only a KO wins | `--time-over loss` | The game gives time over to whoever has more life, so he learned to lead and run the clock: 54% time overs, 1,875-decision matches |
| Quick wins are worth more | `--win-time-cost 0.0004` | `--step-penalty` also made a quick loss better than a slow one. Charge wins only |
| A draw ends the match | engine, `StageDemo::trainingOver` | A drawn round started another round with life refilled, so stalling cost nothing |
| Mirrored P2 view | engine, `rlObservation(..., mirror)` | The net had to learn each side separately: 92%/28% side gap and the left-wall camping |
| Random sides in the mirror | `--random-sides` | Same side gap |
| Keep SmartAI in the mix | `--league` below `--fights` | Mirror-only training forgot everyone else: Thanos, Venom and Darkseid all 0% |
| Every move is reachable | engine, `RL_BUTTONS = 65` | One button per decision made two-button hypers and throws impossible |
| No discount when it keeps losing | `--gamma 1.0` | With gamma below 1 a later loss costs less, so a losing policy turtles: held Back on 100% of decisions at 50% Thanos life |
| Never an unwinnable fight | `--curriculum`, `--curriculum-back` | With no wins every match is -1 and there's nothing to learn: 0 of 15,000 matches, 2% damage |
| Keep copies | `--keep-every 100`, league snapshots | The peak of the collapsed run was lost: no copy existed |

## Mid-run checks

Read these from the log line or TensorBoard:

- `win100`, and `opponent life X%: won Y%` on a curriculum.
- `dealt/taken`. Near 0.02/1.00 means it has given up or collapsed.
- `timeover=`. Rising means stalling.
- `as P1/P2`. A gap means a side problem.
- `last check`: SmartAI opponents at full life, always the real score.
- Episode length. Rising means slower fights.

If wins fall suddenly, scan what it presses (share of each direction and button, entropy). One direction near 100% is
a collapse. A clip shows it faster than the graphs.

## Evaluating

- Check the opponent earns power: a bar stuck at 0 means its fight is wrong (Galactus, helper hits).
- `rl/eval_pool.py`, 64 matches per opponent, with Cap as P1 only. P2 isn't tested yet.
- Evaluate against every opponent, not only the one a run trains on.
- `rl/record_clip.py` for clips, with `--p2-life` for a handicap level. One clip is one sample; use several seeds.
- Copy a live checkpoint before reading it (`cap_thanos_clip.pt`), and never run `build\rl_new` builds during training:
  the exe is locked. `build\rl_next` is the spare.

## Known gaps

- `--time-over` reads the clock from the observation, so `timed_out` depends on observation -3.
- Style codes are unused in the current recipe (`--skills 0`). The user wants varied, fun play, so they may come back.
- The previous-button input shows only the first button of a combo.
- The specialist collapse at about 105M is undiagnosed; a from-scratch specialist may hit it too.
- Compare a boss character with the real game before trusting a clip: Ikemen and MUGEN 1.0 disagreed on Galactus's floor.
- Only Cap has gone through this recipe. Thanos has the older league policy, and his launcher still uses old settings.
- Clip and eval tools run the SmartAI as player 2 only.

## All 151 specialists (the user's plan, 2026-11-01)

`train_scratch_all.bat` (`rl/scratch_queue.py`) trains a from-scratch Cap specialist against every character, toughest first,
three at a time, each until two checks in a row win 95% at full life, or 200M steps. It waits for `cap_merged` and for 30 GB free.
It can be restarted any time: saved and running specialists are skipped. Then a merge of all 151, which needs 
`rl/pick_teachers.py` extended past its six and a look at running 151 pools at once.
