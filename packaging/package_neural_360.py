"""Packs VS NEURAL CAP for the 360: the console build (run build_360.bat first) plus a trimmed copy of the MUGEN game
the policy trained in, as dist/mugen360_neural_360.zip.

The game copy (dist/mugen360/games/neuralcap/) holds only what the mode needs: the policy's character and the
opponents in data/neural/neural.ini, the stage, the screenpack and lifebars, fonts. Left out to save space and memory:
the intro movie (intro.sff, Intro2.sff), music, unused lifebars. Its select.def lists just these characters, so the
normal VS modes work with them too. data/mugen360.ini in the package points at the copy ([Game] game =).

The game's art belongs to its makers: this zip is for private play. Don't commit it or upload it anywhere public.

    rl\\.venv\\Scripts\\python.exe packaging\\package_neural_360.py ["DC vs MARVEL 2025"]

--full instead packs the whole game (every character; about 4 GB, a 2.3 GB zip) as dist/mugen360_dc_vs_marvel/ and
dist/mugen360_dc_vs_marvel_360.zip: today's engine (default.xex, data/ with the neural files) and the game under
games/<its folder name>, minus MUGEN's own Windows program. Files already there and unchanged aren't copied again.

    rl\\.venv\\Scripts\\python.exe packaging\\package_neural_360.py --full
"""

from __future__ import annotations

import argparse
import configparser
import os
import re
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
GAME = HERE / "DC vs MARVEL 2025"
DIST = HERE / "dist" / "mugen360"
TARGET = DIST / "games" / "neuralcap"
SKIP_DATA = {"intro.sff", "intro2.sff", "fightvf5.sff", "fightvf5.snd"}


def find(relative: str) -> Path:
    """A path in the game, matched without case (select.def and neural.ini don't always match the disk)."""
    here = GAME
    for part in Path(relative).parts:
        match = next((p for p in here.iterdir() if p.name.lower() == part.lower()), None)
        if match is None:
            raise SystemExit(f"{relative} isn't in {GAME}")
        here = match
    return here


# MUGEN's own program and logs: the 360 runs mugen360 instead.
SKIP_FULL = {"dc vs marvel.exe", "elecbyte.mugen.libs", "plugins", "mugen.log", "stdout.txt"}


def sync(source: Path, target: Path) -> int:
    """Copies a folder, skipping files already there with the same size and time."""
    copied = 0
    for root, folders, files in os.walk(source):
        folders[:] = [f for f in folders if f.lower() not in SKIP_FULL]
        out = target / Path(root).relative_to(source)
        out.mkdir(parents=True, exist_ok=True)
        for name in files:
            if name.lower() in SKIP_FULL:
                continue
            a, b = Path(root) / name, out / name
            if b.exists() and b.stat().st_size == a.stat().st_size and int(b.stat().st_mtime) == int(a.stat().st_mtime):
                continue
            shutil.copy2(a, b)
            copied += 1
    return copied


def full():
    """The whole game with today's engine."""
    package = HERE / "dist" / "mugen360_dc_vs_marvel"
    package.mkdir(parents=True, exist_ok=True)
    shutil.copy2(DIST / "default.xex", package / "default.xex")
    shutil.copy2(DIST / "README.txt", package / "README.txt")
    shutil.rmtree(package / "data", ignore_errors=True)
    shutil.copytree(DIST / "data", package / "data")
    (package / "save").mkdir(exist_ok=True)
    copied = sync(GAME, package / "games" / GAME.name)
    engine_ini = package / "data" / "mugen360.ini"
    text = engine_ini.read_text(encoding="utf-8")
    text = re.sub(r"^;?game =.*$", f"game = games/{GAME.name}", text, count=1, flags=re.MULTILINE)
    engine_ini.write_text(text, encoding="utf-8")
    print(f"{package}: engine updated, {copied} game files copied", flush=True)
    zip_path = HERE / "dist" / "mugen360_dc_vs_marvel_360"
    shutil.make_archive(str(zip_path), "zip", HERE / "dist", package.name)
    print(f"{zip_path}.zip: {Path(str(zip_path) + '.zip').stat().st_size / 1e9:.1f} GB (private: the game's art)")


