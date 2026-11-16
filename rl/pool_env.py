"""Many headless fights in one mugen360 process (--rl-pool), stepped together through a binary protocol.

One MugenPool is one process and one matchup. Fights reset themselves when a match ends: step() reports the finished
match (done, winner, final reward) and returns the next match's first observation for that fight.
See runRlPool in src/main.cpp for the protocol.
"""

from __future__ import annotations

import struct
import subprocess
from pathlib import Path

import numpy as np


class MugenPool:
    def __init__(self, exe: str | Path, game: str | Path, p1: str, p2: str, stage: str, count: int,
                 threads: int | None = None, frame_skip: int = 3):
        self.count = count
        self.frame_skip = frame_skip
        args = [str(Path(exe).resolve()), "--rl-pool", str(count), "--wins", "1", "--game", str(Path(game).resolve()),
                "--p1", p1, "--p2", p2, "--stage", stage, "--ai", "smart"]
        if threads:
            args += ["--rl-threads", str(threads)]
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        self.process = subprocess.Popen(args, cwd=Path(exe).resolve().parents[2], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0,
                                        creationflags=flags)
        # Startup log lines come out on stdout before the pool takes it over; the binary stream starts at "RLPL".
        seen = b""
        while not seen.endswith(b"RLPL"):
            seen = (seen + self._read(1))[-4:]
        count_back, self.observation_size, self.direction_count, self.button_count = struct.unpack("<4i", self._read(16))
        if count_back != count:
            raise RuntimeError(f"pool started {count_back} fights, asked for {count}")
        self._obs_bytes = count * 2 * self.observation_size * 4

    def _read(self, size: int) -> bytes:
        data = bytearray()
        while len(data) < size:
            chunk = self.process.stdout.read(size - len(data))
            if not chunk:
                raise RuntimeError(f"mugen360 pool stopped with code {self.process.poll()}")
            data += chunk
        return bytes(data)

    def _observations(self) -> np.ndarray:
        return np.frombuffer(self._read(self._obs_bytes), np.float32).reshape(self.count, 2, self.observation_size)

    def reset(self, seed: int, external: bool = False) -> np.ndarray:
        """Resets every fight (fight i plays seed + i). Returns observations [count, 2, size]."""
        self.process.stdin.write(b"R" + struct.pack("<IB", seed & 0xFFFFFFFF, 1 if external else 0))
        self.process.stdin.flush()
        return self._observations()

    def set_opponent_life(self, fraction: float):
        """Player 2's life bar, as a fraction of its usual size, from each fight's next match on (a curriculum's
        handicap; reset() starts every fight with it)."""
        self.process.stdin.write(b"L" + struct.pack("<f", fraction))
        self.process.stdin.flush()

    def send_step(self, actions: np.ndarray):
        """Starts a step: actions is uint8 [count, 4] (direction1, button1, direction2, button2)."""
        self.process.stdin.write(b"S" + bytes([self.frame_skip]) + np.ascontiguousarray(actions, np.uint8).tobytes())
        self.process.stdin.flush()

    def receive_step(self):
        """Finishes a step: rewards [count], done [count] (bool), winners [count], observations [count, 2, size]."""
        rewards = np.frombuffer(self._read(self.count * 4), np.float32)
        done = np.frombuffer(self._read(self.count), np.uint8).astype(bool)
        winners = np.frombuffer(self._read(self.count), np.int8)
        return rewards, done, winners, self._observations()

    def step(self, actions: np.ndarray):
        self.send_step(actions)
        return self.receive_step()

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write(b"Q")
                self.process.stdin.flush()
                self.process.wait(timeout=10)
            except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
                self.process.kill()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
