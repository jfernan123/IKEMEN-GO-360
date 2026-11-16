"""Watch a policy fight live in the game window (graphics and sound, normal speed), one match after another.

The policy plays player 1 against SmartAI through the RL server's --show mode. Close the window or press Ctrl+C to stop.
"""

from __future__ import annotations

import argparse
import random
import shutil
import tempfile
from pathlib import Path

import torch
from torch.distributions import Categorical

from model import policy_from_checkpoint
from mugen_env import MugenEnv


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/pc_test/mugen360.exe", help="a build with the RL server's --show")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--p1", required=True)
    parser.add_argument("--p2", required=True)
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--greedy", action="store_true", help="always the likeliest move (the policy's best play)")
    parser.add_argument("--matches", type=int, default=0, help="stop after this many (0: keep going)")
    parser.add_argument("--skill", type=int, default=-1, help="the policy's style; -1 picks a new one each match")
    args = parser.parse_args()
    # Training rewrites the checkpoint after every update; play a copy.
    snapshot = Path(tempfile.mkdtemp(prefix="m360live_")) / "policy.pt"
    shutil.copy(args.checkpoint, snapshot)
    saved = torch.load(snapshot, map_location="cpu", weights_only=False)
    ids = {name: i for i, name in enumerate(saved["characters"])}
    policy = policy_from_checkpoint(saved)
    self_id, other_id = torch.tensor([ids[args.p1]]), torch.tensor([ids[args.p2]])
    print(f"{Path(args.checkpoint).name}: {saved['steps']:,} decisions trained, {'greedy' if args.greedy else 'sampling'}")
    wins = played = 0
    with MugenEnv(args.exe, args.game, args.p1, args.p2, args.stage, extra_args=["--show"]) as env:
        while not args.matches or played < args.matches:
            state = env.reset(random.randrange(1 << 30))
            hidden = policy.initial_hidden(1, "cpu")
            skill = torch.tensor([args.skill if args.skill >= 0 else random.randrange(max(1, policy.skills))])
            while not state.done:
                with torch.no_grad():
                    ld, lb, _, hidden = policy(torch.from_numpy(state.observation).unsqueeze(0), self_id, other_id,
                                               hidden, skill if policy.skills else None)
                if args.greedy:
                    action = (int(ld.argmax()), int(lb.argmax()))
                else:
                    action = (int(Categorical(logits=ld).sample()), int(Categorical(logits=lb).sample()))
                state = env.step(action)
            played += 1
            wins += state.winner == 0
            style = f" with style {int(skill)}" if policy.skills else ""
            print(f"match {played}{style}: {'WIN' if state.winner == 0 else 'loss'} ({wins}/{played} won)", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, RuntimeError):
        pass
