"""Renders VintageVerb (Concert Hall, 1970s) and the built-in Vintage Hall with the same settings, to compare.

    from clone import both
    ref, ours = both({"ModDepth": 0.0}, impulse(6.0, 0))

Settings are VintageVerb's normalized parameters (vv.CONCERT_70S underneath); the clone gets the same values
as the plug-in shows them, in its own units.
"""

import re
from pathlib import Path

import numpy as np

from vv import CONCERT_70S, render_many

HERE = Path(__file__).resolve().parent
DEVICE = "builtin:vintage_hall"


def _curves():
    curves, current = {}, None
    for line in (HERE / "parameters-full.txt").read_text().splitlines():
        if re.match(r"^\d+\t", line):
            current = line.split("\t")[1]
            curves[current] = ([], [])
        elif current and " = " in line:
            value, text = line.strip().split(" = ", 1)
            m = re.match(r"[-+]?[0-9.]+", text)
            if m:
                curves[current][0].append(float(value))
                curves[current][1].append(float(m.group()))
    return curves


CURVES = _curves()
DEFAULTS = {"Mix": 1.0, "PreDelay": 0.25, "Decay": 0.42048, "Size": 1.0, "Attack": 0.5, "BassMult": 0.623039,
            "BassXover": 0.381065, "HighShelf": 0.0, "HighFreq": 0.5, "EarlyDiffusion": 1.0, "LateDiffusion": 1.0,
            "ModRate": 0.245455, "ModDepth": 0.38, "HighCut": 0.590539, "LowCut": 0.0}


def shown(param, value):
    values, texts = CURVES[param]
    return float(np.interp(value, values, texts))


def to_clone(params):
    """VintageVerb's normalized parameters -> the built-in's plain ones."""
    p = dict(DEFAULTS, **{k: v for k, v in dict(CONCERT_70S, **params).items() if k in DEFAULTS})
    return {
        "mix": 100.0 * p["Mix"],
        "predelay": shown("PreDelay", p["PreDelay"]),
        "decay": 1000.0 * shown("Decay", p["Decay"]),
        "size": 100.0 * p["Size"],
        "attack": 100.0 * p["Attack"],
        "bass_mult": shown("BassMult", p["BassMult"]),
        "bass_xover": shown("BassXover", p["BassXover"]),
        "high_shelf": shown("HighShelf", p["HighShelf"]),
        "high_freq": shown("HighFreq", p["HighFreq"]),
        "early_diffusion": 100.0 * p["EarlyDiffusion"],
        "late_diffusion": 100.0 * p["LateDiffusion"],
        "mod_rate": shown("ModRate", p["ModRate"]),
        "mod_depth": 100.0 * p["ModDepth"],
        "high_cut": shown("HighCut", p["HighCut"]),
        "low_cut": shown("LowCut", p["LowCut"]),
    }


def both(params, signal):
    """(VintageVerb's output, the clone's) for the same settings and signal."""
    return both_many([(params, signal)])[0]


def both_many(jobs):
    refs = render_many([(dict(CONCERT_70S, **p), s) for p, s in jobs])
    ours = render_many([(to_clone(p), s) for p, s in jobs], device=DEVICE)
    return list(zip(refs, ours))
