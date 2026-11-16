"""Record a saved training win (rl/wins*/win_*.json) to an MP4.

A win's player 1 script holds screen directions (L/R), so it is turned back into the policy's relative actions by
stepping the same match headless and reading the fighter's facing. The headless run checks that the win reproduces;
the recording then plays the same actions with graphics loaded (--rl-record). Training is headless, and with graphics
explods exist (BUGFIX_LIST.md #1), so the recorded fight can drift from the real one; the result says whether it did.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

import imageio_ffmpeg

from mugen_env import MugenEnv

BUTTONS = {"x": 1, "y": 2, "z": 3, "a": 4, "b": 5, "c": 6, "s": 7}


def read_script(path: Path):
    lines = []
    for line in path.read_text().splitlines():
        if line.strip():
            frames, keys = line.split()
            lines.append(("" if keys == "-" else keys))
    return lines


def action(keys: str, facing: float):
    up, down = "U" in keys, "D" in keys
    right, left = "R" in keys, "L" in keys
    forward = right if facing >= 0 else left
    back = left if facing >= 0 else right
    if up:
        direction = 7 if forward else 8 if back else 6
    elif down:
        direction = 4 if forward else 5 if back else 3
    else:
        direction = 1 if forward else 2 if back else 0
    button = next((BUTTONS[k] for k in keys if k in BUTTONS), 0)
    return direction, button


def play(exe, game, data, keys, extra_args=None):
    actions, state = [], None
    with MugenEnv(exe, game, data["p1"], data["p2"], data["stage"], data["frame_skip"], extra_args) as env:
        state = env.reset(data["seed"], data["opponent"] != "smart")
        total = 0.0
        for k in keys:
            a = action(k, float(state.observation[6]))
            actions.append(a)
            state = env.step(a)
            total += state.reward
            if state.done:
                break
    other = (state.observation.size - 5) // 2
    return {"winner": state.winner, "done": state.done, "return": round(total, 3), "decisions": len(actions),
            "p1_life": round(float(state.observation[0]), 3), "p2_life": round(float(state.observation[other]), 3)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("replay", help="a win_*.json from training")
    parser.add_argument("--headless-exe", default="build/rl/mugen360.exe")
    parser.add_argument("--record-exe", default="build/pc_test/mugen360.exe", help="a build with --rl-record")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    data = json.loads(Path(args.replay).read_text(encoding="utf-8"))
    keys = read_script(Path(data["p1_input"]))
    game = data["game"]
    headless = play(args.headless_exe, game, data, keys)
    frames = Path(tempfile.mkdtemp(prefix="m360win_"))
    try:
        recorded = play(args.record_exe, game, data, keys, ["--rl-record", str(frames)])
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-loglevel", "error", "-framerate", "20", "-i",
                        str(frames / "%06d.bmp"), "-vf", "scale=960:-2", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                        "-crf", "23", str(out)], check=True)
    finally:
        shutil.rmtree(frames, ignore_errors=True)
    print(json.dumps({"saved_return": round(data["return"], 3), "headless": headless, "recorded": recorded,
                      "clip": str(Path(args.out).resolve())}, indent=1))


if __name__ == "__main__":
    main()
