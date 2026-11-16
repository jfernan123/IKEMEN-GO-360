"""How a skill-trained policy's styles actually play: each style's fighting habits over a batch of matches against one
opponent, and how well the checkpoint's own style discriminator tells them apart on those matches.

Each style plays --matches fights (its first match each) in an RL pool, sampling moves as in training. Habits are
averaged over every decision of those matches: distance to the opponent, time attacking / guarding / in the air /
getting hit, how often it holds forward, back or up, how often it presses a button, how often it is in a special or
super (states 1000-4999; 5000 and up are getting hit), match length, win rate and life left.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch
from torch.distributions import Categorical

from model import policy_from_checkpoint
from pool_env import MugenPool

# Places in a fighter's block of the observation (see rlObservation in src/main.cpp).
LIFE, STATE, STATE_TYPE, MOVE_TYPE, GUARDING = 0, 12, 14, 15, 8


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/rl/mugen360.exe")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--p1", required=True)
    parser.add_argument("--p2", required=True)
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--matches", type=int, default=64, help="matches per style")
    parser.add_argument("--max-decisions", type=int, default=2500)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--seed", type=int, default=2024)
    args = parser.parse_args()
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    saved = torch.load(args.checkpoint, map_location=device, weights_only=False)
    skills = saved.get("skills", 0)
    if not skills:
        raise SystemExit("this checkpoint has no skills")
    ids = {name: i for i, name in enumerate(saved["characters"])}
    policy = policy_from_checkpoint(saved).to(device)
    size = saved["observation_size"]
    fighter = (size - 5) // 2
    window = saved.get("style_window", 1)
    judge = None
    if "discriminator" in saved:
        judge = torch.nn.Sequential(torch.nn.Linear(fighter + 17, 256), torch.nn.ReLU(), torch.nn.Linear(256, 256),
                                    torch.nn.ReLU(), torch.nn.Linear(256, skills)).to(device)
        judge.load_state_dict(saved["discriminator"])
        judge.eval()

    n = args.matches * skills
    style = torch.arange(n, device=device) % skills
    print(f"{Path(args.checkpoint).name}: {saved['steps']:,} decisions trained, {skills} styles, "
          f"{args.matches} matches each vs {Path(args.p2).stem}")
    habits = {k: [] for k in ("distance", "attacking", "guarding", "air", "hit", "forward", "back", "up", "button",
                              "special")}
    sums = {k: np.zeros(n) for k in habits}
    decisions = np.zeros(n)
    finished = np.full(n, -2)
    life_left = np.zeros(n)
    confusion = np.zeros((skills, skills))
    with MugenPool(args.exe, args.game, args.p1, args.p2, args.stage, n, args.threads) as pool:
        obs = pool.reset(args.seed)[:, 0]
        hidden = policy.initial_hidden(n, device)
        average = torch.zeros((n, fighter + 17), device=device)
        seen = torch.zeros(n, device=device)
        self_ids = torch.full((n,), ids[args.p1], device=device)
        other_ids = torch.full((n,), ids[args.p2], device=device)
        for _ in range(args.max_decisions):
            playing = finished == -2
            if not playing.any():
                break
            x = torch.as_tensor(obs.copy(), device=device)
            with torch.no_grad():
                ld, lb, _, hidden = policy(x, self_ids, other_ids, hidden, style)
                d = Categorical(logits=ld).sample()
                b = Categorical(logits=lb).sample()
                if judge is not None:
                    now = torch.cat((x[:, :fighter], torch.nn.functional.one_hot(d, 9).float(),
                                     torch.nn.functional.one_hot(b, 8).float()), -1)
                    seen += 1
                    rate = torch.maximum(1.0 / seen, torch.full_like(seen, 1.0 / max(1, window)))
                    average += rate.unsqueeze(1) * (now - average)
                    guess = judge(average).argmax(-1).cpu().numpy()
            dn, bn = d.cpu().numpy(), b.cpu().numpy()
            me, them = obs[:, :fighter], obs[:, fighter:2 * fighter]
            live = playing.astype(float)
            sums["distance"] += live * np.abs(obs[:, 2 * fighter]) * 640
            sums["attacking"] += live * (me[:, MOVE_TYPE] == 0)
            sums["hit"] += live * (me[:, MOVE_TYPE] == 1)
            sums["guarding"] += live * (me[:, GUARDING] > 0)
            sums["air"] += live * (me[:, STATE_TYPE] == 1)
            sums["forward"] += live * np.isin(dn, (1, 4, 7))
            sums["back"] += live * np.isin(dn, (2, 5, 8))
            sums["up"] += live * np.isin(dn, (6, 7, 8))
            sums["button"] += live * (bn > 0)
            state_no = np.round(me[:, STATE] * 6000)
            sums["special"] += live * ((state_no >= 1000) & (state_no < 5000))
            decisions += live
            if judge is not None:
                for i in np.flatnonzero(playing):
                    confusion[int(style[i]), guess[i]] += 1
            actions = np.zeros((n, 4), np.uint8)
            actions[:, 0], actions[:, 1] = dn, bn
            _, done, winner, both = pool.step(actions)
            new_life = both[:, 0, LIFE]
            end = playing & done
            finished[end] = winner[end]
            life_left[end] = np.where(winner[end] == 0, me[end, LIFE], 0)
            obs = both[:, 0]
            hidden = hidden * torch.as_tensor(~done, device=device, dtype=torch.float32).unsqueeze(1)
        still = finished == -2
        finished[still] = -1
    rows = []
    for k in range(skills):
        mine = (np.arange(n) % skills) == k
        steps = decisions[mine].sum()
        row = {name: sums[name][mine].sum() / max(1, steps) for name in habits}
        row["length_s"] = decisions[mine].mean() * 3 / 60
        row["win"] = np.mean(finished[mine] == 0)
        row["life"] = life_left[mine][finished[mine] == 0].mean() if (finished[mine] == 0).any() else 0
        row["recognised"] = confusion[k, k] / max(1, confusion[k].sum()) if judge is not None else float("nan")
        rows.append(row)
    print(f"{'style':>5} {'win':>5} {'life':>5} {'secs':>5} {'dist':>5} {'atk':>5} {'grd':>5} {'air':>5} {'hit':>5} "
          f"{'fwd':>5} {'back':>5} {'up':>5} {'btn':>5} {'spcl':>5} {'judge':>6}")
    for k, r in enumerate(rows):
        print(f"{k:>5} {r['win']:5.0%} {r['life']:5.0%} {r['length_s']:5.0f} {r['distance']:5.0f} {r['attacking']:5.0%} "
              f"{r['guarding']:5.0%} {r['air']:5.0%} {r['hit']:5.0%} {r['forward']:5.0%} {r['back']:5.0%} "
              f"{r['up']:5.0%} {r['button']:5.0%} {r['special']:5.0%} {r['recognised']:6.0%}")
    if judge is not None:
        norm = confusion / confusion.sum(1, keepdims=True).clip(1)
        print("\njudge's guesses (rows: true style, columns: guessed style)")
        print("      " + " ".join(f"{k:>5}" for k in range(skills)))
        for k in range(skills):
            print(f"{k:>5} " + " ".join(f"{v:5.0%}" for v in norm[k]))
        print(f"distinct styles (sum of the diagonal): {np.trace(norm):.1f} of {skills}")


if __name__ == "__main__":
    main()
