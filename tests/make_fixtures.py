"""Builds real slot data with the world (stub Archipelago, as the Python tests do) and records what the
Python companion decides for many random item/coin states. The C++ test must agree on every one."""
import json, random, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "companion"))
sys.path.insert(0, str(ROOT / "tests"))
from test_companion import build                     # noqa: E402 (installs the AP stubs)
from core import Progress, RunState, SlotConfig       # noqa: E402

out = []
rng = random.Random(7)
for seed, opts in ((1, {}), (2, {"gauntlet_keys": 1}), (3, {"include_coins": 0}), (4, {"level_unlocks": 0}),
                   (5, {"coin_locks": 0}), (6, {"include_demons": 0, "include_gauntlets": 0})):
    mw, w = build(seed, **opts)
    sd = w.fill_slot_data()
    cfg = SlotConfig.from_slot_data(sd)
    ids = list(cfg.items)
    cases = []
    for _ in range(60):
        received = rng.sample(ids, rng.randint(0, len(ids))) + [999_999] * rng.randint(0, 2)
        coins = set()
        for lv in cfg.levels:
            for n in range(1, len(lv["coin_ids"]) + 1):
                if rng.random() < 0.35:
                    coins.add(f"{lv['idx']}:{n}")
        completed = {lv["idx"] for lv in cfg.levels if rng.random() < 0.3}
        st = RunState(completed=completed, coins=coins)
        prog = Progress(cfg, st, received)
        cases.append({"received": received, "coins": sorted(coins), "completed": sorted(completed),
                      "reasons": {str(lv["idx"]): prog.lock_reason(lv) for lv in cfg.levels},
                      "levels_done": prog.levels_done(), "goal": prog.goal_complete()})
    out.append({"opts": opts, "slot_data": sd, "cases": cases})
(Path(__file__).parent / "fixtures.json").write_text(json.dumps(out))
print("fixtures:", sum(len(o["cases"]) for o in out), "cases")
