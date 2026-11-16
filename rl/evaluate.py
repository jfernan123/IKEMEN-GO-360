from __future__ import annotations

import argparse
import concurrent.futures
import random
import time
from pathlib import Path

import torch
from torch.distributions import Categorical

from model import Policy, policy_from_checkpoint
from mugen_env import MugenEnv
from roster import discover


def duration(seconds: float) -> str:
    seconds = max(0, int(seconds))
    hours, seconds = divmod(seconds, 3600)
    minutes, seconds = divmod(seconds, 60)
    return f"{hours:d}:{minutes:02d}:{seconds:02d}" if hours else f"{minutes:02d}:{seconds:02d}"


def progress(label, complete, total, wins, draws, mean_return, started):
    width = 30
    filled = width if total <= 0 else int(width * complete / total)
    elapsed = time.time() - started
    eta = elapsed * (total - complete) / complete if complete else 0
    losses = complete - wins - draws
    line = (f"{label:6} [{'#' * filled}{'-' * (width - filled)}] {complete:>4}/{total:<4} "
            f"W:{wins:<4} D:{draws:<4} L:{losses:<4} return:{mean_return:+.3f} "
            f"elapsed:{duration(elapsed)} ETA:{duration(eta)}")
    print("\r" + line.ljust(125), end="\n" if complete >= total else "", flush=True)


def play_match(label, match, matchup, args, ids, policy):
    p1, p2, stage = matchup
    # A per-match generator makes random actions repeatable regardless of parallel completion order.
    action_rng = random.Random(args.seed + 1_000_000 + match)
    with MugenEnv(args.exe, args.game, p1, p2, stage) as env:
        state = env.reset(10_000 + match)
        hidden = policy.initial_hidden(1, "cpu")
        episode_return = 0.0
        while not state.done:
            if label == "random":
                action = (action_rng.randrange(env.direction_count), action_rng.randrange(env.button_count))
            else:
                with torch.no_grad():
                    ld, lb, _, hidden = policy(torch.from_numpy(state.observation).unsqueeze(0),
                        torch.tensor([ids[p1]]), torch.tensor([ids[p2]]), hidden)
                # Sampled as in training, unless --greedy asks for the likeliest move (a different, usually
                # stronger player than the one training measures).
                if args.greedy:
                    action = (int(ld.argmax()), int(lb.argmax()))
                else:
                    action = (int(Categorical(logits=ld).sample()), int(Categorical(logits=lb).sample()))
            state = env.step(action)
            episode_return += state.reward
    return state.winner, episode_return


def evaluate(label, matchups, args, ids, policy):
    wins = draws = 0
    returns = []
    started = time.time()
    print(f"{label}: starting {len(matchups)} fixed-seed matches on {args.envs} parallel environments")
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.envs) as pool:
        futures = [pool.submit(play_match, label, match, matchup, args, ids, policy)
                   for match, matchup in enumerate(matchups)]
        for complete, future in enumerate(concurrent.futures.as_completed(futures), 1):
            winner, episode_return = future.result()
            wins += winner == 0
            draws += winner < 0
            returns.append(episode_return)
            progress(label, complete, len(matchups), wins, draws, sum(returns) / len(returns), started)
    losses = len(matchups) - wins - draws
    mean_return = sum(returns) / max(1, len(returns))
    print(f"{label:6}: {wins}/{len(matchups)} wins, {draws} draws, {losses} losses, "
          f"mean return {mean_return:+.3f} against SmartAI")
    return wins, draws, mean_return


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--game", required=True); p.add_argument("--checkpoint", default="rl/checkpoints/marvel_policy.pt")
    p.add_argument("--exe", default="build/pc/mugen360.exe"); p.add_argument("--matches", type=int, default=100)
    p.add_argument("--envs", type=int, default=8, help="matches evaluated simultaneously")
    p.add_argument("--agent", choices=("policy", "random", "both"), default="both")
    p.add_argument("--greedy", action="store_true", help="always the likeliest move instead of sampling")
    p.add_argument("--seed", type=int, default=360, help="fixed matchups and random baseline")
    p.add_argument("--target", type=float, default=.85, help="SmartAI win rate required to qualify the checkpoint")
    p.add_argument("--qualified", default="rl/checkpoints/marvel_policy_qualified.pt")
    args = p.parse_args(); args.envs = max(1, args.envs)
    # Each evaluator thread has tiny network calls; intra-op worker pools would only oversubscribe the CPU.
    torch.set_num_threads(1)
    saved = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    roster = discover(args.game); ids = {name: i for i, name in enumerate(saved["characters"])}
    policy = policy_from_checkpoint(saved)
    rng = random.Random(args.seed)
    characters = [x for x in roster.characters if x in ids]
    matchups = [(*rng.choices(characters, k=2), rng.choice(roster.stages)) for _ in range(args.matches)]
    policy_result = random_result = None
    if args.agent in ("policy", "both"):
        policy_result = evaluate("policy", matchups, args, ids, policy)
    if args.agent in ("random", "both"):
        random_result = evaluate("random", matchups, args, ids, policy)
    if policy_result and random_result:
        win_delta = 100.0 * (policy_result[0] - random_result[0]) / max(1, args.matches)
        return_delta = policy_result[2] - random_result[2]
        print(f"policy advantage: {win_delta:+.1f} percentage points, mean return {return_delta:+.3f}")
    if policy_result:
        win_rate = policy_result[0] / max(1, args.matches)
        gap = 100.0 * (win_rate - args.target)
        if win_rate >= args.target:
            qualified = Path(args.qualified)
            qualified.parent.mkdir(parents=True, exist_ok=True)
            # Save the state loaded at evaluation start; training may have advanced the live checkpoint meanwhile.
            torch.save(saved, qualified)
            print(f"QUALIFIED: {win_rate:.1%} >= {args.target:.1%}; saved evaluated policy to {qualified}")
        else:
            print(f"KEEP TRAINING: {win_rate:.1%} < {args.target:.1%} target ({gap:.1f} percentage points)")


if __name__ == "__main__": main()
