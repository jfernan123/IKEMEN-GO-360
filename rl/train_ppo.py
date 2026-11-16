"""Train one character-conditioned recurrent PPO policy on parallel random Marvel matchups."""

from __future__ import annotations

import argparse
import concurrent.futures
import copy
import json
import math
import random
import time
from collections import Counter, deque
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import torch
from torch.distributions import Categorical
from torch.utils.tensorboard import SummaryWriter

from model import Policy
from monitor import MATCHUPS, TrainingMonitor
from mugen_env import MugenEnv
from roster import discover


DIRECTION_NAMES = ("N", "F", "B", "D", "DF", "DB", "U", "UF", "UB")
BUTTON_NAMES = ("", "x", "y", "z", "a", "b", "c", "Start")


def action_name(action: int) -> str:
    direction, button = divmod(action, len(BUTTON_NAMES))
    parts = []
    if direction:
        parts.append(DIRECTION_NAMES[direction])
    if button:
        parts.append(BUTTON_NAMES[button])
    return "+".join(parts) or "neutral"


def sequence_table(counts: Counter, limit: int = 12) -> str:
    total = sum(counts.values())
    lines = ["| Rank | Relative input sequence | Count | Share |", "|---:|---|---:|---:|"]
    for rank, (sequence, count) in enumerate(counts.most_common(limit), 1):
        label = " -> ".join(action_name(action) for action in sequence)
        lines.append(f"| {rank} | {label} | {count} | {count / max(1, total):.1%} |")
    return "\n".join(lines)


def choose(logits_d, logits_b):
    # Argument validation costs more than sampling at these batch sizes, and logits are always valid.
    dd, db = Categorical(logits=logits_d, validate_args=False), Categorical(logits=logits_b, validate_args=False)
    direction, button = dd.sample(), db.sample()
    return direction, button, dd.log_prob(direction) + db.log_prob(button)


@dataclass
class Slot:
    env: MugenEnv
    p1: str
    p2: str
    stage: str
    seed: int
    external: bool
    state: object
    hidden: torch.Tensor
    opponent_hidden: torch.Tensor
    opponent: Policy | None
    episode_return: float = 0.0
    episode_length: int = 0
    actions: list = None

    def __post_init__(self):
        self.actions = []


def input_line(frame_skip, direction, button, facing):
    keys = []
    if direction in (6, 7, 8): keys.append("U")
    if direction in (3, 4, 5): keys.append("D")
    if direction in (1, 4, 7): keys.append("R" if facing >= 0 else "L")
    if direction in (2, 5, 8): keys.append("L" if facing >= 0 else "R")
    if button: keys.append(("", "x", "y", "z", "a", "b", "c", "s")[button])
    return f"{frame_skip} {''.join(keys) or '-'}"