def main():
    global GAME
    parser = argparse.ArgumentParser()
    parser.add_argument("game", nargs="?", default="DC vs MARVEL 2025")
    parser.add_argument("--full", action="store_true", help="the whole game, not just VS NEURAL CAP's characters")
    args = parser.parse_args()
    GAME = HERE / args.game
    if not (DIST / "default.xex").exists():
        raise SystemExit("run build_360.bat first")
    if args.full:
        full()
        return
    ini = configparser.ConfigParser()
    ini.read(HERE / "data" / "neural" / "neural.ini", encoding="utf-8")
    neural = ini["Neural"]
    characters = [neural["character"].strip()] + [neural[k].split(",")[0].strip()
                                                  for k in sorted(neural) if k.startswith("opponent")]
    # The policy's character can be an opponent too (the self-play mirror): each folder once.
    characters = list(dict.fromkeys(characters))
    stage = neural["stage"].strip()
    lines, stage_name = trim(TARGET, characters, stage)
    # The package plays this game.
    engine_ini = DIST / "data" / "mugen360.ini"
    text = engine_ini.read_text(encoding="utf-8")
    text = re.sub(r"^;?game =.*$", "game = games/neuralcap", text, count=1, flags=re.MULTILINE)
    engine_ini.write_text(text, encoding="utf-8")

    size = sum(f.stat().st_size for f in TARGET.rglob("*") if f.is_file())
    zip_path = HERE / "dist" / "mugen360_neural_360"
    shutil.make_archive(str(zip_path), "zip", HERE / "dist", "mugen360")
    print(f"{TARGET}: {size / 1e6:.0f} MB, characters: {', '.join(lines)}; stage {stage_name}")
    print(f"{zip_path}.zip: {Path(str(zip_path) + '.zip').stat().st_size / 1e6:.0f} MB (private: the game's art)")


def trim(target: Path, characters: list[str], stage: str) -> tuple[list[str], str]:
    """A copy of GAME with only these characters (.def paths as select.def names them) and one stage, at target: the
    screenpack, lifebars and fonts without the intro movie and music, and a select.def listing just them. Returns the
    select.def lines and the stage's file name. Shared with package_web.py."""
    shutil.rmtree(target, ignore_errors=True)
    target.mkdir(parents=True)
    # The screenpack and its files, without the intro movie and music.
    shutil.copytree(GAME / "data", target / "data",
                    ignore=lambda folder, names: [n for n in names if n.lower() in SKIP_DATA or n.lower().endswith(".mp3")])
    for folder in ("MVC Lifebars", "font"):
        shutil.copytree(GAME / folder, target / folder)
    # The characters, whole folders, under the names they have on disk.
    lines = []
    for def_path in characters:
        source = find(def_path)
        shutil.copytree(source.parent, target / "chars" / source.parent.name)
        lines.append(f"{source.parent.name}/{source.name}")
    # The stage: its .def and the files next to it that it names.
    stage_def = find(stage)
    (target / "stages").mkdir()
    shutil.copy2(stage_def, target / "stages" / stage_def.name)
    for name in re.findall(r"^\s*(?:spr|sprite|bgmusic)\s*=\s*([^;\r\n]+)", stage_def.read_text(errors="replace"),
                           re.IGNORECASE | re.MULTILINE):
        name = name.strip().strip('"')
        candidate = stage_def.parent / name
        if name and candidate.exists() and not name.lower().endswith(".mp3"):
            shutil.copy2(candidate, target / "stages" / candidate.name)
    # A select.def with just these.
    (target / "data" / "select.def").write_text(
        "; VS NEURAL CAP's trimmed roster (package_neural_360.py)\n[Characters]\n"
        + "".join(f"{line}, stages/{stage_def.name}\n" for line in lines)
        + f"\n[ExtraStages]\nstages/{stage_def.name}\n\n[Options]\narcade.maxmatches = 6,0,0,0,0,0,0,0,0,0\n"
          "team.maxmatches = 6,0,0,0,0,0,0,0,0,0\n", encoding="utf-8")
    # No intro movie (its sprites are left out).
    system = target / "data" / "system.def"
    text = system.read_text(encoding="latin-1")
    text = re.sub(r"^(\s*intro\.storyboard\s*=)[^;\r\n]*", r"\1 ", text, flags=re.IGNORECASE | re.MULTILINE)
    system.write_text(text, encoding="latin-1")
    return lines, stage_def.name


if __name__ == "__main__":
    main()
