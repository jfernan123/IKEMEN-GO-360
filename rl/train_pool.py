"""Recurrent PPO on hundreds of fights at once, through mugen360's RL pool (pool_env.MugenPool).

Player 1 learns against SmartAI playing one or more opponents. With one opponent the fights split over --pools
processes; with several, each opponent gets its own pool and a share of the fights. The pools take turns: while one
simulates, the network picks the next pool's actions in a single GPU batch.

With several opponents, --reweight-every moves fights toward the opponents the policy wins least against (as in
Prioritized Level Replay, which samples levels by how much is left to learn on them). --anchor keeps a frozen policy
(e.g. a specialist) and adds a KL term pulling the learner toward its moves in fights against --anchor-opponents, so
training on new opponents doesn't wash out what it already knew (as CLEAR's behaviour cloning toward a past policy).

--teachers FILE is policy distillation with reward (as multi-teacher on-policy distillation, MOPD): the fresh student
plays every match, and a frozen teacher for each opponent gives its move probabilities at every decision. The student
minimises KL(teacher || student) on top of PPO, so reward counts from the first step and it can pass its teachers. Each
teacher's pull is judged by that teacher's own win rate: once the student is within --teacher-margin of it the pull
fades, and it comes back if the student slips. Each line of FILE is `opponent | checkpoint | stage` (stage optional);
opponent * is the teacher for every opponent without one. An older network works as a teacher: its missing observation
values are widened in and its missing button choices count as never pressed.

--redo-every recycles dormant units (ReDo, Sokar et al. 2023), keeping the network able to learn new opponents.
--skills gives the policy a skill code per match and pays a small bonus when a discriminator can tell the skills apart
from how Cap plays (DIAYN-style), but only against opponents it already beats (--diversity-min-win, as in SMERL), so
every style still has to win.

--league N plays N of the fights as a mirror against copies of the policy itself, driving player 2 through the pool's
external mode (AlphaStar's league, scaled down). A frozen snapshot joins the league every --snapshot-every updates;
each league match faces the current policy (--league-self of the time) or a snapshot picked by prioritized fictitious
self-play: the snapshots it loses to most come up most, weighted (1 - win rate)^2. Only player 1 learns. The SmartAI
opponents keep their share of the fights, so self-play can't drift into habits that only work against Cap. With
--league equal to --fights every fight is the mirror (split over --pools pools), and the --p2 opponents are only
checked: every --check-every updates, --check-matches against each of them (style 0, as the game plays it).

--rival CHAR --rival-fights N makes this run one of AlphaStar's main agents, training at the same time as another
character's: N fights are against that character, played by the other trainer's current network (re-read from
--rival-checkpoint every --rival-refresh updates; --rival-start until it exists) or by one of its snapshots in
<rival-checkpoint>_league/, picked by PFSP like the mirror's. Both runs read each other's folders, so the league is
shared without one process holding both learners.

Each pool gets simulation threads in proportion to its fights.

Same network, sequence (BPTT) update, adaptive learning rate, TensorBoard tags and checkpoint format as train_ppo.py.
"""

from __future__ import annotations

import argparse
import math
import os
import time
from collections import deque
from pathlib import Path

import numpy as np
import torch
from torch.distributions import Categorical
from torch.utils.tensorboard import SummaryWriter

from eval_pool import play
from model import Policy, add_buttons, add_skills, layer_activations, policy_from_checkpoint, redo
from pool_env import MugenPool
from roster import discover
from train_ppo import widen_observation


def teacher_kl(teacher_d, teacher_b, student_d, student_b):
    """KL(teacher || student) on both heads, per decision: keep the teacher's moves likely."""
    return (teacher_d.softmax(-1) * (teacher_d.log_softmax(-1) - student_d.log_softmax(-1))).sum(-1) + \
           (teacher_b.softmax(-1) * (teacher_b.log_softmax(-1) - student_b.log_softmax(-1))).sum(-1)


def split(total: int, weights: list[float], minimum: int) -> list[int]:
    """Whole fight counts in proportion to weights, each at least minimum, adding up to total."""
    weights = np.asarray(weights, np.float64)
    weights = weights / weights.sum()
    counts = np.maximum(minimum, np.floor(weights * total)).astype(int)
    while counts.sum() > total:
        counts[np.argmax(counts)] -= 1
    while counts.sum() < total:
        counts[np.argmax(weights * total - counts)] += 1
    return counts.tolist()