def save_win(slot, directory, game, frame_skip, step, episode):
    directory.mkdir(parents=True, exist_ok=True)
    safe1 = ''.join(c if c.isalnum() else '_' for c in Path(slot.p1).stem)
    safe2 = ''.join(c if c.isalnum() else '_' for c in Path(slot.p2).stem)
    base = directory / f"win_{step:09d}_{episode:06d}_{safe1}_vs_{safe2}"
    p1_path = base.with_name(base.name + "_p1.txt")
    p2_path = base.with_name(base.name + "_p2.txt")
    p1_path.write_text("\n".join(input_line(frame_skip, a[0], a[1], a[2]) for a in slot.actions) + "\n")
    if slot.external:
        p2_path.write_text("\n".join(input_line(frame_skip, a[3], a[4], a[5]) for a in slot.actions) + "\n")
    metadata = {"game": str(Path(game).resolve()), "p1": slot.p1, "p2": slot.p2, "stage": slot.stage,
                "seed": slot.seed, "frame_skip": frame_skip, "decisions": len(slot.actions),
                "opponent": "selfplay" if slot.external else "smart", "return": slot.episode_return,
                "p1_input": str(p1_path.resolve()), "p2_input": str(p2_path.resolve()) if slot.external else ""}
    base.with_suffix(".json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return base.with_suffix(".json")


def widen_observation(model, old_size, new_size):
    """An older checkpoint's weights for a larger observation. Both layouts are [fighter][fighter][5 shared values], and
    the newer one only adds values at the end of each fighter's block (its helpers and projectiles), so every old input
    keeps its weights and each added input starts at zero: the widened network plays exactly like the old one until
    training teaches it to use them."""
    old_fighter, new_fighter = (old_size - 5) // 2, (new_size - 5) // 2
    if new_fighter < old_fighter or old_size - 2 * old_fighter != 5 or new_size - 2 * new_fighter != 5:
        raise ValueError(f"can't widen a {old_size}-value observation to {new_size}")
    weight = model["encoder.0.weight"]
    widened = weight.new_zeros((weight.shape[0], weight.shape[1] - old_size + new_size))
    for side in range(2):
        widened[:, side * new_fighter:side * new_fighter + old_fighter] =             weight[:, side * old_fighter:(side + 1) * old_fighter]
    widened[:, 2 * new_fighter:new_size] = weight[:, 2 * old_fighter:old_size]
    widened[:, new_size:] = weight[:, old_size:]  # the two character embeddings
    model = dict(model)
    model["encoder.0.weight"] = widened
    return model


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", required=True, help="Marvel MUGEN game folder")
    parser.add_argument("--exe", default="build/pc/mugen360.exe")
    parser.add_argument("--steps", type=int, default=2_000_000, help="total decisions across all environments")
    parser.add_argument("--envs", type=int, default=8, help="simultaneous hidden fights")
    parser.add_argument("--rollout", type=int, default=256, help="decisions per environment before a PPO update")
    parser.add_argument("--frame-skip", type=int, default=3)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--seed", type=int, default=360)
    parser.add_argument("--checkpoint", default="rl/checkpoints/marvel_policy.pt")
    parser.add_argument("--self-play-after", type=int, default=300_000)
    parser.add_argument("--self-play-chance", type=float, default=0.0,
                        help="chance of a frozen-policy opponent; keep 0 until the SmartAI evaluation target passes")
    parser.add_argument("--snapshot-every", type=int, default=100_000)
    parser.add_argument("--logdir", default="rl/runs", help="TensorBoard run directory")
    parser.add_argument("--replay-dir", default="rl/wins", help="saved winning-match action replays")
    parser.add_argument("--monitor", action=argparse.BooleanOptionalAction, default=True,
                        help="run four background matchup counters")
    parser.add_argument("--monitor-max-decisions", type=int, default=2500,
                        help="resolve an overlong diagnostic match by remaining life")
    parser.add_argument("--resume", action="store_true", help="continue from --checkpoint")
    parser.add_argument("--gamma", type=float, default=.99, help="discount per decision")
    parser.add_argument("--entropy-coef", type=float, default=.01, help="reward for keeping actions random")
    parser.add_argument("--minibatch", type=int, default=1024, help="decisions per gradient step")
    parser.add_argument("--lr", type=float, default=3e-4, help="Adam learning rate")
    parser.add_argument("--size", choices=("small", "large"), default="small",
                        help="network size for a new run (a resumed run keeps its checkpoint's)")
    parser.add_argument("--epochs", type=int, default=4, help="passes over each rollout per update")
    parser.add_argument("--target-kl", type=float, default=0.0,
                        help="adapt the learning rate to keep each update's approximate KL near this (0: fixed rate)")
    parser.add_argument("--bptt", type=int, default=32,
                        help="decisions per training sequence for the GRU (1 trains each step alone, as before)")
    parser.add_argument("--init-from", help="start a new run from this checkpoint's weights (not its optimizer or "
                                             "counters); ignored with --resume when --checkpoint exists")
    parser.add_argument("--p1", help="fixed player 1 character (with --p2 and --stage) instead of random matchups")
    parser.add_argument("--p2", help="fixed player 2 character")
    parser.add_argument("--stage", help="fixed stage (alone, random pairs fight only on it)")
    parser.add_argument("--characters", help="comma-separated character pool for random matchups; with --p1 alone, "
                                              "the opponents' pool")
    args = parser.parse_args()
    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    device = torch.device("cuda" if args.device == "cuda" or
                          (args.device == "auto" and torch.cuda.is_available()) else "cpu")
    roster = discover(args.game)
    ids = {path: i for i, path in enumerate(roster.characters)}
    checkpoint = Path(args.checkpoint)
    checkpoint.parent.mkdir(parents=True, exist_ok=True)
    run_dir = Path(args.logdir) / time.strftime("%Y%m%d-%H%M%S")
    writer = SummaryWriter(run_dir)
    writer.add_text("run/config", "\n".join(f"- **{key}**: {value}" for key, value in vars(args).items()), 0)
    writer.add_text("run/hardware", f"device={device}; torch={torch.__version__}; characters={len(roster.characters)}", 0)
    snapshots: list[dict] = []
    saved = None
    total = episode = 0

    fixed_matchup = (args.p1, args.p2, args.stage) if args.p1 and args.p2 and args.stage else None
    # The policy keeps an embedding for every roster character, so a pool run can later widen to the whole game.
    pool_characters = [name.strip() for name in args.characters.split(",")] if args.characters else list(roster.characters)
    for name in (fixed_matchup[:2] if fixed_matchup else pool_characters):
        if name not in ids:
            raise ValueError(f"{name} is not in this game's roster")

    # --p1 without --p2: the policy always plays that character, against random opponents from the pool.
    fixed_p1 = args.p1 if args.p1 and not fixed_matchup else None
    if fixed_p1 and fixed_p1 not in ids:
        raise ValueError(f"{fixed_p1} is not in this game's roster")

    def matchup():
        if fixed_matchup:
            return fixed_matchup
        p1, p2 = random.choices(pool_characters, k=2)
        if fixed_p1:
            p1 = fixed_p1
        return p1, p2, args.stage or random.choice(roster.stages)

    # The first process establishes the protocol shape before the shared policy and other workers are made.
    p1, p2, stage = matchup()
    first_env = MugenEnv(args.exe, args.game, p1, p2, stage, args.frame_skip)
    resume = torch.load(checkpoint, map_location=device, weights_only=False)         if args.resume and checkpoint.is_file() else None
    size = resume.get("size", "small") if resume else args.size
    policy = Policy(first_env.observation_size, len(roster.characters), size=size).to(device)
    optimizer = torch.optim.Adam(policy.parameters(), lr=args.lr)
    print(f"network: {size}, {sum(p.numel() for p in policy.parameters()):,} parameters")
    if resume:
        saved = resume
        if saved["characters"] != list(roster.characters) or saved["observation_size"] != first_env.observation_size:
            raise ValueError("checkpoint roster or observation format does not match this game")
        policy.load_state_dict(saved["model"])
        optimizer.load_state_dict(saved["optimizer"])
        if args.target_kl <= 0:
            for group in optimizer.param_groups:
                group["lr"] = args.lr
        total = int(saved["steps"])
        episode = int(saved.get("episodes", 0))
        snapshots = [{key: value.detach().cpu() for key, value in state.items()}
                     for state in saved.get("snapshots", [])]
        print(f"resumed {checkpoint} at {total} decisions")
    elif args.init_from:
        start = torch.load(args.init_from, map_location=device, weights_only=False)
        if start["characters"] != list(roster.characters):
            raise ValueError("--init-from roster does not match this game")
        if start.get("size", "small") != size:
            raise ValueError(f"--init-from is a {start.get('size', 'small')} network; this run is {size}")
        model = start["model"]
        if start["observation_size"] != first_env.observation_size:
            model = widen_observation(model, start["observation_size"], first_env.observation_size)
        policy.load_state_dict(model)
        print(f"initialized from {args.init_from}")

    def new_slot(index: int, env: MugenEnv | None = None, fixed=None):
        nonlocal episode
        a, b, arena = fixed or matchup()
        external = bool(total >= args.self_play_after and snapshots and random.random() < args.self_play_chance)
        opponent = None
        if external:
            opponent = Policy(first_env.observation_size, len(roster.characters), policy.hidden_size,
                              policy.size).to(device)
            opponent.load_state_dict(random.choice(snapshots))
            opponent.eval()
        env = env or MugenEnv(args.exe, args.game, a, b, arena, args.frame_skip)
        reset_seed = args.seed + index + episode * args.envs
        state = env.reset(reset_seed, external)
        return Slot(env, a, b, arena, reset_seed, external, state, policy.initial_hidden(1, device),
                    policy.initial_hidden(1, device), opponent)

    slots = [new_slot(0, first_env, (p1, p2, stage))]
    slots.extend(new_slot(i) for i in range(1, args.envs))
    returns_window: list[float] = list(saved.get("returns_window", [])) if saved else []
    wins_window: list[float] = list(saved.get("wins_window", [])) if saved else []
    lengths_window: list[int] = list(saved.get("lengths_window", [])) if saved else []
    next_snapshot = (total // args.snapshot_every + 1) * args.snapshot_every
    if total >= args.self_play_after and not snapshots:
        snapshots.append(copy.deepcopy({k: v.detach().cpu() for k, v in policy.state_dict().items()}))
    started = time.time()
    started_at_total = total
    pool = concurrent.futures.ThreadPoolExecutor(args.envs)
    # Starting a fight process takes about a second (it loads both characters), and closing one waits for it to
    # exit. Both happen off the main loop: each slot's next matchup is already loading while its current one plays.
    loader = concurrent.futures.ThreadPoolExecutor(args.envs)

    def prepare():
        a, b, arena = matchup()
        return loader.submit(lambda: ((a, b, arena), MugenEnv(args.exe, args.game, a, b, arena, args.frame_skip)))

    prepared = [None if fixed_matchup else prepare() for _ in slots]
    character_wins = {name: deque(maxlen=50) for name in pool_characters}
    monitor = None
    if args.monitor:
        opponent_state = random.choice(snapshots) if snapshots else policy.state_dict()
        monitor = TrainingMonitor(args, roster, ids, first_env.observation_size, policy.hidden_size, policy.size,
                                  policy.state_dict(), opponent_state,
                                  saved.get("matchup_counters") if saved else None)
    # Histories survive PPO rollout boundaries but are cleared between matches. This counts real contiguous inputs
    # without joining the end of one matchup to the start of the next.
    sequence_histories = [deque(maxlen=4) for _ in slots]

    try:
        while total < args.steps:
            rollouts = [[] for _ in slots]
            sequence_counts = {length: Counter() for length in (2, 3, 4)}
            horizon = min(args.rollout, math.ceil((args.steps - total) / len(slots)))
            # Wall time per part of one rollout and its update, to show where training throughput goes.
            timings = Counter()
            mark = time.perf_counter()

            def lap(name):
                nonlocal mark
                now = time.perf_counter()
                timings[name] += now - mark
                mark = now

            for _ in range(horizon):
                observations = torch.as_tensor(np.stack([s.state.observation for s in slots]), device=device)
                self_ids = torch.tensor([ids[s.p1] for s in slots], device=device)
                other_ids = torch.tensor([ids[s.p2] for s in slots], device=device)
                hidden_before = torch.cat([s.hidden for s in slots])
                with torch.no_grad():
                    ld, lb, values, hidden_after = policy(observations, self_ids, other_ids, hidden_before)
                    directions, buttons, logps = choose(ld, lb)
                    # One device-to-host copy per step: reading GPU tensors element by element waits on the GPU
                    # each time, which used to cost more than the fights themselves.
                    host = torch.stack((directions.float(), buttons.float(), logps, values)).cpu().numpy()
                    directions = host[0].astype(np.int64).tolist()
                    buttons = host[1].astype(np.int64).tolist()
                    logps, values = host[2].tolist(), host[3].tolist()
                    hidden_host = hidden_before.cpu().numpy()
                opponent_actions = []
                for i, slot in enumerate(slots):
                    action = (0, 0)
                    if slot.opponent:
                        oobs = torch.as_tensor(slot.state.opponent_observation, device=device).unsqueeze(0)
                        with torch.no_grad():
                            old, olb, _, slot.opponent_hidden = slot.opponent(
                                oobs, other_ids[i:i + 1], self_ids[i:i + 1], slot.opponent_hidden)
                            od, ob, _ = choose(old, olb)
                        action = (int(od), int(ob))
                    opponent_actions.append(action)
                lap("inference")
                futures = [pool.submit(slot.env.step, (directions[i], buttons[i]), opponent_actions[i])
                           for i, slot in enumerate(slots)]
                for i, slot in enumerate(slots):
                    slot.actions.append((directions[i], buttons[i], float(slot.state.observation[6]),
                                         opponent_actions[i][0], opponent_actions[i][1],
                                         float(slot.state.opponent_observation[6])))
                    sequence_histories[i].append(directions[i] * len(BUTTON_NAMES) + buttons[i])
                    for length, counts in sequence_counts.items():
                        if len(sequence_histories[i]) >= length:
                            counts[tuple(sequence_histories[i])[-length:]] += 1
                lap("bookkeeping")
                results = [future.result() for future in futures]
                lap("env_wait")
                for i, (slot, result) in enumerate(zip(slots, results)):
                    rollouts[i].append((slot.state.observation.copy(), ids[slot.p1], ids[slot.p2],
                                        hidden_host[i], directions[i], buttons[i],
                                        logps[i], values[i], result.reward, result.done))
                    slot.state = result
                    slot.hidden = hidden_after[i:i + 1].detach()
                    slot.episode_return += result.reward
                    slot.episode_length += 1
                    if result.done:
                        episode += 1
                        returns_window.append(slot.episode_return)
                        returns_window = returns_window[-100:]
                        wins_window.append(1.0 if result.winner == 0 else 0.0)
                        # Per character the policy played, or per opponent when it always plays one character.
                        tracked = slot.p2 if fixed_p1 else slot.p1
                        if tracked in character_wins:
                            character_wins[tracked].append(wins_window[-1])
                        wins_window = wins_window[-100:]
                        lengths_window.append(slot.episode_length)
                        lengths_window = lengths_window[-100:]
                        writer.add_scalar("episode/return", slot.episode_return, total)
                        writer.add_scalar("episode/win", wins_window[-1], total)
                        writer.add_scalar("episode/length_decisions", slot.episode_length, total)
                        if result.winner == 0:
                            replay = save_win(slot, Path(args.replay_dir), args.game, args.frame_skip, total, episode)
                            writer.add_text("wins/latest_replay", str(replay), total)
                        # A fixed matchup keeps its process and only resets the match; loading a new pair of
                        # characters takes about a second and stalls every other fight.
                        if fixed_matchup:
                            slots[i] = new_slot(i, slot.env)
                        else:
                            loader.submit(slot.env.close)
                            next_matchup, next_env = prepared[i].result()
                            prepared[i] = prepare()
                            slots[i] = new_slot(i, next_env, next_matchup)
                        sequence_histories[i].clear()
                total += len(slots)
                lap("results")

            flat = []
            for i, trajectory in enumerate(rollouts):
                slot = slots[i]
                with torch.no_grad():
                    obs = torch.as_tensor(slot.state.observation, device=device).unsqueeze(0)
                    _, _, bootstrap, _ = policy(obs, torch.tensor([ids[slot.p1]], device=device),
                                                 torch.tensor([ids[slot.p2]], device=device), slot.hidden)
                advantages = np.zeros(len(trajectory), np.float32)
                gae, next_value = 0.0, float(bootstrap)
                for j in range(len(trajectory) - 1, -1, -1):
                    done, reward, value = trajectory[j][9], trajectory[j][8], trajectory[j][7]
                    delta = reward + args.gamma * next_value * (not done) - value
                    gae = delta + args.gamma * .95 * (not done) * gae
                    advantages[j] = gae
                    next_value = value
                values = np.asarray([row[7] for row in trajectory], np.float32)
                flat.extend((row, advantages[j], advantages[j] + values[j]) for j, row in enumerate(trajectory))
            advantages = np.asarray([item[1] for item in flat], np.float32)
            advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)
            # Each environment's steps in order, as arrays. flat holds them environment after environment.
            columns = []
            offset = 0
            for trajectory in rollouts:
                n = len(trajectory)
                columns.append({
                    "obs": np.stack([r[0] for r in trajectory]).astype(np.float32),
                    "si": np.asarray([r[1] for r in trajectory], np.int64),
                    "oi": np.asarray([r[2] for r in trajectory], np.int64),
                    "hidden": np.stack([r[3] for r in trajectory]).astype(np.float32),
                    "ad": np.asarray([r[4] for r in trajectory], np.int64),
                    "ab": np.asarray([r[5] for r in trajectory], np.int64),
                    "logp": np.asarray([r[6] for r in trajectory], np.float32),
                    "done": np.asarray([r[9] for r in trajectory], np.float32),
                    "adv": advantages[offset:offset + n],
                    "ret": np.asarray([flat[offset + j][2] for j in range(n)], np.float32)})
                offset += n
            # Sequence training for the GRU: each environment's steps cut into chunks of args.bptt decisions. A chunk
            # starts from the memory stored at its first step and runs the GRU through the rest inside the update,
            # so the gradient reaches back through the whole chunk (backpropagation through time) instead of
            # stopping at a stored memory. After a match ends the memory starts again from zero, as it did in play.
            length = max(1, args.bptt)
            chunks = [(i, start, min(length, len(c["ad"]) - start))
                      for i, c in enumerate(columns) for start in range(0, len(c["ad"]), length)]
            chunks_per_batch = max(1, args.minibatch // length)
            policy_losses, value_losses, entropies, kls, clip_fractions, gradient_norms = [], [], [], [], [], []
            for _ in range(args.epochs):
                random.shuffle(chunks)
                for first in range(0, len(chunks), chunks_per_batch):
                    batch = chunks[first:first + chunks_per_batch]
                    steps = max(n for _, _, n in batch)

                    def gather(key, dtype):
                        shape = (steps, len(batch)) + columns[0][key].shape[1:]
                        out = np.zeros(shape, dtype)
                        for b, (i, start, n) in enumerate(batch):
                            out[:n, b] = columns[i][key][start:start + n]
                        return torch.as_tensor(out, device=device)

                    obs, si, oi = gather("obs", np.float32), gather("si", np.int64), gather("oi", np.int64)
                    ad, ab = gather("ad", np.int64), gather("ab", np.int64)
                    old_logp, adv, returns = gather("logp", np.float32), gather("adv", np.float32), gather("ret", np.float32)
                    done = gather("done", np.float32)
                    mask = np.zeros((steps, len(batch)), np.float32)
                    for b, (_, _, n) in enumerate(batch):
                        mask[:n, b] = 1
                    mask = torch.as_tensor(mask, device=device)
                    hidden = torch.as_tensor(np.stack([columns[i]["hidden"][start] for i, start, _ in batch]),
                                             device=device)
                    ld, lb, value = policy.sequence(obs, si, oi, hidden, done)
                    dd, db = Categorical(logits=ld, validate_args=False), Categorical(logits=lb, validate_args=False)
                    logp = dd.log_prob(ad) + db.log_prob(ab)
                    ratio = (logp - old_logp).exp()
                    count = mask.sum()
                    policy_loss = -(torch.minimum(ratio * adv, ratio.clamp(.8, 1.2) * adv) * mask).sum() / count
                    value_loss = .5 * ((value - returns).square() * mask).sum() / count
                    entropy = ((dd.entropy() + db.entropy()) * mask).sum() / count
                    loss = policy_loss + .5 * value_loss - args.entropy_coef * entropy
                    optimizer.zero_grad()
                    loss.backward()
                    gradient_norm = torch.nn.utils.clip_grad_norm_(policy.parameters(), .5)
                    optimizer.step()
                    with torch.no_grad():
                        policy_losses.append(float(policy_loss)); value_losses.append(float(value_loss))
                        entropies.append(float(entropy)); kls.append(float(((old_logp - logp) * mask).sum() / count))
                        clip_fractions.append(float((((ratio - 1).abs() > .2).float() * mask).sum() / count))
                        gradient_norms.append(float(gradient_norm))
            lap("ppo_update")
            # As aggressive as stays stable: halve the rate after an update that moved the policy more than twice the
            # target, raise it after one that moved it less than half. Large KL jumps are what made earlier runs forget.
            if args.target_kl > 0:
                update_kl = float(np.mean(kls))
                for group in optimizer.param_groups:
                    if update_kl > 2 * args.target_kl:
                        group["lr"] = max(group["lr"] / 2, 1e-5)
                    elif update_kl < args.target_kl / 2:
                        group["lr"] = min(group["lr"] * 1.5, 3e-3)
            writer.add_scalar("loss/learning_rate", optimizer.param_groups[0]["lr"], total)
            if total >= next_snapshot:
                snapshots.append(copy.deepcopy({k: v.detach().cpu() for k, v in policy.state_dict().items()}))
                snapshots = snapshots[-8:]
                next_snapshot += args.snapshot_every
            if monitor:
                monitor.update_policy(policy.state_dict(), random.choice(snapshots) if snapshots else policy.state_dict())
            matchup_counters = monitor.checkpoint() if monitor else {}
            torch.save({"model": policy.state_dict(), "optimizer": optimizer.state_dict(), "steps": total,
                        "episodes": episode, "returns_window": returns_window, "wins_window": wins_window,
                        "lengths_window": lengths_window, "snapshots": snapshots,
                        "matchup_counters": matchup_counters,
                        "observation_size": first_env.observation_size, "characters": list(roster.characters),
                        "stages": list(roster.stages), "hidden_size": policy.hidden_size,
                        "size": policy.size}, checkpoint)
            lap("checkpoint")
            mean_return = np.mean(returns_window) if returns_window else 0.0
            # A resumed checkpoint already has many decisions; throughput covers only work done by this process.
            speed = (total - started_at_total) / max(time.time() - started, 1e-6)
            external_count = sum(s.opponent is not None for s in slots)
            old_values = np.asarray([item[0][7] for item in flat], np.float32)
            target_returns = np.asarray([item[2] for item in flat], np.float32)
            return_variance = np.var(target_returns)
            explained_variance = 1.0 - np.var(target_returns - old_values) / return_variance if return_variance > 0 else 0.0
            writer.add_scalar("rollout/mean_return_100", mean_return, total)
            writer.add_scalar("rollout/win_rate_100", np.mean(wins_window) if wins_window else 0.0, total)
            writer.add_scalar("rollout/mean_episode_length_100", np.mean(lengths_window) if lengths_window else 0.0, total)
            writer.add_scalar("performance/decisions_per_second", speed, total)
            writer.add_scalar("performance/self_play_environments", external_count, total)
            writer.add_scalar("loss/policy", np.mean(policy_losses), total)
            writer.add_scalar("loss/value", np.mean(value_losses), total)
            writer.add_scalar("loss/entropy", np.mean(entropies), total)
            writer.add_scalar("loss/approx_kl", np.mean(kls), total)
            writer.add_scalar("loss/clip_fraction", np.mean(clip_fractions), total)
            writer.add_scalar("loss/gradient_norm", np.mean(gradient_norms), total)
            writer.add_scalar("loss/explained_variance", explained_variance, total)
            writer.add_histogram("actions/direction", np.asarray([item[0][4] for item in flat]), total)
            writer.add_histogram("actions/button", np.asarray([item[0][5] for item in flat]), total)
            for length, counts in sequence_counts.items():
                sequence_total = sum(counts.values())
                top_count = counts.most_common(1)[0][1] if counts else 0
                writer.add_scalar(f"actions/sequence_{length}_top_share",
                                  top_count / max(1, sequence_total), total)
                writer.add_scalar(f"actions/sequence_{length}_unique", len(counts), total)
                writer.add_text(f"actions/top_sequences_{length}", sequence_table(counts), total)
            if device.type == "cuda":
                writer.add_scalar("hardware/gpu_memory_allocated_mb", torch.cuda.memory_allocated() / 2**20, total)
                writer.add_scalar("hardware/gpu_memory_reserved_mb", torch.cuda.memory_reserved() / 2**20, total)
            if args.characters:
                for name, results in character_wins.items():
                    if results:
                        writer.add_scalar(f"characters/{Path(name).stem}_win_rate_50", np.mean(results), total)
            monitor_values = monitor.snapshot() if monitor else {}
            for name, values in monitor_values.items():
                writer.add_scalar(f"matchups/{name}_win_rate_100", values["win_rate"], total)
                writer.add_scalar(f"matchups/{name}_draw_rate_100", values["draw_rate"], total)
                writer.add_scalar(f"matchups/{name}_matches", values["total"], total)
            lap("logging")
            for name, seconds in timings.items():
                writer.add_scalar(f"performance/seconds_{name}", seconds, total)
            writer.flush()
            spent = sum(timings.values())
            print("  time: " + " ".join(f"{name}={seconds / spent:.0%}" for name, seconds in timings.most_common()))
            counters = " ".join(f"{name.replace('_', '/')}={values['win_rate']:.0%}(n={values['window']})"
                                for name, values in monitor_values.items())
            print(f"steps={total} envs={len(slots)} episodes={episode} return100={mean_return:.3f} "
                  f"win100={(np.mean(wins_window) if wins_window else 0):.1%} {speed:.0f} decisions/s "
                  f"selfplay={external_count} {counters} tensorboard={run_dir}")
    finally:
        if monitor:
            monitor.close()
        pool.shutdown(wait=False, cancel_futures=True)
        for slot in slots:
            slot.env.close()
        for future in prepared:
            if future and future.done() and not future.exception():
                future.result()[1].close()
        loader.shutdown(wait=False, cancel_futures=True)
        writer.close()


if __name__ == "__main__":
    main()
