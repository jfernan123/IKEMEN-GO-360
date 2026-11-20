"""Head-to-head win rate of two checkpoints playing the same character, over a batch of matches in an RL pool (its
external mode: the second policy drives player 2 from its own view, as the league does in train_pool.py).

Each policy plays player 1 for half of the matches, from the same seeds, so the side doesn't favour either. Every
fight gets a random skill for each side (as in training); each fight's first match counts, and one still running
after --max-decisions is decided on remaining life, like a time over.

    rl\\.venv\\Scripts\\python.exe rl\\eval_versus.py --exe build\\rl\\mugen360.exe --a rl\\checkpoints\\league_cap_shared.pt ^
        --b rl\\checkpoints\\league_cap_268M.pt --p1 chars/captainamerica/captainamerica.def --matches 256
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch
from torch.distributions import Categorical

from model import policy_from_checkpoint
from pool_env import MugenPool


def load(path, device):
    saved = torch.load(path, map_location=device, weights_only=False)
    policy = policy_from_checkpoint(saved).to(device).eval()
    return saved, policy


def act(policy, obs, ids, hidden, skill, device):
    ld, lb, _, hidden = policy(torch.as_tensor(obs, device=device), ids, ids, hidden,
                               skill % policy.skills if policy.skills else None)
    return Categorical(logits=ld).sample().cpu().numpy(), Categorical(logits=lb).sample().cpu().numpy(), hidden


def play(pool, first, second, ids, seed, max_decisions, rng, device):
    """Winners of one batch: first plays player 1, second player 2 (0 player 1, 1 player 2, -1 draw)."""
    n = pool.count
    both = pool.reset(seed, external=True)
    h1, h2 = first.initial_hidden(n, device), second.initial_hidden(n, device)
    s1 = torch.as_tensor(rng.integers(0, 1 << 16, n), device=device)
    s2 = torch.as_tensor(rng.integers(0, 1 << 16, n), device=device)
    result = np.full(n, -2)
    life = np.zeros((n, 2), np.float32)
    other = (both.shape[2] - 5) // 2
    for _ in range(max_decisions):
        actions = np.zeros((n, 4), np.uint8)
        with torch.no_grad():
            actions[:, 0], actions[:, 1], h1 = act(first, both[:, 0], ids, h1, s1, device)
            actions[:, 2], actions[:, 3], h2 = act(second, both[:, 1], ids, h2, s2, device)
        playing = result == -2
        life[playing] = both[playing, 0][:, [0, other]]
        _, done, winner, both = pool.step(actions)
        finished = playing & done
        result[finished] = winner[finished]
        keep = torch.as_tensor(~done, device=device, dtype=torch.float32).unsqueeze(1)
        h1, h2 = h1 * keep, h2 * keep
        if (result != -2).all():
            break
    open_ = result == -2
    result[open_] = np.where(life[open_, 0] > life[open_, 1], 0, np.where(life[open_, 0] < life[open_, 1], 1, -1))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/rl/mugen360.exe")
    parser.add_argument("--a", required=True)
    parser.add_argument("--b", required=True)
    parser.add_argument("--p1", required=True, help="the character both play")
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--matches", type=int, default=256, help="per side")
    parser.add_argument("--max-decisions", type=int, default=2500)
    parser.add_argument("--seed", type=int, default=4242)
    parser.add_argument("--threads", type=int, default=8)
    args = parser.parse_args()
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    saved_a, a = load(args.a, device)
    saved_b, b = load(args.b, device)
    ids = torch.full((args.matches,), saved_a["characters"].index(args.p1), device=device)
    rng = np.random.default_rng(args.seed)
    print(f"A {Path(args.a).name} ({saved_a['steps']:,}) vs B {Path(args.b).name} ({saved_b['steps']:,}), "
          f"{args.matches} matches with each as player 1", flush=True)
    with MugenPool(args.exe, args.game, args.p1, args.p1, args.stage, args.matches, args.threads) as pool:
        a_first = play(pool, a, b, ids, args.seed, args.max_decisions, rng, device)
        b_first = play(pool, b, a, ids, args.seed, args.max_decisions, rng, device)
    a_wins = np.sum(a_first == 0) + np.sum(b_first == 1)
    b_wins = np.sum(a_first == 1) + np.sum(b_first == 0)
    draws = 2 * args.matches - a_wins - b_wins
    total = 2 * args.matches
    print(f"A as player 1: A {np.mean(a_first == 0):.1%}, B {np.mean(a_first == 1):.1%}")
    print(f"B as player 1: B {np.mean(b_first == 0):.1%}, A {np.mean(b_first == 1):.1%}")
    print(f"overall: A {a_wins / total:.1%}, B {b_wins / total:.1%}, draws {draws / total:.1%}")


if __name__ == "__main__":
    main()
