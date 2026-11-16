# Neural fighters (reinforcement learning)

Trains neural-network fighters for a MUGEN game (today: Captain America in `DC vs MARVEL 2025`) inside mugen360, and
exports them for the game's VS NEURAL CAP mode. The policy plays only through controller inputs, so the character's
own CMD and CNS files decide what each press does.

- [PIPELINE.md](PIPELINE.md): the method, stage by stage, and the rules every run follows.
- [TRAINING_HISTORY.md](TRAINING_HISTORY.md): the diary, run by run. Add to it after every training step.

## Layout

| Path | What |
|---|---|
| `scripts/` | The launchers (below). Each one changes to the repository root first, so run it from anywhere. |
| `train_pool.py` | The trainer: recurrent PPO on `mugen360 --rl-pool`, self-play league, curriculum, distillation from teachers, checks against SmartAI. |
| `model.py` | The network, and checkpoint surgery (`add_skills`, `add_buttons`, `widen_observation`, ReDo). |
| `pool_env.py` | Talks to `mugen360 --rl-pool N` (N headless fights in one process, binary protocol described above `runRlPool` in `src/main.cpp`). |
| `mugen_env.py` | Talks to the single-fight RL server (`--rl-server`); used by the tools that show or record one match. |
| `roster.py` | Finds a game's playable characters and stages. |
| `eval_pool.py` | Win rates against SmartAI opponents, with a random-input baseline. |
| `eval_versus.py` | Two checkpoints head to head, each as player 1 for half the matches. |
| `pick_teachers.py` | Picks each opponent's best specialist copy for the merge, into `merge_teachers.txt`. |
| `scratch_queue.py` | Trains a specialist against every character, three at a time (`scripts/train_scratch_all.bat`). |
| `export_policy.py` | Checkpoint → `.m3nn`, the format `src/core/ai/neural_policy.cpp` runs in the game. |
| `record_clip.py`, `watch_live.py` | An MP4 of one match, or matches live in the game window. |
| `scan_toughest.py`, `scan_pool.py`, `scan_smartai.py` | Rank opponents by difficulty, and SmartAI's strength per character. |
| `style_report.py` | How each style of a skill-trained policy plays. |
| `bughunt/` | Records fights and cuts them into contact sheets for a visual bug review (`bughunt/REVIEW.md`). |
| `reports/` | Scan results: the toughest opponents, SmartAI per character, why some SmartAIs are weak. |
| `checkpoints/`, `runs/`, `clips/`, `wins_*/`, `.venv/` | Local only (gitignored): networks and logs, TensorBoard, videos, old replays, Python. |

## Setup and builds

```
rl\scripts\setup_rl.bat      the Python environment in rl\.venv (CUDA PyTorch when there's an NVIDIA GPU)
build_rl.bat                 build\rl\mugen360.exe, the optimized headless engine every trainer and tool uses
build_pc.bat                 build\pc\mugen360.exe, with graphics: record_clip.py and watch_live.py use it
```

Windows locks a running .exe: rebuild `build\rl` only while no training is running.

## Launchers (`rl\scripts\`)

| Launcher | Pipeline stage |
|---|---|
| `train_cap_scratch.bat` | 1. General Cap from scratch (mirror league + SmartAI opponents) |
| `train_cap_specialist.bat <name> <def> <threads>` | 2. One specialist against one opponent, with a life curriculum |
| `train_scratch_all.bat` | 2. The specialist queue for the whole roster. Restart it any time: saved and running specialists are skipped |
| `train_cap_merge.bat` | 3. Distil the specialists into one network (run `pick_teachers.py` first) |
| `tensorboard_rl.bat` | Live charts at http://localhost:6006 |
| `watch_live.bat` | The merged Cap fighting live (`--p2 chars/doom/doom.def` picks the opponent) |

Each training run writes `rl\checkpoints\<run>.log` and `rl\runs\<run>\`. Pass `--resume` to continue a run.

The launchers of earlier experiments (the one-process trainer `train_ppo.py`, the Cap/Thanos leagues, main agents,
mirrors, style runs) were removed in the cleanup after PR #34; `TRAINING_HISTORY.md` describes each of them, and git
history has their files.

## Into the game

```
rl\.venv\Scripts\python.exe rl\export_policy.py rl\checkpoints\cap_merged.pt data\neural\cap_merged.m3nn
```

Then point a `data\neural\neural_*.ini` at it (see `neural_cap.ini`), copy it over `data\neural\neural.ini` and start
the game: VS NEURAL CAP plays it as player 1.
