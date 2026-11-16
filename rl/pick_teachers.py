"""Chooses each opponent's teacher for the merge (rl/PIPELINE.md, stage 3): the final network and evenly spaced copies from
the later half of its run each play --matches against that opponent, and the best goes in rl/merge_teachers.txt. A
specialist's last copy isn't always its best (cap_thanos collapsed after its peak), and a 64-match check is too noisy to
choose by. Everyone else gets the general Cap (the line for *). Run it with no training going: it uses every core.

    rl/.venv/Scripts/python.exe rl/pick_teachers.py
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path

import numpy as np
import torch

from eval_pool import play
from model import policy_from_checkpoint
from pool_env import MugenPool
from roster import discover

SPECIALISTS = [("thanos", "chars/thanos/thanos.def"), ("doomsday", "chars/doomsdayCG/doomsdayCG.def"),
               ("cap", "chars/captainamerica/captainamerica.def"), ("galactus", "chars/GALACTUS/GALACTUS.def"),
               ("superman", "chars/Superman/Superman.def"), ("darkseid", "chars/darkseid/darkseid.def")]
GENERAL = "rl/checkpoints/cap_scratch_league/snap_000491520000.pt"
STAGES = {"galactus": "stages/GalactusOutpost.def"}  # as train_cap_specialist.bat


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/rl/mugen360.exe")
    parser.add_argument("--p1", default="chars/captainamerica/captainamerica.def")
    parser.add_argument("--copies", type=int, default=6, help="history copies tried per specialist, besides the last")
    parser.add_argument("--matches", type=int, default=256)
    parser.add_argument("--frame-skip", type=int, default=3)
    parser.add_argument("--seed", type=int, default=360)
    parser.add_argument("--out", default="rl/merge_teachers.txt")
    args = parser.parse_args()
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    rng = np.random.default_rng(args.seed)
    roster = discover(args.game)
    ids = {path: i for i, path in enumerate(roster.characters)}
    lines = ["# opponent | checkpoint | stage. Written by rl/pick_teachers.py."]
    for name, opponent in SPECIALISTS:
        last = Path(f"rl/checkpoints/scratch_vs_{name}.pt")
        history = sorted(Path(f"rl/checkpoints/scratch_vs_{name}_history").glob("*.pt"))
        history = history[len(history) // 2:]
        spaced = [history[k] for k in np.linspace(0, len(history) - 1, min(args.copies, len(history))).astype(int)]
        stage = STAGES.get(name, "stages/gotham.def")
        best, best_rate = None, -1.0
        for path in spaced + [last]:
            saved = torch.load(path, map_location=device, weights_only=False)
            policy = policy_from_checkpoint(saved).to(device)
            with MugenPool(args.exe, args.game, args.p1, opponent, stage, args.matches, os.cpu_count() or 8,
                           args.frame_skip) as pool:
                result = play(pool, policy, device, ids[args.p1], ids[opponent], args.seed, 2500, rng)
            rate = float(np.mean(result == 0))
            print(f"{name}: {path.name} {rate:.1%}", flush=True)
            if rate >= best_rate:  # a tie goes to the later copy
                best, best_rate = path, rate
        print(f"{name}: picked {best.name} at {best_rate:.1%}", flush=True)
        lines.append(f"{opponent} | {best.as_posix()} | {stage}")
    lines.append(f"* | {GENERAL}")
    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
