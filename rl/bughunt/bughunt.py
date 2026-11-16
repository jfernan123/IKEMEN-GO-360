"""Finds visual bugs in recorded fights, in three steps (the keyframe-then-verify pipeline of recent game-QA work:
"Automated Bug Frame Retrieval from Gameplay Videos Using VLMs", ICSE 2026; RESP, 2026).

  record   records matches through the engine itself (rl/record_clip.py: the clip is exactly what the engine drew).
           Player 1 presses random buttons, player 2 is SmartAI, so each character listed as player 2 shows its moves.
  sheets   cuts every clip down to keyframes: one every --every seconds, plus the frames where the picture changes
           most (hits, supers, flashes, effects appearing), at least --gap seconds apart. Keyframes go onto contact
           sheets, 3 x 2 in time order with the clip and time on each, for a reviewing agent (REVIEW.md, step 1).
  window   one moment in close-up for checking a suspect (REVIEW.md, step 2): a reference frame from a few seconds
           earlier in the same clip, then every frame from --before to --after around the moment.

Clips, sheets and findings go to rl/bughunt/out/<run>/ (not in git).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import imageio_ffmpeg
import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
TILE = (640, 360)

# A spread of characters for a first pass: big bosses, effect-heavy supers, helpers, flyers, grapplers, and the
# characters with known or recently fixed bugs.
DEFAULT_CHARACTERS = [
    "chars/thanos/thanos.def", "chars/darkseid/Darkseid.def", "chars/Galactus/Galactus.def",
    "chars/rogue/rogue.def", "chars/wildcat/wildcat.def", "chars/beast/Beast.def", "chars/shumag/shumag.def",
    "chars/spiderman/Spiderman.def", "chars/joker/joker.def", "chars/MVCI Venom/MVCI Venom.def",
    "chars/storm/Storm.def", "chars/magneto/Magneto.def",
]


def record(args):
    out = Path(args.out) / "clips"
    out.mkdir(parents=True, exist_ok=True)
    # The roster spells paths as select.def does; match without case so "chars/joker/joker.def" finds "Joker.def".
    import torch
    roster = {name.lower(): name for name in torch.load(ROOT / args.checkpoint, map_location="cpu",
                                                        weights_only=False)["characters"]}
    jobs = []
    for i, p2 in enumerate(args.characters or DEFAULT_CHARACTERS):
        if p2.lower() not in roster:
            print(f"{p2}: not in the roster, skipped", flush=True)
            continue
        p2 = roster[p2.lower()]
        stem = Path(p2).stem.replace(" ", "_")
        clip = out / f"{i:02d}_{stem}.mp4"
        if clip.exists() and not args.again:
            continue
        jobs.append((p2, clip, args.seed + i))

    def one(job):
        p2, clip, seed = job
        command = [sys.executable, str(ROOT / "rl" / "record_clip.py"), "--exe", args.exe, "--checkpoint", args.checkpoint,
                   "--p1", args.p1, "--p2", p2, "--stage", args.stage, "--seed", str(seed), "--random",
                   "--out", str(clip)]
        done = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        line = done.stdout.strip().splitlines()[-1] if done.stdout.strip() else done.stderr.strip()[-300:]
        print(f"{clip.name}: {line}", flush=True)
        return {"p1": args.p1, "p2": p2, "clip": str(clip), "result": line}

    with ThreadPoolExecutor(args.parallel) as pool:
        results = list(pool.map(one, jobs))
    (Path(args.out) / "clips.json").write_text(json.dumps(results, indent=1), encoding="utf-8")


def frames_of(clip: Path, size=(160, 90)):
    """Every frame of a clip, small and grey, and the clip's frame rate."""
    raw = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-loglevel", "error", "-i", str(clip), "-vf",
                          f"scale={size[0]}:{size[1]}", "-pix_fmt", "gray", "-f", "rawvideo", "-"],
                         capture_output=True, check=True).stdout
    every = np.frombuffer(raw, np.uint8).reshape(-1, size[1], size[0]).astype(np.float32)
    # record_clip writes one frame per decision at 20 fps.
    return list(every), 20


def frame_at(clip: Path, seconds: float) -> Image.Image:
    data = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-loglevel", "error", "-ss", f"{max(0.0, seconds):.3f}",
                           "-i", str(clip), "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"],
                          capture_output=True, check=True).stdout
    from io import BytesIO
    return Image.open(BytesIO(data)).convert("RGB")


