"""Record matches of the latest policy (graphics on, through record_clip.py) until player 1 wins, then open that video.

Saved training wins can't be replayed faithfully yet (BUGFIX_LIST.md #1 and #9b), so this plays fresh matches with the
current checkpoint instead: four at a time, new seeds each round, stopping at the first win.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--p1", required=True)
    parser.add_argument("--p2", required=True)
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--out", required=True, help="where the winning video goes")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--tries", type=int, default=80)
    parser.add_argument("--greedy", action="store_true", help="the policy's likeliest moves instead of sampling")
    parser.add_argument("--no-open", action="store_true", help="save the video without opening it")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    # Training rewrites the checkpoint after every update; play a copy so it can't change mid-match.
    snapshot = Path(tempfile.mkdtemp(prefix="m360watch_")) / "policy.pt"
    shutil.copy(root / args.checkpoint, snapshot)
    out = (root / args.out).resolve()
    first_seed = random.randrange(1_000_000)

    def attempt(seed):
        candidate = out.with_name(f"{out.stem}_try{seed}{out.suffix}")
        result = subprocess.run([sys.executable, str(root / "rl/record_clip.py"), "--checkpoint", str(snapshot),
                                 "--p1", args.p1, "--p2", args.p2, "--stage", args.stage, "--seed", str(seed),
                                 "--only-win", "--out", str(candidate)] + (["--greedy"] if args.greedy else []), cwd=root, capture_output=True, text=True)
        line = next((l for l in result.stdout.splitlines() if l.startswith("{")), "{}")
        return seed, candidate, json.loads(line)

    tried = 0
    with concurrent.futures.ThreadPoolExecutor(args.parallel) as pool:
        while tried < args.tries:
            seeds = range(first_seed + tried, first_seed + tried + args.parallel)
            tried += args.parallel
            won = None
            for seed, candidate, result in pool.map(attempt, seeds):
                print(f"match {seed - first_seed + 1}: {result.get('winner', '?')} "
                      f"(p1 life {result.get('p1_life', '?')}, p2 life {result.get('p2_life', '?')})", flush=True)
                if result.get("clip") and won is None:
                    won = candidate
                elif result.get("clip"):
                    candidate.unlink(missing_ok=True)
            if won:
                won.replace(out)
                print(f"won: {out}")
                if os.name == "nt" and not args.no_open:
                    os.startfile(out)
                return
    print(f"no win in {tried} matches")


if __name__ == "__main__":
    main()
