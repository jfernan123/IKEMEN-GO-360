from __future__ import annotations

import math

import torch
from torch import nn

# Network sizes. small is the original one-layer encoder (its layer names are kept, so older checkpoints load). large
# reads each frame through three layers with LayerNorm and skip connections, has a wider GRU and two-layer heads: depth
# to work out relationships (whose attack box reaches whom, who is cornered) from the raw positions and boxes.
SIZES = {"small": 192, "large": 256}
# A skill code (SMERL-style diverse play) enters the encoder as a learned vector of this size.
SKILL_SIZE = 16


class Policy(nn.Module):
    """One roster policy: character embeddings condition a recurrent controller. With skills > 0 it also takes a skill
    code, so one network can play several styles (picked per match)."""

    def __init__(self, observation_size: int, character_count: int, hidden_size: int | None = None,
                 size: str = "small", skills: int = 0, buttons: int = 8):
        super().__init__()
        if size not in SIZES:
            raise ValueError(f"unknown network size {size!r}")
        hidden_size = hidden_size or SIZES[size]
        self.observation_size = observation_size
        self.character_count = character_count
        self.hidden_size = hidden_size
        self.size = size
        self.skills = skills
        self.buttons = buttons
        self.character = nn.Embedding(character_count, 24)
        inputs = observation_size + 48 + (SKILL_SIZE if skills else 0)
        if skills:
            self.skill = nn.Embedding(skills, SKILL_SIZE)
        if size == "small":
            self.encoder = nn.Sequential(nn.Linear(inputs, hidden_size), nn.Tanh())
            self.blocks = nn.ModuleList()
            self.memory = nn.GRUCell(hidden_size, hidden_size)
            self.direction = nn.Linear(hidden_size, 9)
            self.button = nn.Linear(hidden_size, buttons)
            self.value = nn.Linear(hidden_size, 1)
        else:
            self.encoder = nn.Sequential(nn.Linear(inputs, hidden_size), nn.LayerNorm(hidden_size), nn.ReLU())
            # Each block adds to what came before (a skip connection), which keeps a deeper network easy to train.
            self.blocks = nn.ModuleList(
                nn.Sequential(nn.Linear(hidden_size, hidden_size), nn.LayerNorm(hidden_size), nn.ReLU())
                for _ in range(2))
            self.memory = nn.GRUCell(hidden_size, hidden_size)
            head = hidden_size // 2
            self.direction = nn.Sequential(nn.Linear(hidden_size, head), nn.ReLU(), nn.Linear(head, 9))
            self.button = nn.Sequential(nn.Linear(hidden_size, head), nn.ReLU(), nn.Linear(head, buttons))
            self.value = nn.Sequential(nn.Linear(hidden_size, head), nn.ReLU(), nn.Linear(head, 1))
            # Start the action heads near uniform, as PPO setups usually do, so early play explores evenly.
            for last in (self.direction[-1], self.button[-1]):
                nn.init.orthogonal_(last.weight, 0.01)
                nn.init.zeros_(last.bias)

    def encode(self, observation, self_id, opponent_id, skill=None):
        parts = [observation, self.character(self_id), self.character(opponent_id)]
        if self.skills:
            if skill is None:
                skill = torch.zeros_like(self_id)
            parts.append(self.skill(skill))
        encoded = self.encoder(torch.cat(parts, dim=-1))
        for block in self.blocks:
            encoded = encoded + block(encoded)
        return encoded

    def forward(self, observation, self_id, opponent_id, hidden, skill=None):
        hidden = self.memory(self.encode(observation, self_id, opponent_id, skill), hidden)
        return self.direction(hidden), self.button(hidden), self.value(hidden).squeeze(-1), hidden

    def sequence(self, observation, self_id, opponent_id, hidden, done, skill=None):
        """The same network over a [T, B] block of steps: the encoder and heads run once over all of it and only the
        GRU steps one at a time. After a step whose match ended (done), the memory starts again from zero."""
        encoded = self.encode(observation, self_id, opponent_id, skill)
        memories = []
        for t in range(encoded.shape[0]):
            hidden = self.memory(encoded[t], hidden)
            memories.append(hidden)
            hidden = hidden * (1 - done[t]).unsqueeze(1)
        memory = torch.stack(memories)
        return self.direction(memory), self.button(memory), self.value(memory).squeeze(-1)

    def initial_hidden(self, batch: int, device):
        return torch.zeros(batch, self.hidden_size, device=device)


def add_skills(model: dict, skills: int) -> dict:
    """A checkpoint without skills, given skill inputs: the encoder's first layer gets zero weights for the skill
    vector, so every skill plays exactly like the original until training tells them apart."""
    weight = model["encoder.0.weight"]
    model = dict(model)
    model["encoder.0.weight"] = torch.cat((weight, weight.new_zeros((weight.shape[0], SKILL_SIZE))), dim=1)
    model["skill.weight"] = torch.randn(skills, SKILL_SIZE, device=weight.device)
    return model