def save_whole(data: dict, path: Path):
    """torch.save through a temporary file, so another run reading the path (a rival) never sees half a file.
    Windows refuses the swap while the other run has the file open for a moment; then it tries again."""
    temporary = path.with_name(path.name + ".tmp")
    torch.save(data, temporary)
    for attempt in range(50):
        try:
            os.replace(temporary, path)
            return
        except PermissionError:
            time.sleep(0.1)
    os.replace(temporary, path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", required=True)
    parser.add_argument("--exe", default="build/rl/mugen360.exe")
    parser.add_argument("--p1", required=True, help="the policy's character")
    parser.add_argument("--p2", required=True, help="SmartAI's character, or several, comma-separated")
    parser.add_argument("--weights", help="starting share of fights per opponent, comma-separated (default even)")
    parser.add_argument("--stage", default="stages/gotham.def")
    parser.add_argument("--fights", type=int, default=512, help="fights in all")
    parser.add_argument("--pools", type=int, default=2, help="processes for a single opponent (several: one each)")
    parser.add_argument("--threads", type=int, default=0, help="simulation threads per pool (0: --cores, shared by the pools' fights)")
    parser.add_argument("--cores", type=int, default=0, help="cores this run may use (0: all of them)")
    parser.add_argument("--rollout", type=int, default=64, help="decisions per fight before each update")
    parser.add_argument("--steps", type=int, default=50_000_000, help="total decisions")
    parser.add_argument("--frame-skip", type=int, default=3)
    parser.add_argument("--size", choices=("small", "large"), default="large")
    parser.add_argument("--gamma", type=float, default=.997)
    parser.add_argument("--lam", type=float, default=.95)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--target-kl", type=float, default=0.02, help="adaptive learning rate target (0: fixed)")
    parser.add_argument("--epochs", type=int, default=4)
    parser.add_argument("--minibatch", type=int, default=4096, help="decisions per gradient step")
    parser.add_argument("--bptt", type=int, default=32)
    parser.add_argument("--entropy-coef", type=float, default=.01)
    parser.add_argument("--random-sides", action="store_true",
                        help="in mirror fights the learner plays player 1 or player 2 at random each match")
    parser.add_argument("--damage-dealt-weight", type=float, default=1.0,
                        help="reward per opponent life bar dealt (the engine's reward counts 1)")
    parser.add_argument("--damage-taken-weight", type=float, default=1.0,
                        help="penalty per own life bar lost (the engine's reward counts 1)")
    parser.add_argument("--reweight-every", type=int, default=0,
                        help="updates between moving fights toward the opponents won least against (0: never)")
    parser.add_argument("--min-share", type=float, default=0.08, help="least share of fights any opponent keeps")
    parser.add_argument("--anchor", help="a frozen checkpoint whose moves the learner is kept near")
    parser.add_argument("--anchor-opponents", default="", help="opponents whose fights get the anchor term")
    parser.add_argument("--anchor-coef", type=float, default=0.5)
    parser.add_argument("--teachers", help="a file of teachers, one per opponent: opponent | checkpoint | stage")
    parser.add_argument("--teacher-coef", type=float, default=1.0, help="weight of the KL to a teacher at full pull")
    parser.add_argument("--teacher-margin", type=float, default=0.05,
                        help="a teacher's pull fades once the student wins within this of the teacher's rate")
    parser.add_argument("--teacher-fade", type=float, default=0.995, help="pull kept per update once within the margin")
    parser.add_argument("--teacher-floor", type=float, default=0.02, help="the least pull a teacher keeps")
    parser.add_argument("--teacher-matches", type=int, default=256, help="matches measuring each teacher at the start")
    parser.add_argument("--step-penalty", type=float, default=0.0,
                        help="taken off every decision's reward, so a quicker win is worth more")
    parser.add_argument("--win-time-cost", type=float, default=0.0,
                        help="taken off a win per decision it lasted, so a quicker win is worth more (losses untouched)")
    parser.add_argument("--time-over",choices=("game", "loss", "draw"), default="game",
                        help="the outcome when the clock runs out: the game's (more life wins), a loss for both "
                             "(only a KO earns the win), or a draw")
    parser.add_argument("--lr-max", type=float, default=3e-3, help="the adaptive learning rate's ceiling")
    parser.add_argument("--redo-every", type=int, default=0, help="updates between dormant-unit recycling (0: never)")
    parser.add_argument("--redo-tau", type=float, default=0.025, help="dormant below this share of the layer average")
    parser.add_argument("--skills", type=int, default=0, help="skill codes for varied play (0: none)")
    parser.add_argument("--diversity-coef", type=float, default=0.002, help="bonus per decision for a telling style")
    parser.add_argument("--diversity-min-win", type=float, default=0.6,
                        help="the style bonus only counts against opponents beaten at least this often")
    parser.add_argument("--style-window", type=int, default=32,
                        help="decisions the style discriminator averages Cap's behaviour over (1: single frames)")
    parser.add_argument("--stop-win", type=float, default=0.0,
                        help="end the run once every periodic check wins at least this share (0: never)")
    parser.add_argument("--stop-checks", type=int, default=2, help="checks in a row --stop-win has to hold for")
    parser.add_argument("--curriculum", default="",
                        help="the opponents' life bar in steps, e.g. 0.25,0.5,0.75,1: the next step starts once matches "
                             "at this one are won --curriculum-win of the time (the style bonus waits for the last step)")
    parser.add_argument("--curriculum-win", type=float, default=0.6)
    parser.add_argument("--keep-every", type=int, default=0,
                        help="updates between copies of the network in <checkpoint>_history (0: none)")
    parser.add_argument("--curriculum-back", type=float, default=0.0,
                        help="go back a step when matches at this one are won less than this share (0: never)")
    parser.add_argument("--skill-scale", type=float, default=1.0,
                        help="new runs only: the style codes start this size (small: every style starts as one "
                             "policy and learns as fast as a run without styles, then the style bonus splits them)")
    parser.add_argument("--skill-noise", type=float, default=0.0,
                        help="once at start: random weights of this size on the skill inputs, so skills that "
                             "began as copies of one policy start a little different")
    parser.add_argument("--league", type=int, default=0, help="fights against copies of the policy (0: no self-play)")
    parser.add_argument("--league-self", type=float, default=0.35,
                        help="share of league matches against the current policy rather than a snapshot")
    parser.add_argument("--snapshot-every", type=int, default=300, help="updates between league snapshots")
    parser.add_argument("--league-size", type=int, default=16,
                        help="snapshots kept (the first, the starting policy, always stays)")
    parser.add_argument("--rival", help="another character trained at the same time by its own run (a main agent)")
    parser.add_argument("--rival-fights", type=int, default=0, help="fights against the rival")
    parser.add_argument("--rival-checkpoint", help="the rival run's --checkpoint (its snapshots: <stem>_league/)")
    parser.add_argument("--rival-start", help="the rival's network until its run has saved a checkpoint")
    parser.add_argument("--rival-refresh", type=int, default=10,
                        help="updates between re-reading the rival's checkpoint and snapshots")
    parser.add_argument("--check-every", type=int, default=0,
                        help="updates between win-rate checks against the --p2 opponents (0: never)")
    parser.add_argument("--check-matches", type=int, default=64, help="matches against each opponent per check")
    parser.add_argument("--seed", type=int, default=360)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--init-from", help="start from this checkpoint's weights (same network size)")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--logdir", default="rl/runs")
    args = parser.parse_args()
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device("cuda" if args.device == "cuda" or (args.device == "auto" and torch.cuda.is_available())
                          else "cpu")
    roster = discover(args.game)
    ids = {path: i for i, path in enumerate(roster.characters)}
    opponents = [o.strip() for o in args.p2.split(",") if o.strip()]
    for name in [args.p1] + opponents + ([args.rival] if args.rival else []):
        if name not in ids:
            raise ValueError(f"{name} is not in this game's roster")
    rival_fights = max(0, args.rival_fights) if args.rival else 0
    if rival_fights and not args.rival_checkpoint:
        raise ValueError("--rival needs --rival-checkpoint")
    anchored = {o.strip() for o in args.anchor_opponents.split(",") if o.strip()}
    # Each opponent's teacher checkpoint and stage, from --teachers.
    teacher_paths, arenas = {}, {}
    if args.teachers:
        lines = [[part.strip() for part in line.split("#")[0].split("|")]
                 for line in Path(args.teachers).read_text(encoding="utf-8").splitlines()]
        entries = {parts[0]: parts for parts in lines if len(parts) >= 2}
        for opponent in opponents:
            parts = entries.get(opponent) or entries.get("*")
            if parts is None:
                raise ValueError(f"no teacher for {opponent}")
            teacher_paths[opponent] = parts[1]
            if len(parts) > 2 and parts[2]:
                arenas[opponent] = parts[2]
    # A single opponent still runs in several pools so play and the network overlap.
    pool_opponents = opponents if len(opponents) > 1 else opponents * max(1, args.pools)
    weights = [float(w) for w in args.weights.split(",")] if args.weights else [1.0] * len(opponents)
    if len(weights) != len(opponents):
        raise ValueError("--weights needs one value per opponent")
    pool_weights = weights if len(opponents) > 1 else [1.0] * len(pool_opponents)
    league_fights = max(0, min(args.league, args.fights - rival_fights))
    smart_fights = args.fights - league_fights - rival_fights
    if smart_fights == 0:
        # All mirror: the SmartAI opponents are only checked now and then.
        pool_opponents, pool_weights = [], []
    elif smart_fights < len(pool_opponents):
        raise ValueError("--league leaves too few fights for the SmartAI opponents")
    # Without SmartAI pools the mirror splits over --pools, so one simulates while the network picks the other's moves.
    # (With a rival the mirror and the rival are a pool each, which already take turns.)
    league_pools = max(1, args.pools) if smart_fights == 0 and not rival_fights \
        and league_fights >= 2 * max(1, args.pools) else 1

    def threads_for(count: int) -> int:
        """A pool's simulation threads, in proportion to its share of the fights."""
        return args.threads or max(1, round((args.cores or os.cpu_count() or 8) * count / args.fights))

    minimum = max(8, int(args.min_share * smart_fights))

    curriculum = [float(x) for x in args.curriculum.split(",") if x.strip()]
    stage = 0
    # Results of matches that started at the current curriculum step (a match running when the step changes doesn't
    # count), and the step each fight's current match started at.
    stage_wins: deque = deque(maxlen=200)
    fight_stage = np.zeros(args.fights, np.int64)

    groups: list[dict] = []
    other_ids = torch.zeros(args.fights, dtype=torch.long, device=device)
    is_anchor = torch.zeros(args.fights, dtype=torch.bool, device=device)

    def build(counts):
        for group in groups:
            group["pool"].close()
        groups.clear()
        start = 0
        for k, (opponent, count) in enumerate(zip(pool_opponents, counts)):
            pool = MugenPool(args.exe, args.game, args.p1, opponent, arenas.get(opponent, args.stage), count, threads_for(count),
                             args.frame_skip)
            if curriculum:
                pool.set_opponent_life(curriculum[stage])
            sl = slice(start, start + count)
            fight_stage[sl] = stage
            other_ids[sl] = ids[opponent]
            is_anchor[sl] = opponent in anchored
            groups.append({"opponent": opponent, "pool": pool, "slice": sl,
                           "obs": pool.reset(args.seed + 100_003 * k + int(rng.integers(1 << 20)))[:, 0]})
            start += count
        # The mirror: player 2 is the same character, and the trainer picks his moves (external) instead of SmartAI.
        # league_offset is where a pool's fights start in the league's own arrays (league_pick, league_hidden...).
        offset = 0
        for k, count in enumerate(split(league_fights, [1.0] * league_pools, 1) if league_fights else []):
            pool = MugenPool(args.exe, args.game, args.p1, args.p1, args.stage, count, threads_for(count),
                             args.frame_skip)
            sl = slice(start, start + count)
            fight_stage[sl] = stage
            other_ids[sl] = ids[args.p1]
            is_anchor[sl] = "league" in anchored
            both = pool.reset(args.seed + 100_003 * (len(counts) + k) + int(rng.integers(1 << 20)), external=True)
            groups.append({"opponent": "league", "pool": pool, "slice": sl, "obs": both[:, 0], "obs2": both[:, 1],
                           "league": "league", "league_offset": offset})
            start += count
            offset += count
        # The rival: player 2 is the other main agent's character, its moves picked here too.
        if rival_fights:
            pool = MugenPool(args.exe, args.game, args.p1, args.rival, args.stage, rival_fights,
                             threads_for(rival_fights), args.frame_skip)
            sl = slice(start, start + rival_fights)
            fight_stage[sl] = stage
            other_ids[sl] = ids[args.rival]
            is_anchor[sl] = "rival" in anchored
            both = pool.reset(args.seed + 100_003 * (len(counts) + league_pools) + int(rng.integers(1 << 20)),
                              external=True)
            groups.append({"opponent": "rival", "pool": pool, "slice": sl, "obs": both[:, 0], "obs2": both[:, 1],
                           "league": "rival", "league_offset": 0})
        return groups[0]["pool"].observation_size

    counts = split(smart_fights, pool_weights, minimum if len(opponents) > 1 else 1) if smart_fights else []
    size = build(counts)

    checkpoint = Path(args.checkpoint)
    checkpoint.parent.mkdir(parents=True, exist_ok=True)
    resume = torch.load(checkpoint, map_location=device, weights_only=False) \
        if args.resume and checkpoint.is_file() else None
    if resume and curriculum and resume.get("curriculum_stage", 0) > 0:
        # The pools started at the first step; their next matches use the saved one (the running ones don't count).
        stage = min(int(resume["curriculum_stage"]), len(curriculum) - 1)
        stage_wins.extend(resume.get("stage_wins", []))
        for group in groups:
            group["pool"].set_opponent_life(curriculum[stage])
    net_size = resume.get("size", "small") if resume else args.size
    skills = resume.get("skills", 0) if resume else args.skills
    # The engine's button choices: 8 in older builds, 65 with the presses of several buttons at once.
    buttons = groups[0]["pool"].button_count
    policy = Policy(size, len(roster.characters), size=net_size, skills=skills, buttons=buttons).to(device)
    # The style discriminator guesses the skill from Cap's own part of the frame and the move he picked.
    discriminator = torch.nn.Sequential(torch.nn.Linear((size - 5) // 2 + 17, 256), torch.nn.ReLU(),
                                        torch.nn.Linear(256, 256), torch.nn.ReLU(),
                                        torch.nn.Linear(256, max(1, skills))).to(device)
    disc_optimizer = torch.optim.Adam(discriminator.parameters(), lr=3e-4)
    optimizer = torch.optim.Adam(policy.parameters(), lr=args.lr)
    total = episodes = 0
    returns_window, wins_window, lengths_window = deque(maxlen=100), deque(maxlen=100), deque(maxlen=100)
    opponent_wins = {o: deque(maxlen=200) for o in (opponents if smart_fights else []) + (["league"] if league_fights else [])
                     + (["rival"] if rival_fights else [])}
    last_check: dict[str, float] = {}
    checks_passed = 0
    if resume:
        if resume["characters"] != list(roster.characters) or resume["observation_size"] != size:
            raise ValueError("checkpoint roster or observation format does not match this game")
        if policy_from_checkpoint(resume).buttons != buttons:
            raise ValueError(f"checkpoint has fewer button choices than this build's {buttons}: start a new run "
                             "with --init-from it instead (the new choices are added then)")
        policy.load_state_dict(resume["model"])
        optimizer.load_state_dict(resume["optimizer"])
        if skills and "discriminator" in resume and resume.get("style_window", 1) == args.style_window:
            discriminator.load_state_dict(resume["discriminator"])
            disc_optimizer.load_state_dict(resume["disc_optimizer"])
        elif skills:
            print(f"style discriminator starts fresh (window {args.style_window})")
        total, episodes = int(resume["steps"]), int(resume.get("episodes", 0))
        returns_window.extend(resume.get("returns_window", []))
        wins_window.extend(resume.get("wins_window", []))
        lengths_window.extend(resume.get("lengths_window", []))
        for o, results in resume.get("opponent_wins", {}).items():
            if o in opponent_wins:
                opponent_wins[o].extend(results)
        print(f"resumed {checkpoint} at {total:,} decisions")
    elif args.init_from:
        start = torch.load(args.init_from, map_location=device, weights_only=False)
        if start.get("size", "small") != net_size:
            raise ValueError(f"--init-from is a {start.get('size', 'small')} network; this run is {net_size}")
        model = start["model"]
        if start["observation_size"] != size:
            model = widen_observation(model, start["observation_size"], size)
        if skills and not start.get("skills"):
            model = add_skills(model, skills)
        if policy.buttons > policy_from_checkpoint(start).buttons:
            model = add_buttons(model, net_size, policy.buttons)
            print(f"button choices widened to {policy.buttons} (presses of several buttons start rare)")
        policy.load_state_dict(model)
        print(f"initialized from {args.init_from}")
    elif skills and args.skill_scale != 1.0:
        with torch.no_grad():
            policy.skill.weight.mul_(args.skill_scale)
        print(f"style codes start at {args.skill_scale} of their usual size")
    if skills and args.skill_noise > 0:
        with torch.no_grad():
            first = policy.encoder[0].weight
            first[:, -policy.skill.embedding_dim:] += args.skill_noise * torch.randn_like(
                first[:, -policy.skill.embedding_dim:])
        print(f"skill inputs nudged by {args.skill_noise}")
    anchor = None
    if args.anchor:
        anchor = policy_from_checkpoint(torch.load(args.anchor, map_location=device, weights_only=False)).to(device)
        for parameter in anchor.parameters():
            parameter.requires_grad_(False)
        print(f"anchor {args.anchor} on {', '.join(Path(o).stem for o in anchored) or 'no opponents'}")

    # The teachers (one network per checkpoint, however many opponents share it), each opponent's pull, and the win
    # rate the student has to come close to, measured here so every teacher is judged the same way.
    teacher_nets, teacher_of = {}, {}
    teacher_weight = dict(resume.get("teacher_weight", {})) if resume else {}
    teacher_score = dict(resume.get("teacher_score", {})) if resume else {}
    for opponent, path in teacher_paths.items():
        if path not in teacher_nets:
            saved = torch.load(path, map_location=device, weights_only=False)
            if saved["characters"] != list(roster.characters):
                raise ValueError(f"teacher {path} has another roster")
            if saved["observation_size"] != size:
                saved = dict(saved, model=widen_observation(saved["model"], saved["observation_size"], size),
                             observation_size=size)
            teacher_nets[path] = policy_from_checkpoint(saved).to(device)
            for parameter in teacher_nets[path].parameters():
                parameter.requires_grad_(False)
        teacher_of[opponent] = teacher_nets[path]
        teacher_weight.setdefault(opponent, 1.0)
        if opponent not in teacher_score:
            with MugenPool(args.exe, args.game, args.p1, opponent, arenas.get(opponent, args.stage),
                           args.teacher_matches, os.cpu_count() or 8, args.frame_skip) as pool:
                result = play(pool, teacher_of[opponent], device, ids[args.p1], ids[opponent], args.seed, 2500, rng)
            teacher_score[opponent] = float(np.mean(result == 0))
        print(f"teacher for {Path(opponent).stem}: {path}, wins {teacher_score[opponent]:.0%}, "
              f"pull {teacher_weight[opponent]:.2f}", flush=True)
    fight_pull = torch.zeros(args.fights, device=device)

    # The league: frozen snapshots of the policy in <checkpoint>_league/, each with the learner's recent results
    # against it. Snapshot 0 is the policy as this run started, so there is always one opponent to face.
    league_dir = checkpoint.with_name(checkpoint.stem + "_league")
    snapshots: list[dict] = []

    def snapshot_record():
        return {"model": {k: v.detach().clone() for k, v in policy.state_dict().items()}, "observation_size": size,
                "characters": list(roster.characters), "hidden_size": policy.hidden_size, "size": policy.size,
                "skills": skills, "buttons": buttons, "steps": total}

    def add_snapshot(path: Path, saved: dict, results=()):
        net = policy_from_checkpoint(saved).to(device)
        for parameter in net.parameters():
            parameter.requires_grad_(False)
        snapshots.append({"path": path, "net": net, "skills": saved.get("skills", 0),
                          "wins": deque(results, maxlen=200)})

    if league_fights:
        league_dir.mkdir(parents=True, exist_ok=True)
        saved_results = resume.get("league_wins", {}) if resume else {}
        found = sorted(league_dir.glob("snap_*.pt")) if resume else []
        if not resume:
            for old in league_dir.glob("snap_*.pt"):
                old.unlink()
        for path in found:
            add_snapshot(path, torch.load(path, map_location=device, weights_only=False), saved_results.get(path.name, ()))
        if not snapshots:
            path = league_dir / f"snap_{total:012d}.pt"
            torch.save(snapshot_record(), path)
            add_snapshot(path, torch.load(path, map_location=device, weights_only=False))
        print(f"league: {league_fights} mirror fights, {len(snapshots)} snapshots in {league_dir}, "
              f"{args.league_self:.0%} against the current policy")

    # The rival: the other main agent's current network and its snapshots, as its own run last saved them.
    rival_snapshots: list[dict] = []
    rival_current: dict = {}
    rival_dir = Path(args.rival_checkpoint).with_name(Path(args.rival_checkpoint).stem + "_league") \
        if rival_fights else None
    rival_saved_results = resume.get("rival_wins", {}) if resume else {}

    def load_frozen(path: Path):
        """A checkpoint's network, frozen, or None while its run is still writing it (or has just deleted it)."""
        try:
            saved = torch.load(path, map_location=device, weights_only=False)
        except Exception:
            return None
        net = policy_from_checkpoint(saved).to(device)
        for parameter in net.parameters():
            parameter.requires_grad_(False)
        return {"net": net, "skills": saved.get("skills", 0), "steps": int(saved.get("steps", 0))}

    def refresh_rival() -> bool:
        """Re-reads the rival's checkpoint when it changed, and its snapshot folder (new ones join, deleted ones go).
        Fights facing a snapshot that went keep playing on with a new pick. True if the snapshots changed."""
        path = Path(args.rival_checkpoint)
        if not path.is_file() and args.rival_start:
            path = Path(args.rival_start)
        if path.is_file() and (rival_current.get("path") != path or rival_current.get("mtime") != path.stat().st_mtime):
            mtime = path.stat().st_mtime
            loaded = load_frozen(path)
            if loaded:
                rival_current.update(loaded, path=path, mtime=mtime)
        on_disk = sorted(rival_dir.glob("snap_*.pt")) if rival_dir.is_dir() else []
        keep = [s for s in rival_snapshots if s["path"] in on_disk]
        known = {s["path"] for s in keep}
        added = []
        for found in on_disk:
            if found not in known:
                loaded = load_frozen(found)
                if loaded:
                    added.append({"path": found, "net": loaded["net"], "skills": loaded["skills"],
                                  "wins": deque(rival_saved_results.get(found.name, ()), maxlen=200)})
        if len(keep) == len(rival_snapshots) and not added:
            return False
        old = list(rival_snapshots)
        rival_snapshots[:] = keep + added
        if "pick" in league_state.get("rival", {}):
            state = league_state["rival"]
            for k, who in enumerate(state["pick"]):
                if who >= 0:
                    still = old[who]
                    state["pick"][k] = rival_snapshots.index(still) if still in keep else pick_opponent("rival")
        return True

    def pick_opponent(league: str = "league") -> int:
        """-1 for the current network, else a snapshot, the ones the learner beats least coming up most (PFSP)."""
        pool_of = snapshots if league == "league" else rival_snapshots
        if rng.random() < args.league_self or not pool_of:
            return -1
        rates = np.array([float(np.mean(s["wins"])) if len(s["wins"]) >= 20 else 0.5 for s in pool_of])
        weights = np.square(1.0 - rates) + 0.02
        return int(rng.choice(len(pool_of), p=weights / weights.sum()))
    print(f"network: {net_size}, {sum(p.numel() for p in policy.parameters()):,} parameters; {args.fights} fights: "
          + ", ".join(f"{Path(g['opponent']).stem} {g['slice'].stop - g['slice'].start}" for g in groups)
          + "; threads " + ", ".join(str(threads_for(g["slice"].stop - g["slice"].start)) for g in groups)
          + f"; {args.rollout * args.fights:,} decisions per update; device {device}"
          + (f"; checking {', '.join(Path(o).stem for o in opponents)} every {args.check_every} updates"
             if args.check_every else ""))

    run_dir = Path(args.logdir) / time.strftime("%Y%m%d-%H%M%S")
    writer = SummaryWriter(run_dir)
    writer.add_text("run/config", "\n".join(f"- **{k}**: {v}" for k, v in vars(args).items()), 0)

    fights = args.fights
    self_ids = torch.full((fights,), ids[args.p1], device=device)
    # Each match plays one skill, picked at random when it starts.
    skill_ids = torch.as_tensor(rng.integers(0, max(1, skills), fights), device=device) if skills else None
    fighter_size = (size - 5) // 2
    # Each fight's running average of Cap's own frame and move choices (the style the discriminator judges), over
    # about --style-window decisions, starting again with each match.
    style_rate = 1.0 / max(1, args.style_window)
    style_average = torch.zeros((fights, fighter_size + 17), device=device)
    style_seen = torch.zeros(fights, device=device)
    hidden = policy.initial_hidden(fights, device)
    anchor_hidden = anchor.initial_hidden(fights, device) if anchor else None
    teacher_hidden = torch.zeros((fights, policy.hidden_size), device=device)
    # Each league fight's player 2: who plays him this match, his memory and his skill code, per kind of league
    # fight (the mirror, the rival). His network sees himself first and the learner second.
    league_state: dict[str, dict] = {}
    if rival_fights:
        refresh_rival()
        if not rival_current:
            raise ValueError(f"no rival network yet: {args.rival_checkpoint} (or --rival-start) must exist")
        print(f"rival: {Path(args.rival).stem}, {rival_fights} fights, current network {rival_current['path']} "
              f"({rival_current['steps']:,} decisions), {len(rival_snapshots)} snapshots in {rival_dir}")

    def start_league(league: str, count: int, character: str, hidden_size: int):
        league_state[league] = {
            "pick": np.array([pick_opponent(league) for _ in range(count)], np.int64),
            "hidden": torch.zeros((count, hidden_size), device=device),
            "skill": torch.as_tensor(rng.integers(0, max(1, skills), count), device=device),
            "self_ids": torch.full((count,), ids[character], device=device),
            "other_ids": torch.full((count,), ids[args.p1], device=device)}

    if league_fights:
        start_league("league", league_fights, args.p1, policy.hidden_size)
    if rival_fights:
        start_league("rival", rival_fights, args.rival, rival_current["net"].hidden_size)

    def league_player(league: str, who: int):
        """The network and skill count playing player 2: the current one (-1) or snapshot `who`."""
        if league == "league":
            return (policy, skills) if who < 0 else (snapshots[who]["net"], snapshots[who]["skills"])
        if who < 0:
            return rival_current["net"], rival_current["skills"]
        return rival_snapshots[who]["net"], rival_snapshots[who]["skills"]
    episode_return = np.zeros(fights, np.float32)
    episode_length = np.zeros(fights, np.int64)
    # Life bars dealt and taken per match (1 = a whole bar), whatever the reward weights: how much he fights.
    episode_dealt = np.zeros(fights, np.float32)
    episode_taken = np.zeros(fights, np.float32)
    dealt_window, taken_window = deque(maxlen=100), deque(maxlen=100)
    # Matches that ended by the clock rather than a KO.
    time_over_window = deque(maxlen=100)
    # Which player the learner is in each fight's match (0: player 1, on the left; 1: player 2). Only mirror fights
    # change sides (--random-sides), from the first match on.
    learner_side = np.zeros(fights, np.int64)
    if args.random_sides:
        for group in groups:
            if group.get("league") == "league":
                sl = group["slice"]
                learner_side[sl] = rng.integers(0, 2, sl.stop - sl.start)
                right = learner_side[sl] == 1
                group["obs"], group["obs2"] = np.where(right[:, None], group["obs2"], group["obs"]), \
                    np.where(right[:, None], group["obs"], group["obs2"])
    side_wins = {0: deque(maxlen=200), 1: deque(maxlen=200)}
    T = args.rollout
    updates = 0

    started, started_total = time.time(), total
    try:
        while total < args.steps:
            buf_obs = torch.zeros((T, fights, size), device=device)
            buf_hidden = torch.zeros((T, fights, policy.hidden_size), device=device)
            buf_anchor_hidden = torch.zeros((T, fights, anchor.hidden_size), device=device) if anchor else None
            # What each fight's teacher would do at each decision, worked out as the fight played.
            buf_teacher_d = torch.zeros((T, fights, 9), device=device) if teacher_of else None
            buf_teacher_b = torch.zeros((T, fights, buttons), device=device) if teacher_of else None
            teacher_gap = {opponent: [] for opponent in teacher_of}
            buf_d = torch.zeros((T, fights), dtype=torch.long, device=device)
            buf_b = torch.zeros((T, fights), dtype=torch.long, device=device)
            buf_logp = torch.zeros((T, fights), device=device)
            buf_value = torch.zeros((T, fights), device=device)
            buf_reward = np.zeros((T, fights), np.float32)
            buf_skill = torch.zeros((T, fights), dtype=torch.long, device=device)
            buf_style = torch.zeros((T, fights, fighter_size + 17), device=device) if skills else None
            buf_done = np.zeros((T, fights), np.float32)
            timings = {"inference": 0.0, "env_wait": 0.0}
            for t in range(T):
                # Pools take turns: choose and send pool k's actions while the pools before it play theirs.
                pending = []
                for group in groups:
                    mark = time.perf_counter()
                    sl = group["slice"]
                    obs = torch.as_tensor(group["obs"], device=device)
                    with torch.no_grad():
                        sk = skill_ids[sl] if skills else None
                        ld, lb, value, h_next = policy(obs, self_ids[sl], other_ids[sl], hidden[sl], sk)
                        dd, db = Categorical(logits=ld, validate_args=False), Categorical(logits=lb, validate_args=False)
                        d, b = dd.sample(), db.sample()
                        logp = dd.log_prob(d) + db.log_prob(b)
                        a_next = None
                        if anchor is not None and bool(is_anchor[sl.start]):
                            buf_anchor_hidden[t, sl] = anchor_hidden[sl]
                            a_next = anchor(obs, self_ids[sl], other_ids[sl], anchor_hidden[sl])[3]
                        t_next = None
                        teacher = teacher_of.get(group["opponent"])
                        if teacher is not None:
                            td, tb, _, t_next = teacher(obs, self_ids[sl], other_ids[sl], teacher_hidden[sl])
                            # An 8-button teacher never presses the choices it doesn't have.
                            tb = torch.nn.functional.pad(tb, (0, buttons - tb.shape[-1]), value=-30.0)
                            buf_teacher_d[t, sl], buf_teacher_b[t, sl] = td, tb
                            teacher_gap[group["opponent"]].append(float(teacher_kl(td, tb, ld, lb).mean()))
                    buf_obs[t, sl] = obs
                    buf_hidden[t, sl] = hidden[sl]
                    buf_d[t, sl], buf_b[t, sl], buf_logp[t, sl], buf_value[t, sl] = d, b, logp, value
                    if skills:
                        buf_skill[t, sl] = skill_ids[sl]
                        now = torch.cat((obs[:, :fighter_size], torch.nn.functional.one_hot(d, 9).float(),
                                         torch.nn.functional.one_hot(b.clamp(max=7), 8).float()), -1)
                        # The first decisions of a match average over what there is so far.
                        style_seen[sl] += 1
                        rate = torch.maximum(1.0 / style_seen[sl], torch.full_like(style_seen[sl], style_rate))
                        style_average[sl] += rate.unsqueeze(1) * (now - style_average[sl])
                        buf_style[t, sl] = style_average[sl]
                    actions = np.zeros((sl.stop - sl.start, 4), np.uint8)
                    actions[:, 0] = d.cpu().numpy()
                    actions[:, 1] = b.cpu().numpy()
                    league_next = None
                    if "league_offset" in group:
                        # Player 2 from his own view, one batch per opponent playing in this pool. The moves stay on
                        # the GPU until all are picked: one copy back instead of two per opponent.
                        off, n = group["league_offset"], sl.stop - sl.start
                        state = league_state[group["league"]]
                        picks = state["pick"][off:off + n]
                        obs2 = torch.as_tensor(group["obs2"], device=device)
                        mine = state["hidden"][off:off + n]
                        league_next = mine.clone()
                        d2 = torch.zeros(n, dtype=torch.long, device=device)
                        b2 = torch.zeros(n, dtype=torch.long, device=device)
                        with torch.no_grad():
                            for who in np.unique(picks):
                                rows = torch.as_tensor(np.flatnonzero(picks == who), device=device)
                                net, has_skills = league_player(group["league"], int(who))
                                ld2, lb2, _, h2 = net(obs2[rows], state["self_ids"][off + rows],
                                                      state["other_ids"][off + rows], mine[rows],
                                                      state["skill"][off + rows] % has_skills if has_skills else None)
                                league_next[rows] = h2
                                d2[rows] = Categorical(logits=ld2, validate_args=False).sample()
                                b2[rows] = Categorical(logits=lb2, validate_args=False).sample()
                        actions[:, 2] = d2.cpu().numpy()
                        actions[:, 3] = b2.cpu().numpy()
                        # Where the learner plays the right side this match, his moves are player 2's.
                        right = learner_side[sl] == 1
                        actions[right] = actions[right][:, [2, 3, 0, 1]]
                    timings["inference"] += time.perf_counter() - mark
                    group["pool"].send_step(actions)
                    pending.append((group, h_next, a_next, t_next, league_next))
                for group, h_next, a_next, t_next, league_next in pending:
                    mark = time.perf_counter()
                    reward, done, winner, obs = group["pool"].receive_step()
                    timings["env_wait"] += time.perf_counter() - mark
                    sl = group["slice"]
                    # Everything from here on is from the learner's side: where he played player 2 this match, the
                    # engine's reward (player 1's) flips sign, the winner index swaps, and the two views swap.
                    side = learner_side[sl].copy()
                    flip = side == 1
                    reward = np.where(flip, -reward, reward).astype(np.float32)
                    winner = np.where(flip & (winner >= 0) & (winner <= 1), 1 - winner, winner)
                    obs = np.where(flip[:, None, None], obs[:, ::-1], obs)
                    # The engine's reward is damage dealt - damage taken (life bars) + 1 win / -1 loss. Split it
                    # from the two life bars (observation 0 is his own life, fighter_size his opponent's, as
                    # fractions) to weigh the parts; at a match's end the next match has started, so the end's
                    # step splits the engine's number instead (one side is nearly always 0 within 3 ticks).
                    outcome = np.where(done, np.where(winner == 0, 1.0, np.where(winner == 1, -1.0, 0.0)), 0.0)
                    net = reward - outcome
                    dealt = np.where(done, np.maximum(net, 0.0),
                                     np.maximum(group["obs"][:, fighter_size] - obs[:, 0, fighter_size], 0.0))
                    taken = np.where(done, np.maximum(-net, 0.0), np.maximum(group["obs"][:, 0] - obs[:, 0, 0], 0.0))
                    # The clock ran out: the view before this step (observation -3 is the time left) shows it at 0,
                    # since the clock stops once the round is decided. The game gives that to whoever has more
                    # life; --time-over loss makes it a loss for both, so only a KO earns the win.
                    timed_out = done & (group["obs"][:, -3] <= 0.0)
                    if args.time_over != "game":
                        outcome = np.where(timed_out, -1.0 if args.time_over == "loss" else 0.0, outcome)
                        reward = (net + outcome).astype(np.float32)
                    if args.damage_dealt_weight != 1.0 or args.damage_taken_weight != 1.0:
                        reward = (args.damage_dealt_weight * dealt - args.damage_taken_weight * taken
                                  + outcome).astype(np.float32)
                    if args.step_penalty:
                        reward = (reward - args.step_penalty).astype(np.float32)
                    if args.win_time_cost:
                        # A win is worth less the longer it took; a loss stays -1 however quick, so giving up early
                        # never pays the way it does with --step-penalty.
                        won_now = done & (outcome > 0)
                        reward = (reward - np.where(won_now, args.win_time_cost * (episode_length[sl] + 1), 0.0)
                                  ).astype(np.float32)
                    episode_dealt[sl] += dealt
                    episode_taken[sl] += taken
                    if args.random_sides and "league_offset" in group and group["league"] == "league" and done.any():
                        # A finished match's next one (already started) gets a new side; its first views follow.
                        ended = np.flatnonzero(done)
                        new_side = rng.integers(0, 2, len(ended))
                        swap = new_side != side[ended]
                        obs[ended[swap]] = obs[ended[swap]][:, ::-1]
                        learner_side[sl.start + ended] = new_side
                    group["obs"] = obs[:, 0]
                    if league_next is not None:
                        group["obs2"] = obs[:, 1]
                        off = group["league_offset"]
                        state = league_state[group["league"]]
                        state["hidden"][off:off + len(done)] = league_next * torch.as_tensor(
                            1.0 - done.astype(np.float32), device=device).unsqueeze(1)
                        played = snapshots if group["league"] == "league" else rival_snapshots
                        for j in np.flatnonzero(done):
                            # The result counts for whoever played player 2; the next match gets a new pick.
                            k = off + j
                            if state["pick"][k] >= 0:
                                played[state["pick"][k]]["wins"].append(1.0 if winner[j] == 0 else 0.0)
                            state["pick"][k] = pick_opponent(group["league"])
                            state["skill"][k] = int(rng.integers(0, max(1, skills)))
                    buf_reward[t, sl] = reward
                    buf_done[t, sl] = done
                    keep = torch.as_tensor(1.0 - done.astype(np.float32), device=device).unsqueeze(1)
                    hidden[sl] = h_next * keep
                    if skills:
                        style_average[sl] *= keep
                        style_seen[sl] *= keep.squeeze(1)
                    if t_next is not None:
                        teacher_hidden[sl] = t_next * keep
                    if a_next is not None:
                        anchor_hidden[sl] = a_next * keep
                    episode_return[sl] += reward
                    episode_length[sl] += 1
                    for j in np.flatnonzero(done):
                        i = sl.start + j
                        episodes += 1
                        won = 1.0 if winner[j] == 0 else 0.0
                        returns_window.append(float(episode_return[i]))
                        wins_window.append(won)
                        lengths_window.append(int(episode_length[i]))
                        dealt_window.append(float(episode_dealt[i]))
                        taken_window.append(float(episode_taken[i]))
                        time_over_window.append(1.0 if timed_out[j] else 0.0)
                        opponent_wins[group["opponent"]].append(won)
                        if args.random_sides and group["opponent"] == "league":
                            side_wins[int(side[j])].append(won)
                        if fight_stage[i] == stage:
                            stage_wins.append(won)
                        fight_stage[i] = stage
                        if skills:
                            skill_ids[i] = int(rng.integers(0, skills))
                        episode_return[i] = 0
                        episode_length[i] = 0
                        episode_dealt[i] = 0
                        episode_taken[i] = 0
                total += fights

            # The style bonus (SMERL): how well the discriminator recognises each step's skill, only in fights
            # against opponents already beaten often enough. Then the discriminator learns from this rollout.
            mark = time.perf_counter()
            diversity = {"bonus": 0.0, "accuracy": 0.0}
            confusion = None
            if skills:
                features = buf_style.reshape(T * fights, -1)
                labels = buf_skill.reshape(-1)
                with torch.no_grad():
                    scores = discriminator(features)
                    logq = scores.log_softmax(-1).gather(1, labels.unsqueeze(1)).squeeze(1)
                    # Which style the judge guessed for each true style: one clear peak per row means the styles are
                    # all distinct; a row split between two columns means those two styles play alike.
                    confusion = torch.zeros((skills, skills), device=device)
                    confusion.index_put_((labels, scores.argmax(-1)), torch.ones_like(labels, dtype=torch.float32),
                                         accumulate=True)
                    confusion = (confusion / confusion.sum(1, keepdim=True).clamp_min(1)).cpu().numpy()
                    bonus = (logq + math.log(skills)).clamp(-1.0, math.log(skills)).reshape(T, fights)
                    if curriculum:
                        # Only at the full opponent: styles shouldn't form against a handicapped one.
                        final = stage == len(curriculum) - 1 and len(stage_wins) >= 100
                        rate = {g["opponent"]: float(np.mean(stage_wins)) if final else 0.0 for g in groups}
                    else:
                        rate = {g["opponent"]: float(np.mean(opponent_wins[g["opponent"]])) if opponent_wins[g["opponent"]]
                                else 0.0 for g in groups}
                    beaten = torch.as_tensor([rate[g["opponent"]] >= args.diversity_min_win
                                              for g in groups for _ in range(g["slice"].start, g["slice"].stop)],
                                             device=device, dtype=torch.float32)
                    bonus = bonus * beaten.unsqueeze(0)
                buf_reward += args.diversity_coef * bonus.cpu().numpy()
                diversity["bonus"] = float(bonus.mean())
                order = torch.randperm(T * fights, device=device)
                correct = 0
                for first in range(0, T * fights, 4096):
                    ix = order[first:first + 4096]
                    logits = discriminator(features[ix])
                    disc_loss = torch.nn.functional.cross_entropy(logits, labels[ix])
                    disc_optimizer.zero_grad()
                    disc_loss.backward()
                    disc_optimizer.step()
                    correct += int((logits.argmax(-1) == labels[ix]).sum())
                diversity["accuracy"] = correct / (T * fights)

            for group in groups:
                fight_pull[group["slice"]] = teacher_weight.get(group["opponent"], 0.0)
            # Advantages (GAE) for every fight at once, bootstrapped from the value after the last step.
            with torch.no_grad():
                last_obs = torch.as_tensor(np.concatenate([g["obs"] for g in groups]), device=device)
                _, _, bootstrap, _ = policy(last_obs, self_ids, other_ids, hidden, skill_ids if skills else None)
            values = buf_value.cpu().numpy()
            next_values = np.concatenate([values[1:], bootstrap.cpu().numpy()[None]])
            advantages = np.zeros((T, fights), np.float32)
            gae = np.zeros(fights, np.float32)
            for t in range(T - 1, -1, -1):
                live = 1.0 - buf_done[t]
                delta = buf_reward[t] + args.gamma * next_values[t] * live - values[t]
                gae = delta + args.gamma * args.lam * live * gae
                advantages[t] = gae
            returns = torch.as_tensor(advantages + values, device=device)
            advantages_t = torch.as_tensor((advantages - advantages.mean()) / (advantages.std() + 1e-8), device=device)
            done_t = torch.as_tensor(buf_done, device=device)

            # Sequence update: chunks of args.bptt decisions from each fight, the GRU run through each chunk.
            length = max(1, min(args.bptt, T))
            chunks = [(t0, f) for f in range(fights) for t0 in range(0, T, length)]
            per_batch = max(1, args.minibatch // length)
            stats = {k: [] for k in ("policy", "value", "entropy", "kl", "clip", "grad", "anchor", "teacher")}
            for _ in range(args.epochs):
                rng.shuffle(chunks)
                for first in range(0, len(chunks), per_batch):
                    batch = chunks[first:first + per_batch]
                    starts = torch.as_tensor([c[0] for c in batch], device=device)
                    cols = torch.as_tensor([c[1] for c in batch], device=device)
                    steps = torch.arange(length, device=device).unsqueeze(1) + starts.unsqueeze(0)  # [L, B]
                    valid = (steps < T).float()
                    steps = steps.clamp(max=T - 1)
                    pick = lambda x: x[steps, cols.unsqueeze(0).expand_as(steps)]
                    obs_seq, dones_seq = pick(buf_obs), pick(done_t)
                    sids, oids = self_ids[cols].expand(length, -1), other_ids[cols].expand(length, -1)
                    ld, lb, value = policy.sequence(obs_seq, sids, oids, buf_hidden[starts, cols], dones_seq,
                                                    pick(buf_skill) if skills else None)
                    dd, db = Categorical(logits=ld, validate_args=False), Categorical(logits=lb, validate_args=False)
                    logp = dd.log_prob(pick(buf_d)) + db.log_prob(pick(buf_b))
                    old_logp, adv, ret = pick(buf_logp), pick(advantages_t), pick(returns)
                    ratio = (logp - old_logp).exp()
                    count = valid.sum()
                    policy_loss = -(torch.minimum(ratio * adv, ratio.clamp(.8, 1.2) * adv) * valid).sum() / count
                    value_loss = .5 * ((value - ret).square() * valid).sum() / count
                    entropy = ((dd.entropy() + db.entropy()) * valid).sum() / count
                    loss = policy_loss + .5 * value_loss - args.entropy_coef * entropy
                    anchor_loss = torch.zeros((), device=device)
                    if anchor is not None:
                        anchor_mask = valid * is_anchor[cols].float().unsqueeze(0)
                        anchor_count = anchor_mask.sum()
                        if anchor_count > 0:
                            with torch.no_grad():
                                ald, alb, _ = anchor.sequence(obs_seq, sids, oids, buf_anchor_hidden[starts, cols],
                                                              dones_seq)
                            # KL(anchor || learner) for both heads: keep the anchor's moves likely.
                            kl = (ald.softmax(-1) * (ald.log_softmax(-1) - ld.log_softmax(-1))).sum(-1) + \
                                 (alb.softmax(-1) * (alb.log_softmax(-1) - lb.log_softmax(-1))).sum(-1)
                            anchor_loss = (kl * anchor_mask).sum() / anchor_count
                            loss = loss + args.anchor_coef * anchor_loss
                    teacher_loss = torch.zeros((), device=device)
                    if teacher_of:
                        kl = teacher_kl(pick(buf_teacher_d), pick(buf_teacher_b), ld, lb)
                        teacher_loss = (kl * valid * fight_pull[cols].unsqueeze(0)).sum() / count
                        loss = loss + args.teacher_coef * teacher_loss
                    optimizer.zero_grad()
                    loss.backward()
                    grad = torch.nn.utils.clip_grad_norm_(policy.parameters(), .5)
                    optimizer.step()
                    with torch.no_grad():
                        stats["policy"].append(float(policy_loss)); stats["value"].append(float(value_loss))
                        stats["entropy"].append(float(entropy)); stats["anchor"].append(float(anchor_loss))
                        stats["teacher"].append(float(teacher_loss))
                        stats["kl"].append(float(((old_logp - logp) * valid).sum() / count))
                        stats["clip"].append(float((((ratio - 1).abs() > .2).float() * valid).sum() / count))
                        stats["grad"].append(float(grad))
            update_kl = float(np.mean(stats["kl"]))
            if args.target_kl > 0:
                for group in optimizer.param_groups:
                    if update_kl > 2 * args.target_kl:
                        group["lr"] = max(group["lr"] / 2, 1e-5)
                    elif update_kl < args.target_kl / 2:
                        group["lr"] = min(group["lr"] * 1.5, args.lr_max)
            timings["ppo_update"] = time.perf_counter() - mark
            updates += 1
            recycled = {}
            if args.redo_every and updates % args.redo_every == 0 and policy.size == "large":
                sample = torch.randint(0, T * fights, (min(8192, T * fights),), device=device)
                flat_obs = buf_obs.reshape(T * fights, -1)[sample]
                flat_cols = sample % fights
                with torch.no_grad():
                    acts = layer_activations(policy, flat_obs, self_ids[flat_cols], other_ids[flat_cols],
                                             buf_hidden.reshape(T * fights, -1)[sample],
                                             buf_skill.reshape(-1)[sample] if skills else None)
                recycled = redo(policy, optimizer, acts, args.redo_tau)
                print("redo recycled: " + ", ".join(f"{k} {v}" for k, v in recycled.items()), flush=True)

            win_by_opponent = {o: float(np.mean(r)) if r else 0.0 for o, r in opponent_wins.items()}
            # A teacher's pull fades while the student is close to the teacher's win rate, and returns if it slips.
            for opponent in teacher_of:
                if len(opponent_wins[opponent]) >= 100:
                    close = win_by_opponent[opponent] >= teacher_score[opponent] - args.teacher_margin
                    change = args.teacher_fade if close else 1.0 / args.teacher_fade
                    teacher_weight[opponent] = float(np.clip(teacher_weight[opponent] * change, args.teacher_floor, 1.0))
            save_whole({"model": policy.state_dict(), "optimizer": optimizer.state_dict(), "steps": total,
                        "episodes": episodes, "returns_window": list(returns_window),
                        "wins_window": list(wins_window), "lengths_window": list(lengths_window),
                        "opponent_wins": {o: list(r) for o, r in opponent_wins.items()},
                        "observation_size": size, "characters": list(roster.characters),
                        "stages": list(roster.stages), "hidden_size": policy.hidden_size, "size": policy.size,
                        "skills": skills, "style_window": args.style_window,
                        "curriculum_stage": stage, "stage_wins": list(stage_wins),
                        "teacher_weight": teacher_weight, "teacher_score": teacher_score,
                        "league_wins": {s["path"].name: list(s["wins"]) for s in snapshots},
                        "rival_wins": {s["path"].name: list(s["wins"]) for s in rival_snapshots},
                        **({"discriminator": discriminator.state_dict(), "disc_optimizer": disc_optimizer.state_dict()}
                           if skills else {})},
                       checkpoint)
            # A copy every --keep-every updates, to go back to if a run falls apart later.
            if args.keep_every and updates % args.keep_every == 0:
                kept = checkpoint.with_name(checkpoint.stem + "_history")
                kept.mkdir(exist_ok=True)
                save_whole(snapshot_record(), kept / f"{checkpoint.stem}_{total:012d}.pt")
            # The periodic check: the policy as the game plays it (style 0, sampled moves) against each SmartAI
            # opponent, in a pool of its own with every core; the training pools wait meanwhile. Not counted in speed.
            if args.check_every and updates % args.check_every == 0:
                mark = time.time()
                for opponent in opponents:
                    with MugenPool(args.exe, args.game, args.p1, opponent, arenas.get(opponent, args.stage), args.check_matches,
                                   os.cpu_count() or 8, args.frame_skip) as pool:
                        result = play(pool, policy, device, ids[args.p1], ids[opponent], args.seed + updates, 2500, rng)
                    last_check[opponent] = float(np.mean(result == 0))
                    writer.add_scalar(f"check/{Path(opponent).stem}_win_rate", last_check[opponent], total)
                started += time.time() - mark
                print(f"check ({args.check_matches} matches each, {time.time() - mark:.0f} s): "
                      + " ".join(f"{Path(o).stem}={w:.0%}" for o, w in last_check.items()), flush=True)
                # Checks fight the full life bar whatever the curriculum's step. Two in a row, as one 64-match check is
                # noisy. The network was saved just above, so the run can simply end here.
                checks_passed = checks_passed + 1 if min(last_check.values()) >= args.stop_win else 0
                if args.stop_win and checks_passed >= args.stop_checks:
                    print(f"reached {args.stop_win:.0%} in {checks_passed} checks in a row, stopping at {total:,}", flush=True)
                    break
            speed = (total - started_total) / max(time.time() - started, 1e-6)
            explained = 1 - np.var(advantages) / max(np.var(advantages + values), 1e-8)
            win = float(np.mean(wins_window)) if wins_window else 0.0
            ret = float(np.mean(returns_window)) if returns_window else 0.0
            for tag, value in (("rollout/win_rate_100", win), ("rollout/mean_return_100", ret),
                               ("rollout/mean_episode_length_100", float(np.mean(lengths_window)) if lengths_window else 0),
                               ("rollout/damage_dealt_100", float(np.mean(dealt_window)) if dealt_window else 0),
                               ("rollout/damage_taken_100", float(np.mean(taken_window)) if taken_window else 0),
                               ("rollout/time_over_100", float(np.mean(time_over_window)) if time_over_window else 0),
                               ("performance/decisions_per_second", speed),
                               ("loss/policy", np.mean(stats["policy"])), ("loss/value", np.mean(stats["value"])),
                               ("loss/entropy", np.mean(stats["entropy"])), ("loss/approx_kl", update_kl),
                               ("loss/clip_fraction", np.mean(stats["clip"])), ("loss/gradient_norm", np.mean(stats["grad"])),
                               ("loss/explained_variance", explained), ("loss/anchor_kl", np.mean(stats["anchor"])),
                               ("loss/teacher_kl", np.mean(stats["teacher"])),
                               ("loss/learning_rate", optimizer.param_groups[0]["lr"])):
                writer.add_scalar(tag, value, total)
            for name, seconds in timings.items():
                writer.add_scalar(f"performance/seconds_{name}", seconds, total)
            for name, count in recycled.items():
                writer.add_scalar(f"plasticity/recycled_{name}", count, total)
            if skills:
                writer.add_scalar("diversity/discriminator_accuracy", diversity["accuracy"], total)
                writer.add_scalar("diversity/bonus", diversity["bonus"], total)
                if confusion is not None:
                    for k in range(skills):
                        writer.add_scalar(f"diversity/recognised_style_{k}", confusion[k, k], total)
                    # Effective number of styles: how many the judge keeps apart (8 when every row is one peak).
                    writer.add_scalar("diversity/distinct_styles", float(np.trace(confusion)), total)
                    image = np.kron(confusion, np.ones((24, 24)))[None]
                    writer.add_image("diversity/confusion_true_rows_guessed_columns", image, total)
            if curriculum:
                writer.add_scalar("curriculum/opponent_life", curriculum[stage], total)
                writer.add_scalar("curriculum/win_rate_at_this_step", float(np.mean(stage_wins)) if stage_wins else 0.0,
                                  total)
            for opponent in teacher_of:
                writer.add_scalar(f"teachers/{Path(opponent).stem}_pull", teacher_weight[opponent], total)
                writer.add_scalar(f"teachers/{Path(opponent).stem}_kl", np.mean(teacher_gap[opponent]), total)
            for k, results in side_wins.items():
                if results:
                    writer.add_scalar(f"league/win_rate_as_p{k + 1}", float(np.mean(results)), total)
            if snapshots:
                writer.add_scalar("league/snapshots", len(snapshots), total)
                for s in snapshots:
                    if s["wins"]:
                        writer.add_scalar(f"league/win_rate_vs_{s['path'].stem}", float(np.mean(s["wins"])), total)
            if rival_snapshots:
                writer.add_scalar("rival/snapshots", len(rival_snapshots), total)
                writer.add_scalar("rival/current_network_decisions", rival_current["steps"], total)
                for s in rival_snapshots:
                    if s["wins"]:
                        writer.add_scalar(f"rival/win_rate_vs_{s['path'].stem}", float(np.mean(s["wins"])), total)
            if len(groups) > 1:
                for group in groups:
                    stem = Path(group["opponent"]).stem
                    writer.add_scalar(f"characters/{stem}_win_rate_200", win_by_opponent[group["opponent"]], total)
                    writer.add_scalar(f"characters/{stem}_fights", group["slice"].stop - group["slice"].start, total)
            writer.flush()
            print(f"steps={total:,} episodes={episodes:,} return100={ret:.3f} win100={win:.1%} "
                  f"dealt/taken={float(np.mean(dealt_window)) if dealt_window else 0:.2f}/"
                  f"{float(np.mean(taken_window)) if taken_window else 0:.2f} "
                  f"timeover={float(np.mean(time_over_window)) if time_over_window else 0:.0%} {speed:,.0f} decisions/s "
                  f"kl={update_kl:.4f} lr={optimizer.param_groups[0]['lr']:.2e} | "
                  + " ".join(f"{Path(o).stem}={w:.0%}" for o, w in win_by_opponent.items())
                  + (f" | league {len(snapshots)} snaps, vs oldest "
                     f"{float(np.mean(snapshots[0]['wins'])) if snapshots[0]['wins'] else 0:.0%}" if snapshots else "")
                  + (f" | as P1/P2 {float(np.mean(side_wins[0])) if side_wins[0] else 0:.0%}/"
                     f"{float(np.mean(side_wins[1])) if side_wins[1] else 0:.0%}" if args.random_sides else "")
                  + (f" | rival {len(rival_snapshots)} snaps, current at {rival_current['steps'] / 1e6:.0f}M"
                     if rival_fights else "")
                  + (" | last check " + " ".join(f"{Path(o).stem}={w:.0%}" for o, w in last_check.items())
                     if last_check and not smart_fights else "")
                  + (" | pull/kl " + " ".join(f"{Path(o).stem}={teacher_weight[o]:.2f}/{np.mean(teacher_gap[o]):.2f}" for o in teacher_of) if teacher_of else "")
                  + (f" | style acc={diversity['accuracy']:.0%}" if skills else "")
                  + (f" | opponent life {curriculum[stage]:.0%}: won {float(np.mean(stage_wins)) if stage_wins else 0:.0%}"
                     f" of {len(stage_wins)}" if curriculum else "")
                  + f" | tensorboard={run_dir}", flush=True)

            # The curriculum's next step: every pool's next matches start with the bigger life bar.
            if curriculum and stage < len(curriculum) - 1 and len(stage_wins) >= stage_wins.maxlen                     and np.mean(stage_wins) >= args.curriculum_win:
                stage += 1
                stage_wins.clear()
                for group in groups:
                    group["pool"].set_opponent_life(curriculum[stage])
                print(f"curriculum: opponent life now {curriculum[stage]:.0%}", flush=True)
            # And a step back when he's stopped winning at this one, so he isn't left losing every match (all -1, nothing
            # to learn from) at a step he can't win.
            elif curriculum and args.curriculum_back > 0 and stage > 0 and len(stage_wins) >= stage_wins.maxlen \
                    and np.mean(stage_wins) < args.curriculum_back:
                stage -= 1
                stage_wins.clear()
                for group in groups:
                    group["pool"].set_opponent_life(curriculum[stage])
                print(f"curriculum: back to opponent life {curriculum[stage]:.0%}", flush=True)

            # A new league snapshot: the policy as it is now, frozen. Past --league-size the oldest after the first go.
            if league_fights and updates % args.snapshot_every == 0:
                path = league_dir / f"snap_{total:012d}.pt"
                save_whole(snapshot_record(), path)
                add_snapshot(path, torch.load(path, map_location=device, weights_only=False))
                league_pick = league_state["league"]["pick"]
                while len(snapshots) > max(2, args.league_size):
                    gone = snapshots.pop(1)
                    try:
                        gone["path"].unlink(missing_ok=True)
                    except PermissionError:
                        pass  # a rival run is reading it; it stays on disk until next time
                    # Fights that faced it (or a later one, now one place earlier) keep playing whoever is there.
                    league_pick[league_pick == 1] = 0 if len(snapshots) == 1 else 1
                    league_pick[league_pick > 1] -= 1
                print(f"league: snapshot {path.name}, {len(snapshots)} in the league", flush=True)

            # The rival's newest network and snapshots, as its own run has saved them.
            if rival_fights and updates % args.rival_refresh == 0:
                if refresh_rival():
                    print(f"rival: {len(rival_snapshots)} snapshots", flush=True)

            # Move fights toward the opponents won least against; a pool restarts its fights when its size changes.
            if smart_fights and len(opponents) > 1 and args.reweight_every and updates % args.reweight_every == 0 \
                    and all(len(r) >= 50 for r in opponent_wins.values()):
                need = [1.0 - win_by_opponent[o] + 0.05 for o in opponents]
                new_counts = split(smart_fights, need, minimum)
                if max(abs(a - b) for a, b in zip(new_counts, counts)) >= 8:
                    counts = new_counts
                    build(counts)
                    hidden = policy.initial_hidden(fights, device)
                    anchor_hidden = anchor.initial_hidden(fights, device) if anchor else None
                    teacher_hidden.zero_()
                    for state in league_state.values():
                        state["hidden"].zero_()
                    if skills:
                        skill_ids = torch.as_tensor(rng.integers(0, skills, fights), device=device)
                        style_average.zero_()
                        style_seen.zero_()
                    episode_return[:] = 0
                    episode_length[:] = 0
                    print("reweighted: " + ", ".join(f"{Path(o).stem} {c}" for o, c in zip(opponents, counts)),
                          flush=True)
    finally:
        for group in groups:
            group["pool"].close()
        writer.close()


if __name__ == "__main__":
    main()
