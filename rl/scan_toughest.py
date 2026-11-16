"""Ranks the roster's AI opponents by how hard they are on one policy: the policy plays player 1 against each character
(SmartAI, as in training) on one stage, and the toughest are the ones it beats least, then the ones that keep the most
life. Writes a CSV as it goes (a stopped scan resumes from it) and a Markdown ranking at the end.

    python rl/scan_toughest.py --checkpoint rl/checkpoints/cap_scratch_league/snap_000491520000.pt \
        --p1 chars/captainamerica/captainamerica.def --out rl/toughest_scan
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np
import torch

from eval_pool import play
from model import policy_from_checkpoint
from pool_env import MugenPool

FIELDS = ["character", "win_rate", "opponent_life", "own_life", "error"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/rl_next/mugen360.exe")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--p1", required=True)
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--matches", type=int, default=32)
    parser.add_argument("--max-decisions", type=int, default=2500)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--seed", type=int, default=4242)
    parser.add_argument("--out", default="rl/toughest_scan", help="writes this .csv and .md")
    args = parser.parse_args()
    saved = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    policy = policy_from_checkpoint(saved)
    characters = list(saved["characters"])
    ids = {name: i for i, name in enumerate(characters)}
    rng = np.random.default_rng(args.seed)
    csv_path = Path(args.out + ".csv")
    rows = {}
    if csv_path.is_file():
        with csv_path.open(newline="", encoding="utf-8") as f:
            rows = {row["character"]: row for row in csv.DictReader(f)}
    for k, opponent in enumerate(characters):
        if opponent in rows and not rows[opponent]["error"]:
            continue
        try:
            with MugenPool(args.exe, args.game, args.p1, opponent, args.stage, args.matches, args.threads) as pool:
                result, life = play(pool, policy, "cpu", ids[args.p1], ids[opponent], args.seed, args.max_decisions, rng,
                                    return_life=True)
            row = {"character": opponent, "win_rate": f"{np.mean(result == 0):.3f}",
                   "opponent_life": f"{np.mean(life[:, 1]):.3f}", "own_life": f"{np.mean(life[:, 0]):.3f}", "error": ""}
        except Exception as error:  # a character that stops the pool shouldn't stop the scan
            row = {"character": opponent, "win_rate": "", "opponent_life": "", "own_life": "", "error": str(error)[:80]}
        rows[opponent] = row
        print(f"{k + 1}/{len(characters)} {Path(opponent).stem}: {row['win_rate'] or row['error']} "
              f"opponent life {row['opponent_life']}", flush=True)
        with csv_path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, FIELDS)
            writer.writeheader()
            writer.writerows(rows.values())
    done = sorted((r for r in rows.values() if not r["error"]),
                  key=lambda r: (float(r["win_rate"]), -float(r["opponent_life"])))
    failed = [r for r in rows.values() if r["error"]]
    lines = [f"# Toughest AI opponents for {Path(args.p1).stem}", "",
             f"Scan by `rl/scan_toughest.py`: `{Path(args.checkpoint).name}` as player 1 against each character's AI "
             f"(SmartAI, default level), {args.matches} matches each, {args.stage}. Toughest first: lowest win rate, then "
             "the most life the opponent had left at the end.", "",
             "| # | Character | Cap's win rate | Opponent's life left | Cap's life left |", "|---:|---|---:|---:|---:|"]
    for n, r in enumerate(done, 1):
        lines.append(f"| {n} | {Path(r['character']).stem} | {float(r['win_rate']):.0%} | {float(r['opponent_life']):.0%} "
                     f"| {float(r['own_life']):.0%} |")
    if failed:
        lines += ["", "Didn't run: " + ", ".join(f"{Path(r['character']).stem} ({r['error']})" for r in failed)]
    Path(args.out + ".md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {args.out}.md", flush=True)


if __name__ == "__main__":
    main()
