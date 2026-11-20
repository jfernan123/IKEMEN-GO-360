"""Writes a trained policy for the game's neural CPU (core/ai/neural_policy.cpp): the network's weights without PyTorch.

Format, little-endian: "M3NN", u32 version (1), u32 observation size, u32 hidden size, u32 skills, the roster
(u32 count, then each character's .def path as u16 length + bytes, in the checkpoint's id order), then the tensors
(u32 count, then each: u16 name length + name, u32 rows, u32 columns, rows * columns float32). A 1-D tensor is
rows x 1. Only "large" networks (encoder + 2 skip blocks + GRU + two-layer heads).

    python rl/export_policy.py rl/checkpoints/pool_cap_vs5.pt data/neural/cap.m3nn
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import torch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint")
    parser.add_argument("out")
    parser.add_argument("--reference", help="also write PyTorch's scores on two fixed random steps here, for the "
                                            "engine's unit test (tests/neural_test.cpp)")
    args = parser.parse_args()
    saved = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    if saved.get("size", "small") != "large":
        raise SystemExit("only large networks can be exported")
    model = saved["model"]
    out = bytearray(b"M3NN")
    out += struct.pack("<IIII", 1, saved["observation_size"], saved.get("hidden_size") or 256, saved.get("skills", 0))
    characters = list(saved["characters"])
    out += struct.pack("<I", len(characters))
    for name in characters:
        data = name.encode("utf-8")
        out += struct.pack("<H", len(data)) + data
    out += struct.pack("<I", len(model))
    for name, tensor in model.items():
        tensor = tensor.detach().float().contiguous()
        rows, cols = (tensor.shape[0], 1) if tensor.dim() == 1 else (tensor.shape[0], tensor[0].numel())
        data = name.encode("utf-8")
        out += struct.pack("<H", len(data)) + data + struct.pack("<II", rows, cols)
        out += tensor.numpy().astype("<f4").tobytes()
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_bytes(bytes(out))
    if args.reference:
        write_reference(saved, args.reference)
    print(f"{args.out}: {len(out) / 1e6:.2f} MB, {len(model)} tensors, {len(characters)} characters, "
          f"{saved['steps']:,} decisions trained")


def write_reference(saved, path):
    """Lines: self id, opponent id; then per step the observation, the direction scores and the button scores."""
    from model import policy_from_checkpoint
    policy = policy_from_checkpoint(saved).eval()
    ids = {name.lower(): i for i, name in enumerate(saved["characters"])}
    me, them = ids["chars/captainamerica/captainamerica.def"], ids["chars/thanos/thanos.def"]
    generator = torch.Generator().manual_seed(7)
    hidden = policy.initial_hidden(1, "cpu")
    lines = [f"{me} {them}"]
    for _ in range(2):
        observation = torch.rand((1, saved["observation_size"]), generator=generator) * 2 - 1
        with torch.no_grad():
            directions, buttons, _, hidden = policy(observation, torch.tensor([me]), torch.tensor([them]), hidden,
                                                    torch.zeros(1, dtype=torch.long) if policy.skills else None)
        for row in (observation[0], directions[0], buttons[0]):
            lines.append(" ".join(f"{v:.7g}" for v in row.tolist()))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
