from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from mugen_env import MugenEnv

root = Path(__file__).resolve().parents[1]
with MugenEnv(root / "build/pc_test/mugen360.exe", root, "data/chars/kfm/kfm.def",
              "data/chars/kfm/kfm.def", "data/stages/kfm.def") as env:
    state = env.reset(123)
    assert state.observation.shape == (121,)
    assert state.opponent_observation.shape == (121,)
    state = env.step((1, 1))
    assert np.isfinite(state.observation).all() and np.isfinite(state.reward)
print("RL protocol smoke test passed")
