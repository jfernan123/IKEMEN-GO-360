# Marvel roster reinforcement learning

The method, stage by stage: [PIPELINE.md](PIPELINE.md). What happened, run by run: [TRAINING_HISTORY.md](TRAINING_HISTORY.md).

This pipeline trains one recurrent PPO policy for the mounted Marvel MUGEN game. Eight hidden fights run in parallel
by default, and each episode uses two randomly
selected playable characters and a random stage. The policy receives both character identities, life and power,
position and velocity, state and move type, control/guard/hit status, previous input, and the exact world coordinates
of up to four current attack boxes and four hurt boxes for each fighter.

The action is a relative direction and one of the six MUGEN buttons, Start, or no button, held for three engine ticks.
The engine still runs the character's CMD and CNS files, so the network has to learn motion inputs, timing, defense,
combos, and matchup strategy through normal controls.

Run `setup_rl.bat` once. Then train with:

```
train_marvel_ai.bat "C:\path\to\the\Marvel MUGEN game"
```

Because this workspace already has `DC vs MARVEL 2025` beside the executable, `train_marvel_ai.bat` with no argument
uses that game automatically.

`train_marvel_ai_from_scratch.bat` starts a randomly initialized SmartAI-only run under
`rl/checkpoints/marvel_policy_scratch.pt`, with wins under `rl/wins_scratch`. It does not pass `--resume` and leaves the
main `marvel_policy.pt` checkpoint untouched. Running the scratch launcher again intentionally replaces the scratch
policy after its first PPO update; use `resume_marvel_ai_scratch.bat` to continue the scratch checkpoint.

The default curriculum fights only the current SmartAI until the deterministic policy wins at least 85% of the fixed
100-match evaluation. Self-play remains available through `--self-play-chance .35` after that qualification if wanted.
The latest checkpoint is `rl/checkpoints/marvel_policy.pt`. Checkpoints preserve the policy, optimizer, decision and
episode counters, rolling 100-episode statistics, and frozen self-play opponent pool across restarts.

Training writes live TensorBoard runs under `rl/runs`. Start `tensorboard_rl.bat` while training, then use the browser
dashboard at `http://localhost:6006`. It charts return and win rate, episode length, policy/value loss, entropy,
approximate KL, clipping, gradient norm, explained variance, action distributions, throughput, self-play usage, and
GPU memory. The Text dashboard's `actions/top_sequences_2`, `_3`, and `_4` tables show the most common contiguous
relative-input sequences from every PPO rollout. Scalars beside them show the leading sequence's share and the number
of unique sequences; histories continue across rollout boundaries but reset at the end of each match.

Four background probe matches keep separate rolling 100-match counters under TensorBoard's `matchups/` group and on
each console status line: deterministic policy vs SmartAI, current policy vs a frozen policy snapshot, policy vs
random controls, and SmartAI vs random controls. The probes use frozen CPU policy copies and never enter PPO's reward
or gradients. SmartAI is player 2 in its random-control probe; its result is counted from player 2's perspective.

Every completed win is saved under `rl/wins` as replay metadata and controller scripts. Run `replay_last_win.bat` to
watch the newest one with full Marvel graphics and sound, or pass a particular JSON file to watch an older win. SmartAI
replays preserve its random seed; self-play wins preserve both policies' actual controller inputs. Training launched
through `train_marvel_ai.bat` resumes `marvel_policy.pt`, so it can be restarted to enable replay capture without losing
the learned weights.

Use `--envs 12` or `--envs 16` when the PC can be devoted to training. Eight leaves CPU time for other work. The
fight simulations always run on the CPU; `setup_rl.bat` installs CUDA PyTorch when it finds an NVIDIA GPU so batched
policy inference and PPO updates run on the GPU. Pass `--device cpu` or `--device cuda` to compare them explicitly.

Evaluate the deterministic policy and uniformly random controls on the same 100 matchups and seeds:

```
rl\.venv\Scripts\python.exe rl\evaluate.py --game "C:\path\to\the\Marvel MUGEN game"
```

The paired result is the useful anti-mashing check: policy win rate and mean return should beat the random baseline.
Use `--agent policy` or `--agent random` to run only one side of the comparison.
For the bundled Marvel game, double-click `evaluate_marvel_ai.bat`, or pass a shorter match count while checking the
pipeline: `evaluate_marvel_ai.bat 10`. The default is 100 paired matchups. Each policy and random-baseline phase shows
an updating progress bar with completed matches, wins, draws, losses, mean return, elapsed time, and ETA.
Evaluation runs eight matches simultaneously by default. The batch file's optional third argument changes that count;
for example, `evaluate_marvel_ai.bat 100 "DC vs MARVEL 2025" 4` uses four environments.
The default qualification target is 85%. A successful evaluation saves the exact policy it tested as
`rl/checkpoints/marvel_policy_qualified.pt`. A fourth batch argument changes the target, such as `0.90` for 90%.

Measure how many simultaneous hidden fights the PC can sustain:

```
rl\.venv\Scripts\python.exe rl\benchmark_envs.py --game "C:\path\to\the\Marvel MUGEN game"
```

Training is PC-only. The checkpoint can later be exported to a compact C++ inference format for the Xbox build after
the policy has learned enough to justify freezing its network shape.

## RL pool (hundreds of fights at once)

`train_pool.py` trains on one matchup with `mugen360 --rl-pool N`: N headless fights in one process that load each
character once, step on every core and talk to Python through a binary protocol (`pool_env.py`; the protocol is
described above `runRlPool` in `src/main.cpp`). Two pools take turns so the GPU picks one pool's actions while the
other simulates. With 512 fights it trains at about 11,000 decisions/s on this PC (the per-process trainer manages
about 1,600) in about 300 MB. Each match rebuilds its fight from the shared characters, so a seed always plays out
the same way.

```
build_rl.bat
train_pool_cap_vs_thanos.bat
```

Options: `--fights`, `--pools`, `--rollout` (decisions per fight per update), `--size small|large`, `--bptt`,
`--target-kl` (adaptive learning rate), `--init-from`, `--resume`. Checkpoints use the same format as `train_ppo.py`,
so `record_clip.py` and `watch_win.py` work on them.