def keyframes(clip: Path, every: float, gap: float, most: int):
    """Times worth looking at: a steady sample plus the biggest changes in the picture."""
    frames, fps = frames_of(clip)
    if len(frames) < 2:
        return [0.0]
    change = np.array([0.0] + [float(np.abs(b - a).mean()) for a, b in zip(frames, frames[1:])])
    length = len(frames) / fps
    times = [t for t in np.arange(0.5, length, every)]
    for i in np.argsort(-change):
        t = i / fps
        if change[i] < 4 or len(times) >= most:
            break
        if all(abs(t - u) >= gap for u in times):
            times.append(t)
    return sorted(times)


def label(image: Image.Image, text: str) -> Image.Image:
    image = image.resize(TILE)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 8 + 7 * len(text), 16), fill=(0, 0, 0))
    draw.text((4, 3), text, fill=(255, 255, 0))
    return image


def sheet(tiles, columns=3):
    rows = (len(tiles) + columns - 1) // columns
    out = Image.new("RGB", (TILE[0] * columns, TILE[1] * rows), (40, 40, 40))
    for k, tile in enumerate(tiles):
        out.paste(tile, ((k % columns) * TILE[0], (k // columns) * TILE[1]))
    return out


def sheets(args):
    run = Path(args.out)
    clips = sorted((run / "clips").glob("*.mp4"))
    target = run / "sheets"
    target.mkdir(parents=True, exist_ok=True)
    index = []
    for clip in clips:
        try:
            times = keyframes(clip, args.every, args.gap, args.most)
        except subprocess.CalledProcessError:
            print(f"{clip.name}: can't be read (cut off while recording?); record it again with --again", flush=True)
            continue
        tiles = [label(frame_at(clip, t), f"{clip.stem}  t={t:.1f}s") for t in times]
        for n in range(0, len(tiles), 6):
            name = f"{clip.stem}_{n // 6:02d}.png"
            sheet(tiles[n:n + 6]).save(target / name)
            index.append({"sheet": str(target / name), "clip": str(clip), "times": [round(t, 2) for t in times[n:n + 6]]})
        print(f"{clip.name}: {len(times)} keyframes, {(len(tiles) + 5) // 6} sheets", flush=True)
    (run / "sheets.json").write_text(json.dumps(index, indent=1), encoding="utf-8")
    print(f"{len(index)} sheets in {target}")


def window(args):
    clip = Path(args.clip)
    t = args.time
    times = list(np.arange(max(0.0, t - args.before), t + args.after + 1e-6, args.step))[:11]
    tiles = [label(frame_at(clip, max(0.0, t - args.reference)), f"REFERENCE t={max(0.0, t - args.reference):.1f}s")]
    tiles += [label(frame_at(clip, u), f"{clip.stem}  t={u:.2f}s") for u in times]
    out = Path(args.output or (clip.parent.parent / "windows" / f"{clip.stem}_{t:.1f}.png"))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet(tiles, columns=4).save(out)
    print(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    r = sub.add_parser("record")
    r.add_argument("--out", required=True)
    r.add_argument("--characters", nargs="*", help="player 2's characters (default: a spread of 12)")
    r.add_argument("--p1", default="chars/captainamerica/captainamerica.def")
    r.add_argument("--stage", default="stages/gotham.def")
    r.add_argument("--checkpoint", default="rl/checkpoints/cap_thanos_styles.pt", help="only for the roster")
    r.add_argument("--exe", default="build/pc_test/mugen360.exe")
    r.add_argument("--seed", type=int, default=500)
    r.add_argument("--parallel", type=int, default=6)
    r.add_argument("--again", action="store_true", help="record clips that already exist again")
    s = sub.add_parser("sheets")
    s.add_argument("--out", required=True)
    s.add_argument("--every", type=float, default=3.0, help="seconds between steady samples")
    s.add_argument("--gap", type=float, default=0.75, help="least seconds between two keyframes")
    s.add_argument("--most", type=int, default=24, help="keyframes per clip at most")
    w = sub.add_parser("window")
    w.add_argument("clip")
    w.add_argument("time", type=float)
    w.add_argument("--before", type=float, default=0.5)
    w.add_argument("--after", type=float, default=1.5)
    w.add_argument("--step", type=float, default=0.2)
    w.add_argument("--reference", type=float, default=4.0, help="seconds earlier for the reference frame")
    w.add_argument("--output")
    args = parser.parse_args()
    {"record": record, "sheets": sheets, "window": window}[args.command](args)


if __name__ == "__main__":
    main()
