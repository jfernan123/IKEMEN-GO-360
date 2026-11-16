"""Subprocess environment for mugen360's hidden RL protocol."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass
class Step:
    observation: np.ndarray
    opponent_observation: np.ndarray
    reward: float
    done: bool
    winner: int


class MugenEnv:
    def __init__(self, exe: str | Path, game: str | Path, p1: str, p2: str, stage: str, frame_skip: int = 3,
                 extra_args: list[str] | None = None):
        self.frame_skip = frame_skip
        args = [str(Path(exe).resolve()), "--rl-server", "--wins", "1", "--game", str(Path(game).resolve()),
                "--p1", p1, "--p2", p2, "--stage", stage, "--ai", "smart", *(extra_args or [])]
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        self.process = subprocess.Popen(args, cwd=Path(exe).resolve().parents[2], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                        encoding="utf-8", errors="replace", bufsize=1, creationflags=flags)
        ready = self._read("RL_READY")
        fields = ready.split()
        self.observation_size, self.direction_count, self.button_count = map(int, fields[1:4])

    def _read(self, prefix: str) -> str:
        assert self.process.stdout
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError(f"mugen360 stopped with code {self.process.poll()}")
            line = line.strip()
            if line.startswith("RL_ERROR"):
                raise RuntimeError(line)
            if line.startswith(prefix):
                return line

    def _send(self, line: str) -> Step:
        assert self.process.stdin
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()
        fields = self._read("RL_OBS").split()
        reward, done, winner = float(fields[1]), bool(int(fields[2])), int(fields[3])
        values = np.asarray(fields[4:], dtype=np.float32)
        expected = self.observation_size * 2
        if values.size != expected:
            raise RuntimeError(f"protocol returned {values.size} values, expected {expected}")
        return Step(values[:self.observation_size], values[self.observation_size:], reward, done, winner)

    def reset(self, seed: int, external_opponent: bool = False, opponent_life: float = 1.0) -> Step:
        """opponent_life: player 2's life bar as a fraction (a curriculum's handicap)."""
        return self._send(f"reset {seed} {'external' if external_opponent else 'smart'} {opponent_life}")

    def step(self, action: tuple[int, int], opponent_action: tuple[int, int] = (0, 0)) -> Step:
        return self._send(f"step {action[0]} {action[1]} {opponent_action[0]} {opponent_action[1]} {self.frame_skip}")

    def close(self):
        if self.process.poll() is None:
            try:
                assert self.process.stdin
                self.process.stdin.write("quit\n")
                self.process.stdin.flush()
                self.process.wait(timeout=5)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self.process.kill()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