def add_buttons(model: dict, size: str, buttons: int, old_scale: float = 0.3) -> dict:
    """A checkpoint with the original eight button choices (none, x, y, z, a, b, c, start), given the presses of two
    or more attack buttons as choices 8 on (src/core/rl_observation: rlButtonMask). Each new choice's output starts
    as the average of its buttons' outputs, lowered so that all of them together get about old_scale of the
    probability: the policy plays as before, and a combination its single buttons favour is tried most."""
    key = "button.weight" if size == "small" else "button.2.weight"
    weight, bias = model[key], model[key.replace("weight", "bias")]
    old = weight.shape[0]
    if old >= buttons:
        return model
    masks = [mask for mask in range(3, 64) if mask & (mask - 1)][:buttons - old]
    rows, biases = [], []
    for mask in masks:
        parts = [k + 1 for k in range(6) if mask & (1 << k)]
        rows.append(weight[parts].mean(0))
        biases.append(bias[parts].mean())
    offset = math.log(old_scale / len(masks))
    model = dict(model)
    model[key] = torch.cat((weight, torch.stack(rows)))
    model[key.replace("weight", "bias")] = torch.cat((bias, torch.stack(biases) + offset))
    return model


def policy_from_checkpoint(saved: dict, observation_size: int | None = None) -> Policy:
    """A checkpoint's network with its weights, in the shape it was saved with (older ones are all small)."""
    policy = Policy(observation_size or saved["observation_size"], len(saved["characters"]),
                    saved.get("hidden_size"), saved.get("size", "small"), saved.get("skills", 0),
                    saved["model"]["button.weight" if saved.get("size", "small") == "small" else "button.2.weight"].shape[0])
    policy.load_state_dict(saved["model"])
    policy.eval()
    return policy


def _layer_units(policy: Policy):
    """The large network's ReLU layers ReDo looks after: (name, the Linear in, its LayerNorm or None, what reads the
    units out: a Linear whose input columns they are, or None when they feed the skip-connection stream)."""
    units = [("encoder", policy.encoder[0], policy.encoder[1], None)]
    units += [(f"block{k + 1}", block[0], block[1], None) for k, block in enumerate(policy.blocks)]
    units += [(f"{name}_head", head[0], None, head[2]) for name, head in
              (("direction", policy.direction), ("button", policy.button), ("value", policy.value))]
    return units


def layer_activations(policy: Policy, observation, self_id, opponent_id, hidden, skill=None) -> dict:
    """The ReLU outputs of every layer ReDo looks after, for a batch of steps."""
    out = {}
    parts = [observation, policy.character(self_id), policy.character(opponent_id)]
    if policy.skills:
        parts.append(policy.skill(skill if skill is not None else torch.zeros_like(self_id)))
    h = policy.encoder(torch.cat(parts, dim=-1))
    out["encoder"] = h
    for k, block in enumerate(policy.blocks):
        z = block(h)
        out[f"block{k + 1}"] = z
        h = h + z
    memory = policy.memory(h, hidden)
    for name, head in (("direction", policy.direction), ("button", policy.button), ("value", policy.value)):
        out[f"{name}_head"] = head[1](head[0](memory))
    return out


@torch.no_grad()
def redo(policy: Policy, optimizer: torch.optim.Optimizer, activations: dict, tau: float = 0.025) -> dict:
    """ReDo (Sokar et al., 2023, "The Dormant Neuron Phenomenon in Deep Reinforcement Learning"): units whose average
    activation is below tau times their layer's average are dormant. A head unit gets fresh incoming weights and zero
    outgoing ones (as in the paper), so the policy plays the same right after. A unit behind a LayerNorm keeps its
    incoming weights (fresh ones would shift the whole layer's normalisation) and is reopened instead: LayerNorm scale
    0 and a tiny positive offset, so it outputs almost nothing yet its ReLU passes gradient again and it can grow back.
    Returns how many units each layer recycled."""
    recycled = {}
    for name, linear, norm, reader in _layer_units(policy):
        magnitude = activations[name].abs()
        score = magnitude.mean(0)
        layer = score.mean().clamp_min(1e-8)
        # Quiet on average and never firing strongly: a unit silent in most frames but important in rare ones stays.
        dormant = (score <= tau * layer) & (magnitude.max(0).values <= layer)
        count = int(dormant.sum())
        recycled[name] = count
        if not count:
            continue
        index = dormant.nonzero().squeeze(1)
        if norm is not None:
            # LayerNorm normalises each unit by the whole layer's mean and spread, so fresh incoming weights would
            # shift every other unit too. The incoming weights stay; the unit is reopened through its own scale and
            # offset instead, outputting almost nothing until training grows its scale again.
            norm.weight[index] = 0
            norm.bias[index] = 1e-3
            touched = [(norm.weight, (index,)), (norm.bias, (index,))]
        else:
            fresh = torch.empty_like(linear.weight)
            nn.init.kaiming_uniform_(fresh, a=math.sqrt(5))
            linear.weight[index] = fresh[index]
            linear.bias[index] = 0
            reader.weight[:, index] = 0
            touched = [(linear.weight, (index, slice(None))), (linear.bias, (index,)),
                       (reader.weight, (slice(None), index))]
        # Adam's running averages for the recycled weights start over too.
        for parameter, where in touched:
            state = optimizer.state.get(parameter)
            if state:
                for key in ("exp_avg", "exp_avg_sq"):
                    if key in state:
                        state[key][where] = 0
    return recycled
