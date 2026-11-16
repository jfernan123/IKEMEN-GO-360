"""Record one policy-vs-SmartAI (or policy-vs-policy) match to an MP4 through the RL server itself (--rl-record), so the clip is exactly
the match the policy played. Needs a build with --rl-record (build_pc_test.bat) and imageio-ffmpeg."""

from __future__ import annotations

import argparse
import json
import random
import shutil
import subprocess
import tempfile
from pathlib import Path

import imageio_ffmpeg
import torch
from torch.distributions import Categorical

from model import Policy, policy_from_checkpoint
from mugen_env import MugenEnv


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default="DC vs MARVEL 2025")
    parser.add_argument("--exe", default="build/pc_test/mugen360.exe")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--p1", required=True, help="the policy's character")
    parser.add_argument("--p2", required=True, help="the opponent's character")
    parser.add_argument("--vs-policy", nargs="?", const="same", metavar="CHECKPOINT",
                        help="player 2 is a policy instead of SmartAI (this checkpoint, or --checkpoint's when blank)")
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--random", action="store_true", help="player 1 presses uniformly random inputs (a baseline)")
    parser.add_argument("--greedy", action="store_true", help="always take the likeliest action instead of sampling")
    parser.add_argument("--skill", type=int, default=-1, help="the policy's skill (style); -1 picks one at random")
    parser.add_argument("--only-win", action="store_true", help="write the clip only if player 1 wins")
    parser.add_argument("--out", required=True)
    parser.add_argument("--p2-life", type=float, default=1.0, help="player 2's life bar as a fraction (a handicap)")
    args = parser.parse_args()
    saved = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    ids = {name: i for i, name in enumerate(saved["characters"])}
    opponent_saved = None
    if args.vs_policy:
        opponent_saved = saved if args.vs_policy == "same" else torch.load(args.vs_policy, map_location="cpu",
                                                                           weights_only=False)

    def load(state):
        # In the shape it was saved with (8 or 65 button choices).
        return policy_from_checkpoint(state, env.observation_size)

    baseline = random.Random(args.seed)
    nonlocal_opponent = [None]

    styles = saved.get("skills", 0)
    skill = torch.tensor([args.skill if args.skill >= 0 else random.Random(args.seed).randrange(max(1, styles))])

    def act(policy, observation, self_id, other_id, hidden):
        if args.random and policy is not nonlocal_opponent[0]:
            return (baseline.randrange(9), baseline.randrange(8)), hidden
        with torch.no_grad():
            ld, lb, _, hidden = policy(torch.from_numpy(observation).unsqueeze(0), self_id, other_id, hidden,
                                       skill if policy.skills else None)
        if args.greedy:
            return (int(ld.argmax()), int(lb.argmax())), hidden
        return (int(Categorical(logits=ld).sample()), int(Categorical(logits=lb).sample())), hidden
    torch.manual_seed(args.seed)
    frames = Path(tempfile.mkdtemp(prefix="m360clip_"))
    try:
        with MugenEnv(args.exe, args.game, args.p1, args.p2, args.stage,
                      extra_args=["--rl-record", str(frames)]) as env:
            policy = load(saved)
            nonlocal_opponent[0] = load(opponent_saved) if opponent_saved else None
            opponent = nonlocal_opponent[0]
            state, decisions = env.reset(args.seed, opponent is not None, args.p2_life), 0
            hidden = policy.initial_hidden(1, "cpu")
            opponent_hidden = policy.initial_hidden(1, "cpu")
            self_id, other_id = torch.tensor([ids[args.p1]]), torch.tensor([ids[args.p2]])
            while not state.done and decisions < 3000:
                action, hidden = act(policy, state.observation, self_id, other_id, hidden)
                opponent_action = (0, 0)
                if opponent:
                    opponent_action, opponent_hidden = act(opponent, state.opponent_observation, other_id, self_id,
                                                           opponent_hidden)
                state = env.step(action, opponent_action)
                decisions += 1
        other = (state.observation.size - 5) // 2
        result = {"skill": int(skill) if styles else None, "winner": "p1" if state.winner == 0 else "p2" if state.winner == 1 else "draw",
                  "decisions": decisions, "p1_life": round(float(state.observation[0]), 3),
                  "p2_life": round(float(state.observation[other]), 3)}
        out = Path(args.out)
        if args.only_win and state.winner != 0:
            print(json.dumps({"clip": None, **result}))
            return
        out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-loglevel", "error", "-framerate", "20", "-i",
                        str(frames / "%06d.bmp"), "-vf", "scale=960:-2", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                        "-crf", "23", str(out)], check=True)
        print(json.dumps({"clip": str(out.resolve()), **result}))
    finally:
        shutil.rmtree(frames, ignore_errors=True)


if __name__ == "__main__":
    main()
