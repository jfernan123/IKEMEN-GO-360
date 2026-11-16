"""Win rates of one policy against SmartAI with several opponents, each over a batch of matches in an RL pool.

For every opponent, N fights start from the same seeds twice: once with the policy playing player 1 (sampling its
actions, as in training) and once with uniformly random inputs, as a baseline. Each fight's first match counts; a
match still running after --max-decisions is decided on remaining life, like a time over.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch
from torch.distributions import Categorical

from model import policy_from_checkpoint
from pool_env import MugenPool


def play(pool, policy, device, self_id, other_id, seed, max_decisions, rng, greedy=False, return_life=False):
    n = pool.count
    obs = pool.reset(seed)[:, 0]
    hidden = policy.initial_hidden(n, device) if policy else None
    result = np.full(n, -2)  # -2 still playing, then winner (0 player 1, 1 player 2, -1 draw)
    life = np.zeros((n, 2), np.float32)
    other = (obs.shape[1] - 5) // 2
    for _ in range(max_decisions):
        actions = np.zeros((n, 4), np.uint8)
        if policy:
            with torch.no_grad():
                ids_self = torch.full((n,), self_id, device=device)
                ids_other = torch.full((n,), other_id, device=device)
                ld, lb, _, hidden = policy(torch.as_tensor(obs, device=device), ids_self, ids_other, hidden)
                if greedy:
                    actions[:, 0] = ld.argmax(-1).cpu().numpy()
                    actions[:, 1] = lb.argmax(-1).cpu().numpy()
                else:
                    actions[:, 0] = Categorical(logits=ld).sample().cpu().numpy()
                    actions[:, 1] = Categorical(logits=lb).sample().cpu().numpy()
        else:
            actions[:, 0] = rng.integers(0, 9, n)
            actions[:, 1] = rng.integers(0, 8, n)
        playing = result == -2
        life[playing] = obs[playing][:, [0, other]]
        _, done, winner, obs_both = pool.step(actions)
        obs = obs_both[:, 0]
        finished = playing & done
        result[finished] = winner[finished]
        if policy:
            hidden = hidden * torch.as_tensor(~done, device=device, dtype=torch.float32).unsqueeze(1)
        if (result != -2).all():
            break
    open_ = result == -2
    result[open_] = np.where(life[open_, 0] > life[open_, 1], 0, np.where(life[open_, 0] < life[open_, 1], 1, -1))
    return (result, life) if return_life else result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/rl/mugen360.exe")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--p1", required=True)
    parser.add_argument("--opponents", required=True, help="comma-separated character paths")
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--matches", type=int, default=64)
    parser.add_argument("--max-decisions", type=int, default=2500)
    parser.add_argument("--seed", type=int, default=4242)
    parser.add_argument("--threads", type=int, default=8)
    args = parser.parse_args()
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    saved = torch.load(args.checkpoint, map_location=device, weights_only=False)
    ids = {name: i for i, name in enumerate(saved["characters"])}
    policy = policy_from_checkpoint(saved).to(device)
    print(f"{Path(args.checkpoint).name}: {saved.get('size', 'small')} network, {saved['steps']:,} decisions trained; "
          f"{args.matches} matches each")
    print(f"{'opponent (SmartAI)':28s} {'policy wins':>12s} {'greedy wins':>12s} {'random wins':>12s}")
    rng = np.random.default_rng(args.seed)
    for opponent in [o.strip() for o in args.opponents.split(",") if o.strip()]:
        with MugenPool(args.exe, args.game, args.p1, opponent, args.stage, args.matches, args.threads) as pool:
            learned = play(pool, policy, device, ids[args.p1], ids[opponent], args.seed, args.max_decisions, rng)
            greedy = play(pool, policy, device, ids[args.p1], ids[opponent], args.seed, args.max_decisions, rng, True)
            baseline = play(pool, None, device, 0, 0, args.seed, args.max_decisions, rng)
        print(f"{Path(opponent).stem:28s} {np.mean(learned == 0):12.1%} {np.mean(greedy == 0):12.1%} {np.mean(baseline == 0):12.1%}", flush=True)


if __name__ == "__main__":
    main()
