"""Background fixed-policy matchup counters for the PPO trainer."""

from __future__ import annotations

import concurrent.futures
import copy
import random
import threading
from collections import deque

import torch
from torch.distributions import Categorical

from model import Policy
from mugen_env import MugenEnv


MATCHUPS = ("policy_smart", "policy_policy", "policy_random", "smart_random")


def cpu_state(state):
    return {key: value.detach().cpu().clone() for key, value in state.items()}


def policy_action(policy, observation, self_id, opponent_id, hidden):
    # Sampled, as training plays: the probes measure the same player as win100 (argmax played a different one).
    with torch.no_grad():
        ld, lb, _, hidden = policy(torch.from_numpy(observation).unsqueeze(0),
                                   torch.tensor([self_id]), torch.tensor([opponent_id]), hidden)
    return (int(Categorical(logits=ld).sample()), int(Categorical(logits=lb).sample())), hidden


def monitor_match(kind, number, matchup, args, ids, policy, old_policy):
    p1, p2, stage = matchup
    external = kind in ("policy_policy", "policy_random")
    rng = random.Random(args.seed + 3_000_000 + number * len(MATCHUPS) + MATCHUPS.index(kind))
    with MugenEnv(args.exe, args.game, p1, p2, stage, args.frame_skip) as env:
        state = env.reset(args.seed + 2_000_000 + number, external)
        hidden1 = policy.initial_hidden(1, "cpu")
        hidden2 = old_policy.initial_hidden(1, "cpu")
        for _ in range(args.monitor_max_decisions):
            if state.done:
                break
            action1 = (0, 0)
            action2 = (0, 0)
            if kind != "smart_random":
                action1, hidden1 = policy_action(policy, state.observation, ids[p1], ids[p2], hidden1)
            else:
                action1 = (rng.randrange(env.direction_count), rng.randrange(env.button_count))
            if kind == "policy_policy":
                action2, hidden2 = policy_action(old_policy, state.opponent_observation, ids[p2], ids[p1], hidden2)
            elif kind == "policy_random":
                action2 = (rng.randrange(env.direction_count), rng.randrange(env.button_count))
            state = env.step(action1, action2)
    # SmartAI is player 2 in smart_random; the learned policy is player 1 in every other probe.
    focal_winner = 1 if kind == "smart_random" else 0
    if state.done:
        winner = state.winner
    else:
        # A compatibility-broken character can prevent the match terminal from arriving. Resolve the probe exactly
        # like a time-over comparison so one matchup cannot freeze all four rolling counters.
        opponent_life_index = (state.observation.size - 5) // 2
        p1_life, p2_life = float(state.observation[0]), float(state.observation[opponent_life_index])
        winner = -1 if abs(p1_life - p2_life) < 1e-6 else 0 if p1_life > p2_life else 1
    return 0 if winner < 0 else 1 if winner == focal_winner else -1


class TrainingMonitor:
    """Continuously runs one match of each kind using frozen CPU policy copies."""

    def __init__(self, args, roster, ids, observation_size, hidden_size, size, current_state, opponent_state,
                 saved=None):
        self.args = args
        self.roster = roster
        self.ids = ids
        self.observation_size = observation_size
        self.hidden_size = hidden_size
        self.size = size
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.current_state = cpu_state(current_state)
        self.opponent_state = cpu_state(opponent_state)
        saved = saved or {}
        self.windows = {name: deque(saved.get(name, {}).get("window", []), maxlen=100) for name in MATCHUPS}
        self.totals = {name: int(saved.get(name, {}).get("total", 0)) for name in MATCHUPS}
        self.number = int(saved.get("number", 0))
        self.thread = threading.Thread(target=self.run, name="rl-matchup-monitor", daemon=True)
        self.thread.start()

    def update_policy(self, current_state, opponent_state):
        with self.lock:
            self.current_state = cpu_state(current_state)
            self.opponent_state = cpu_state(opponent_state)

    def snapshot(self):
        with self.lock:
            result = {}
            for name in MATCHUPS:
                values = list(self.windows[name])
                result[name] = {"win_rate": sum(value == 1 for value in values) / max(1, len(values)),
                                "draw_rate": sum(value == 0 for value in values) / max(1, len(values)),
                                "window": len(values), "total": self.totals[name]}
            return result

    def checkpoint(self):
        with self.lock:
            result = {name: {"window": list(self.windows[name]), "total": self.totals[name]} for name in MATCHUPS}
            result["number"] = self.number
            return result

    def close(self):
        self.stop_event.set()
        self.thread.join(timeout=30)

    def run(self):
        while not self.stop_event.is_set():
            with self.lock:
                current_state = copy.deepcopy(self.current_state)
                opponent_state = copy.deepcopy(self.opponent_state)
                number = self.number
                self.number += 1
            policy = Policy(self.observation_size, len(self.ids), self.hidden_size, self.size)
            old_policy = Policy(self.observation_size, len(self.ids), self.hidden_size, self.size)
            policy.load_state_dict(current_state); policy.eval()
            old_policy.load_state_dict(opponent_state); old_policy.eval()
            rng = random.Random(self.args.seed + 4_000_000 + number)
            characters = list(self.ids)
            specs = {}
            for kind in MATCHUPS:
                p1, p2 = rng.choices(characters, k=2)
                specs[kind] = (p1, p2, rng.choice(self.roster.stages))
            with concurrent.futures.ThreadPoolExecutor(max_workers=len(MATCHUPS)) as pool:
                futures = {pool.submit(monitor_match, kind, number, specs[kind], self.args, self.ids,
                                       policy, old_policy): kind for kind in MATCHUPS}
                for future in concurrent.futures.as_completed(futures):
                    kind = futures[future]
                    try:
                        result = future.result()
                    except Exception:
                        continue
                    with self.lock:
                        self.windows[kind].append(result)
                        self.totals[kind] += 1

