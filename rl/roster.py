"""Find playable character and stage definitions in a mounted MUGEN game."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Roster:
    characters: tuple[str, ...]
    stages: tuple[str, ...]


def _lines(path: Path):
    return path.read_text(encoding="utf-8", errors="ignore").splitlines()


def _find_select(root: Path) -> Path:
    candidates = (root / "data/select.def", root / "select.def")
    for path in candidates:
        if path.is_file():
            return path
    found = list(root.rglob("select.def"))
    if not found:
        raise FileNotFoundError(f"No select.def below {root}")
    return found[0]


def _character(root: Path, entry: str) -> str | None:
    entry = entry.replace("\\", "/").strip()
    if entry.lower().endswith(".def"):
        guesses = (root / entry, root / "chars" / entry, root / "data" / entry)
    elif "/" in entry:
        guesses = (root / "chars" / f"{entry}.def", root / f"{entry}.def")
    else:
        guesses = (root / "chars" / entry / f"{entry}.def", root / "data/chars" / entry / f"{entry}.def")
    for path in guesses:
        if path.is_file():
            return path.relative_to(root).as_posix()
    return None


def _stage(root: Path, entry: str) -> str | None:
    entry = entry.replace("\\", "/").strip()
    guesses = (root / entry, root / "stages" / entry, root / "data" / entry, root / "data/stages" / entry)
    for path in guesses:
        if path.is_file():
            return path.relative_to(root).as_posix()
    return None


def discover(game: str | Path) -> Roster:
    root = Path(game).resolve()
    select = _find_select(root)
    section = ""
    characters: list[str] = []
    stages: list[str] = []
    for raw in _lines(select):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            section = line.lower()
            continue
        first = line.split(",", 1)[0].strip()
        if section == "[characters]" and first.lower() not in {"randomselect", "dummyslot", "skipslot"}:
            found = _character(root, first)
            if found and found not in characters:
                characters.append(found)
        elif section in {"[extrastages]", "[stages]"}:
            found = _stage(root, first)
            if found and found not in stages:
                stages.append(found)
    if not characters:
        raise ValueError(f"No playable characters from {select}")
    if not stages:
        stage_dirs = (root / "stages", root / "data/stages")
        for directory in stage_dirs:
            if directory.is_dir():
                stages.extend(path.relative_to(root).as_posix() for path in directory.glob("*.def"))
    if not stages:
        raise ValueError(f"No stages in {root}")
    return Roster(tuple(characters), tuple(dict.fromkeys(stages)))
