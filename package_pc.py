"""Packs mugen360 for another Windows PC with a MUGEN game, as dist/mugen360_pc_<game>.zip: the optimized PC build
(build/pc, run build_pc.bat first), SDL3 and the Visual C++ runtime next to it (so nothing needs installing), data/
with the neural files, and the whole game folder. data/mugen360.ini in the package points at the game, and Play.bat
starts it.

The game's art belongs to its makers: this zip is for private sharing. Don't commit it or upload it anywhere public.

    rl\\.venv\\Scripts\\python.exe package_pc.py ["DC vs MARVEL 2025"]

Files already in the package and unchanged aren't copied again.
"""

from __future__ import annotations

import argparse
import glob
import re
import shutil
from pathlib import Path

from package_neural_360 import sync

HERE = Path(__file__).resolve().parent
BUILD = HERE / "build" / "pc"
# The Visual C++ runtime the build links against, deployed next to the exe (Microsoft allows app-local copies).
REDIST = "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
RUNTIME = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")

README = """mugen360 - {game}
=====================================

Double-click Play.bat to start. (mugen360.exe on its own works too.)

VS NEURAL CAP: on the title menu (it takes the TEAM ARCADE row), fight a Captain America played by a neural network
trained with reinforcement learning. Pick your character from the list; the AI Cap is player 1, you are player 2.

Keyboard
  Arrows / WASD        move
  U I O  (or C V E)    punches: light, medium, heavy
  J K L  (or Z X)      kicks: light, medium, heavy
  Enter                start / taunt
  Esc                  back (fight > select > title); on the title, EXIT
  Q                    show hitboxes and debug info
An Xbox controller works too: X Y RB punches, A B RT kicks, Start, Back = Esc.
Options (on the title menu) change the controls, difficulty, round time and volume; they save to save/config.ini.

Captain America's moves: Shield Throw = down, down-forward, forward + punch (toward the opponent).
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("game", nargs="?", default="DC vs MARVEL 2025")
    args = parser.parse_args()
    game = HERE / args.game
    if not (BUILD / "mugen360.exe").exists():
        raise SystemExit("run build_pc.bat first")
    if not game.is_dir():
        raise SystemExit(f"{game} isn't there")
    slug = re.sub(r"[^a-z0-9]+", "_", game.name.lower()).strip("_")
    package = HERE / "dist" / f"mugen360_pc_{slug}"
    package.mkdir(parents=True, exist_ok=True)

    for name in ("mugen360.exe", "SDL3.dll"):
        shutil.copy2(BUILD / name, package / name)
    folders = sorted(glob.glob(REDIST))
    if not folders:
        raise SystemExit("the Visual C++ redistributable folder isn't there: " + REDIST)
    for name in RUNTIME:
        shutil.copy2(Path(folders[-1]) / name, package / name)

    shutil.rmtree(package / "data", ignore_errors=True)
    shutil.copytree(HERE / "data", package / "data")
    engine_ini = package / "data" / "mugen360.ini"
    text = engine_ini.read_text(encoding="utf-8")
    text = re.sub(r"^;?game =.*$", f"game = {game.name}", text, count=1, flags=re.MULTILINE)
    engine_ini.write_text(text, encoding="utf-8")
    (package / "save").mkdir(exist_ok=True)
    (package / "save" / "README.txt").write_text("mugen360 keeps its options here (config.ini).\n", encoding="utf-8")
    (package / "Play.bat").write_text(f'@echo off\r\ncd /d "%~dp0"\r\nstart "" mugen360.exe --game "{game.name}"\r\n',
                                      encoding="ascii", newline="")
    (package / "README.txt").write_text(README.format(game=game.name).replace("\n", "\r\n"), encoding="utf-8",
                                        newline="")
    copied = sync(game, package / game.name)
    print(f"{package}: engine and data updated, {copied} game files copied", flush=True)

    zip_path = HERE / "dist" / package.name
    shutil.make_archive(str(zip_path), "zip", HERE / "dist", package.name)
    print(f"{zip_path}.zip: {Path(str(zip_path) + '.zip').stat().st_size / 1e9:.1f} GB (private: the game's art)")


if __name__ == "__main__":
    main()
