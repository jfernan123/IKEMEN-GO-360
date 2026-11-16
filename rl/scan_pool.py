"""How strong SmartAI is with each character: mirror matches against uniformly random inputs, in an RL pool (so
explods work, unlike the first headless scan). Ranks characters by SmartAI's win rate, then by the life it keeps."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np

from pool_env import MugenPool


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/rl/mugen360.exe")
    parser.add_argument("--characters", required=True, help="comma-separated, or @file with one per line")
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--matches", type=int, default=32)
    parser.add_argument("--max-decisions", type=int, default=2500)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--out", default="rl/reports/smartai_pool_scan.csv")
    args = parser.parse_args()
    names = [l.strip() for l in Path(args.characters[1:]).read_text().splitlines() if l.strip()] if args.characters.startswith("@") else \
        [c.strip() for c in args.characters.split(",") if c.strip()]
    rng = np.random.default_rng(7)
    rows = []
    for name in names:
        try:
            with MugenPool(args.exe, args.game, name, name, args.stage, args.matches, args.threads) as pool:
                obs = pool.reset(9000)[:, 0]
                other = (obs.shape[1] - 5) // 2
                result = np.full(args.matches, -2)
                smart_life = np.zeros(args.matches, np.float32)
                length = np.zeros(args.matches, np.int64)
                for step in range(args.max_decisions):
                    playing = result == -2
                    smart_life[playing] = obs[playing, other]
                    actions = np.zeros((args.matches, 4), np.uint8)
                    actions[:, 0] = rng.integers(0, 9, args.matches)
                    actions[:, 1] = rng.integers(0, 8, args.matches)
                    _, done, winner, both = pool.step(actions)
                    obs = both[:, 0]
                    finished = playing & done
                    result[finished] = winner[finished]
                    length[finished] = step + 1
                    if (result != -2).all():
                        break
                open_ = result == -2
                length[open_] = args.max_decisions
        except RuntimeError as error:
            print(f"{name}: {error}", flush=True)
            continue
        row = {"character": name, "smart_win": float(np.mean(result == 1)), "smart_life_left": float(smart_life.mean()),
               "decisions": float(length.mean()), "unfinished": int(open_.sum())}
        rows.append(row)
        print(f"{Path(name).stem:28s} SmartAI wins {row['smart_win']:4.0%}  keeps {row['smart_life_left']:4.0%} life  "
              f"{row['decisions']:5.0f} decisions  unfinished {row['unfinished']}", flush=True)
    rows.sort(key=lambda r: (-r["smart_win"], -r["smart_life_left"]))
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
