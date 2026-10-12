"""Ad-hoc renders through a VST3 effect or a built-in device, for analysis: render(params, signal) -> (frames, 2).

    from vv import render, impulse
    y = render({"ModDepth": 0, "EarlyDiffusion": 0}, impulse(6.0, 0))
    y = render({"decay": 4.0}, impulse(6.0, 0), device="builtin:vintage_hall")

`params` are normalized values (VST3) or plain ones (built-in devices), by name or id; choices by name too.
"""

import os
import subprocess
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
EXE = HERE.parent.parent / "build-render" / "bin" / "substation-plugin-render.exe"
VINTAGEVERB = r"C:\Program Files\Common Files\VST3\ValhallaDSP\ValhallaVintageVerb.vst3"
RATE = 48000

MODES = ["Concert Hall", "Plate", "Room", "Chamber", "Random Space", "Chorus Space", "Ambience",
         "Bright Hall", "Sanctuary", "Dirty Hall", "Dirty Plate", "Smooth Plate", "Smooth Room",
         "Smooth Random", "Nonlin", "Chaotic Chamber", "Chaotic Hall", "Chaotic Neutral", "Cathedral",
         "Palace", "Chamber1979", "Hall1984"]
COLORS = ["seventies", "eighties", "now"]
CONCERT_70S = {"Mix": 1.0, "PreDelay": 0.0, "ReverbMode": 1.5 / 24, "ColorMode": 0.5}


def impulse(seconds, channel, gain=1.0):
    x = np.zeros((int(seconds * RATE), 2), np.float32)
    if channel in (0, 2):
        x[0, 0] = gain
    if channel in (1, 2):
        x[0, 1] = gain
    return x


def render_many(jobs, device=VINTAGEVERB, warmup=1.5):
    """jobs: [(params, signal)] -> [output]. One process for all of them."""
    work = Path(tempfile.mkdtemp(prefix="vvr-"))
    lines, outs = [], []
    for i, (params, signal) in enumerate(jobs):
        src, dst = work / f"in{i}.f32", work / f"out{i}.f32"
        np.asarray(signal, "<f4").tofile(src)
        lines.append(f"{dst}|{src}|" + ";".join(f"{k}={v}" for k, v in params.items()))
        outs.append(dst)
    (work / "jobs.txt").write_text("\n".join(lines) + "\n")
    env = dict(os.environ)
    env["PATH"] = r"C:\Qt\Tools\mingw1310_64\bin;" + env["PATH"]
    subprocess.run([str(EXE), device, "--jobs", str(work / "jobs.txt"), "--warmup", str(warmup)],
                   env=env, check=True, stdout=subprocess.DEVNULL)
    result = [np.fromfile(o, "<f4").reshape(-1, 2).astype(np.float64) for o in outs]
    for f in work.iterdir():
        f.unlink()
    work.rmdir()
    return result


def render(params, signal, device=VINTAGEVERB, base=None):
    if base is None:
        base = CONCERT_70S if device == VINTAGEVERB else {}
    return render_many([(dict(base, **params), signal)], device)[0]
