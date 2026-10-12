"""Loop-only comparison: the clone's output corrected by the input/output transfer difference (measured on the
first tap with no diffusion), so what remains is the ring's own difference."""
import sys
sys.path.insert(0, '.')
import numpy as np
from scipy.signal import butter, sosfiltfilt
from vv import impulse
from clone import both_many

N = 1 << 19
SOS = butter(8, 10000, fs=48000, output='sos')


def correction():
    (ref, ours), = both_many([({"ModDepth": 0, "EarlyDiffusion": 0, "LateDiffusion": 0, "Attack": 0.0}, impulse(0.1, 0))])
    w = np.zeros(len(ref)); w[:4000] = 1
    return np.fft.rfft(ours[:, 0] * w, N) / np.fft.rfft(ref[:, 0] * w, N)


def errors(ref, ours, H, spans=((0, 4800), (4800, 24000), (24000, 48000), (48000, 96000), (96000, None))):
    out = []
    for o in range(2):
        fixed = np.fft.irfft(np.fft.rfft(ours[:, o], N) / H, N)[:len(ref)]
        r = sosfiltfilt(SOS, ref[:, o]); d = sosfiltfilt(SOS, fixed) - r
        out.append([10 * np.log10(np.sum(d[a:b] ** 2) / max(np.sum(r[a:b] ** 2), 1e-30)) for a, b in spans])
    return out


def report(cases, seconds=4.0):
    H = correction()
    res = both_many([(p, impulse(seconds, c)) for _, p in cases for c in (0, 1)])
    k = 0
    for nm, _ in cases:
        for c in (0, 1):
            e = errors(*res[k], H); k += 1
            print(f"{nm:16s} in{'LR'[c]}", " | ".join(f"out{'LR'[o]} " + " ".join(f"{v:6.1f}" for v in e[o]) for o in range(2)))


if __name__ == "__main__":
    report([("sparse", {"ModDepth": 0, "EarlyDiffusion": 0, "LateDiffusion": 0, "Attack": 0.0}),
            ("early only", {"ModDepth": 0, "EarlyDiffusion": 1, "LateDiffusion": 0, "Attack": 0.0}),
            ("early .3", {"ModDepth": 0, "EarlyDiffusion": 0.3, "LateDiffusion": 0, "Attack": 0.0}),
            ("late 1", {"ModDepth": 0, "EarlyDiffusion": 0, "LateDiffusion": 1, "Attack": 0.0}),
            ("all nomod", {"ModDepth": 0})])
