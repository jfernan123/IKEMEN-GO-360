"""Rate SmartAI per character: each character's mirror match, SmartAI (player 2) against uniformly random controls
(player 1). A character whose SmartAI often loses to random inputs is a weak training opponent."""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import random
import sys
import time
from pathlib import Path

from mugen_env import MugenEnv
from roster import discover


def rate(args, character):
    rng = random.Random(hash(character) & 0xFFFF)
    results = []
    try:
        with MugenEnv(args.exe, args.game, character, character, args.stage, args.frame_skip) as env:
            for match in range(args.matches):
                state = env.reset(9000 + match)
                decisions = 0
                while not state.done and decisions < args.max_decisions:
                    state = env.step((rng.randrange(env.direction_count), rng.randrange(env.button_count)))
                    decisions += 1
                other = (state.observation.size - 5) // 2
                life_random, life_smart = float(state.observation[0]), float(state.observation[other])
                # An overlong match is decided on remaining life, like a time over.
                winner = state.winner if state.done else (0 if life_random > life_smart else 1 if life_smart > life_random else -1)
                results.append((winner, decisions, life_smart - life_random, not state.done))
    except Exception as error:
        return character, None, str(error)
    return character, results, ""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/pc/mugen360.exe")
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--matches", type=int, default=6)
    parser.add_argument("--workers", type=int, default=6)
    parser.add_argument("--frame-skip", type=int, default=3)
    parser.add_argument("--max-decisions", type=int, default=2500)
    parser.add_argument("--out", default="rl/smartai_scan.csv")
    args = parser.parse_args()
    roster = discover(args.game)
    started = time.time()
    with open(args.out, "w", newline="", encoding="utf-8") as file, \
            concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
        out = csv.writer(file)
        out.writerow(["character", "smart_win_rate", "random_wins", "draws", "matches", "mean_life_margin",
                      "mean_decisions", "unfinished", "error"])
        futures = [pool.submit(rate, args, c) for c in roster.characters]
        for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
            character, results, error = future.result()
            if results is None:
                out.writerow([character, "", "", "", 0, "", "", "", error])
            else:
                n = len(results)
                out.writerow([character, f"{sum(r[0] == 1 for r in results) / n:.3f}",
                              sum(r[0] == 0 for r in results), sum(r[0] == -1 for r in results), n,
                              f"{sum(r[2] for r in results) / n:.3f}", sum(r[1] for r in results) // n,
                              sum(r[3] for r in results), ""])
            file.flush()
            print(f"{done}/{len(futures)} {character} {time.time() - started:.0f}s", flush=True)


if __name__ == "__main__":
    sys.exit(main())
