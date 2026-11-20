"""Packs VS NEURAL CAP for the browser build: the game's files as dist/web/game.data + game.data.js (Emscripten's file
packager), next to the engine build_web.bat put there. dist/web/ is then the whole site: upload it as is (Vercel,
any static host).

What goes in: our data/ (without the other neural networks and Ikemen's own screenpack, which the game replaces),
data/neural/neural_web.ini as the page's neural.ini, and a trimmed copy of the game with only the policy's character,
the opponents in that ini and its stage (package_neural_360.trim). The page caches the package in the browser
(IndexedDB), so a second visit doesn't download it again.

The game's art belongs to its makers.

    rl\\.venv\\Scripts\\python.exe packaging\\package_web.py ["DC vs MARVEL 2025"] [--ini data/neural/neural_web.ini]
"""

from __future__ import annotations

import argparse
import configparser
import os
import re
import shutil
import subprocess
from pathlib import Path

import package_neural_360 as neural

HERE = Path(__file__).resolve().parent.parent
WEB = HERE / "dist" / "web"
ROOT = HERE / "dist" / "web_root"  # the files as the page sees them, packed into game.data
# Our data/ without what the game makes unneeded: Ikemen's screenpack (the game has its own) and, in data/neural/,
# every network but the policy's.
SKIP_OURS = {"ikemen1"}


def emsdk() -> Path:
    root = Path(os.environ.get("EMSDK_ROOT", Path.home() / "emsdk"))
    if not (root / "upstream" / "emscripten" / "tools" / "file_packager.py").is_file():
        raise SystemExit(f"Emscripten isn't at {root} (see build_web.bat)")
    return root


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("game", nargs="?", default="DC vs MARVEL 2025")
    parser.add_argument("--ini", default="data/neural/neural_web.ini", help="the VS NEURAL CAP settings to pack")
    parser.add_argument("--budget", type=float, default=96.0,
                        help="MB the whole site may take (Vercel's free plan: 100 MB a deployment)")
    args = parser.parse_args()
    neural.GAME = HERE / args.game
    if not (WEB / "mugen360.wasm").is_file():
        raise SystemExit("run build_web.bat first")

    ini = configparser.ConfigParser()
    ini.read(HERE / args.ini, encoding="utf-8")
    section = ini["Neural"]
    policy = section["policy"].strip()
    characters = [section["character"].strip()] + [section[k].split(",")[0].strip()
                                                   for k in sorted(section) if k.startswith("opponent")]
    characters = list(dict.fromkeys(characters))

    shutil.rmtree(ROOT, ignore_errors=True)
    data = ROOT / "data"
    shutil.copytree(HERE / "data", data, ignore=lambda folder, names: [
        n for n in names if n in SKIP_OURS
        or (Path(folder).name == "neural" and n.endswith(".m3nn") and f"data/neural/{n}" != policy)
        or (Path(folder).name == "neural" and n.endswith(".ini"))])
    shutil.copy2(HERE / args.ini, data / "neural" / "neural.ini")
    lines, stage = neural.trim(ROOT / "games" / "neuralcap", characters, section["stage"].strip())
    engine_ini = data / "mugen360.ini"
    text = engine_ini.read_text(encoding="utf-8")
    text = re.sub(r"^;?game =.*$", "game = games/neuralcap", text, count=1, flags=re.MULTILINE)
    engine_ini.write_text(text, encoding="utf-8")

    sdk = emsdk()
    python = next((sdk / "python").glob("*/python.exe"))
    for old in ("game.data", "game.data.js"):
        (WEB / old).unlink(missing_ok=True)
    subprocess.run([str(python), str(sdk / "upstream" / "emscripten" / "tools" / "file_packager.py"),
                    str(WEB / "game.data"), "--preload", f"{ROOT}@/", "--use-preload-cache",
                    f"--js-output={WEB / 'game.data.js'}"], check=True)

    sizes = {f.name: f.stat().st_size / 1e6 for f in WEB.iterdir() if f.is_file()}
    total = sum(sizes.values())
    print(f"{', '.join(lines)} on {stage}; policy {policy}")
    print("dist/web: " + ", ".join(f"{n} {s:.1f} MB" for n, s in sorted(sizes.items())) + f"; {total:.1f} MB in all")
    if total > args.budget:
        raise SystemExit(f"over the {args.budget:.0f} MB budget: take out an opponent (Vercel's free plan stops at 100 MB)")


if __name__ == "__main__":
    main()
