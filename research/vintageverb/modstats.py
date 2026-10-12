"""Modulation statistics, VintageVerb against the clone: band RT60s, a 1 kHz tone's spread in the tail."""
import sys
sys.path.insert(0, '.')
import numpy as np
import analyze as A
from clone import both_many
from vv import impulse, RATE


def sine(freq, length, level_db, total):
    t = np.arange(int(length * RATE)) / RATE
    fade = np.minimum(1, np.minimum(t, length - t) / 0.01)
    x = np.zeros((int(total * RATE), 2), np.float32)
    x[:len(t), 0] = x[:len(t), 1] = 10 ** (level_db / 20) * np.sin(2 * np.pi * freq * t) * fade
    return x


CASES = [("default", {}), ("depth 0", {"ModDepth": 0.0}), ("depth .25", {"ModDepth": 0.25}), ("depth 1", {"ModDepth": 1.0}),
         ("rate 0.25Hz", {"ModRate": 0.0152}), ("rate 1Hz", {"ModRate": 0.0909}), ("rate 5Hz", {"ModRate": 0.495}),
         ("rate 10Hz", {"ModRate": 1.0})]

if __name__ == "__main__":
    cases = CASES
    jobs = []
    for _, p in cases:
        jobs += [(p, impulse(10, 0)), (p, sine(1000, 3, -12, 6)), (p, sine(3000, 3, -12, 6))]
    res = both_many(jobs)
    for i, (nm, _) in enumerate(cases):
        (ri, oi), (rs, os_), (rs3, os3) = res[3 * i], res[3 * i + 1], res[3 * i + 2]
        print(f"== {nm}")
        for lab, y, s, s3 in (("ref", ri, rs, rs3), ("ours", oi, os_, os3)):
            r = A.band_rt60(y)
            print(f"  {lab:4s} RT " + " ".join(f"{k}:{v:.2f}" for k, v in r.items()),
                  f"| 1k spread {A.tone_spread(s, 1000, 3.2, 0.8):5.1f} {A.tone_spread(s, 1000, 4.0, 1.0):5.1f}"
                  f" | 3k {A.tone_spread(s3, 3000, 3.2, 0.8):5.1f} {A.tone_spread(s3, 3000, 4.0, 1.0):5.1f}")
