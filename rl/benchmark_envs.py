"""Measure headless fight throughput at several parallel environment counts."""

from __future__ import annotations

import argparse
import concurrent.futures
import time

from mugen_env import MugenEnv
from roster import discover


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", required=True)
    parser.add_argument("--exe", default="build/pc/mugen360.exe")
    parser.add_argument("--counts", default="1,2,4,8")
    parser.add_argument("--decisions", type=int, default=200)
    args = parser.parse_args()
    roster = discover(args.game)
    for count in map(int, args.counts.split(",")):
        envs = [MugenEnv(args.exe, args.game, roster.characters[(i * 2) % len(roster.characters)],
                         roster.characters[(i * 2 + 1) % len(roster.characters)],
                         roster.stages[i % len(roster.stages)]) for i in range(count)]
        for i, env in enumerate(envs):
            env.reset(1000 + i)

        def run(item):
            index, env = item
            for step in range(args.decisions):
                env.step(((step + index) % 9, (step // 3 + index) % 8))

        started = time.perf_counter()
        with concurrent.futures.ThreadPoolExecutor(count) as pool:
            list(pool.map(run, enumerate(envs)))
        elapsed = time.perf_counter() - started
        print(f"{count} envs: {count * args.decisions / elapsed:.1f} total decisions/s "
              f"({args.decisions / elapsed:.1f} each)")
        for env in envs:
            env.close()


if __name__ == "__main__":
    main()
