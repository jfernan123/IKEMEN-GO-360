"""Play a saved winning RL episode with the normal visible engine."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("replay", nargs="?", help="win JSON; newest rl/wins/*.json when omitted")
    # pc_test can be rebuilt while a long trainer keeps build/pc/mugen360.exe open.
    parser.add_argument("--exe", default="build/pc_test/mugen360.exe")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.replay:
        replay = Path(args.replay)
    else:
        wins = sorted((root / "rl/wins").glob("win_*.json"), key=lambda path: path.stat().st_mtime)
        if not wins:
            raise SystemExit("No winning replays yet. Training saves them under rl/wins.")
        replay = wins[-1]
    data = json.loads(replay.read_text(encoding="utf-8"))
    command = [str((root / args.exe).resolve()), "--game", data["game"], "--p1", data["p1"], "--p2", data["p2"],
               "--stage", data["stage"], "--input", data["p1_input"], "--quickstart", "--wins", "1", "--show",
               "--quit", str(data["decisions"] * data["frame_skip"] + 600)]
    if data["opponent"] == "smart":
        command.extend(("--cpu", "--ai", "smart", "--aiseed", str(data["seed"])))
    else:
        command.extend(("--input2", data["p2_input"]))
    print(f"Replaying {Path(data['p1']).stem} vs {Path(data['p2']).stem} from {replay}")
    subprocess.run(command, cwd=root, check=False)


if __name__ == "__main__":
    main()
