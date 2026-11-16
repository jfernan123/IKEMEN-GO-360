"""Runs a from-scratch Cap specialist for every character, three at a time, once the merge has finished: when one finishes
the next starts, so the PC stays as busy as it safely can. A run that stops early doesn't stop the others. It counts every
trainer on the machine, so it can be restarted while runs are going: a specialist already running or already saved is left
alone. Each run writes its own log; this writes rl/checkpoints/scratch_queue.log.

    rl\scripts\train_scratch_all.bat
"""

from __future__ import annotations

import csv
import re
import shutil
import subprocess
import time
from pathlib import Path

AT_ONCE = 3
THREADS = 3  # per pool, two pools a specialist: 18 threads over the 16 the CPU has
WAIT_FOR = "cap_merged"  # takes the whole machine, so nothing new starts while it runs
MIN_FREE_GB = 30  # nothing new starts below this; the runs going carry on
ROOT = Path(__file__).resolve().parent.parent
LOG = ROOT / "rl" / "checkpoints" / "scratch_queue.log"
SPECIALISTS = [("ivy_vs_cap", "chars/captainamerica/captainamerica.def"),  # Ivy learns, not Cap: see EXTRA
               ("thanos", "chars/thanos/thanos.def"), ("doomsday", "chars/doomsdayCG/doomsdayCG.def"),
               ("cap", "chars/captainamerica/captainamerica.def"), ("galactus", "chars/GALACTUS/GALACTUS.def"),
               ("superman", "chars/Superman/Superman.def"), ("darkseid", "chars/darkseid/darkseid.def")]
EXTRA = {"ivy_vs_cap": '"--p1=chars/poisonivy_G/poisonivy_G.def"'}  # more train_pool options for one run


def everyone() -> list[tuple[str, str]]:
    """The named specialists, then the rest of the roster, toughest first (rl/toughest_scan.csv)."""
    with (ROOT / "rl" / "toughest_scan.csv").open(encoding="utf-8") as f:
        rows = sorted(csv.DictReader(f), key=lambda row: (float(row["win_rate"]), -float(row["opponent_life"])))
    named = {opponent for _, opponent in SPECIALISTS}
    rest = [row["character"] for row in rows if row["character"] not in named]
    return SPECIALISTS + [(re.sub(r"\W", "", Path(opponent).stem).lower(), opponent) for opponent in rest]


def note(text: str) -> None:
    line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} {text}"
    print(line, flush=True)
    with LOG.open("a", encoding="utf-8") as f:
        f.write(line + chr(10))


def trainers() -> set[str]:
    """The checkpoint names of the trainers running now."""
    lines = subprocess.run(["powershell", "-NoProfile", "-Command",
                            "(Get-CimInstance Win32_Process -Filter \"Name='python.exe'\").CommandLine"],
                           capture_output=True, text=True).stdout
    return set(re.findall(r"train_pool\.py.*--checkpoint \S*?([\w-]+)\.pt", lines))


def main():
    running = trainers()
    waiting = [(name, opponent) for name, opponent in everyone()
               if not (ROOT / "rl" / "checkpoints" / f"scratch_vs_{name}.pt").is_file()]
    note(f"running {sorted(running)}, waiting {[name for name, _ in waiting]}")
    while waiting or running:
        now = trainers()
        for name in sorted(running - now):
            note(f"finished {name}")
        running = now
        room = shutil.disk_usage(ROOT).free / 2 ** 30 >= MIN_FREE_GB
        if waiting and len(running) < AT_ONCE and WAIT_FOR not in running and room:
            name, opponent = waiting.pop(0)
            # Don't edit the launcher while runs are inside it: cmd reads a batch file as it runs.
            subprocess.Popen(rf'cmd /c rl\scripts\train_cap_specialist.bat {name} "{opponent}" {THREADS} {EXTRA.get(name, "")}', cwd=ROOT)
            note(f"started {name}")
            time.sleep(60)  # until its trainer shows up
            continue
        time.sleep(30)
    note("all runs finished")


if __name__ == "__main__":
    main()
