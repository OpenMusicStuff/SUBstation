"""Measurements of a reverb's response, the same for VintageVerb's renders and for our clone's.

    python analyze.py <render.wav> [<other.wav>]     prints the metrics (side by side with a second file)

Every function takes a float array (frames, 2) at RATE.
"""

import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

RATE = 48000
OCTAVES = [63, 125, 250, 500, 1000, 2000, 4000, 8000]
THIRDS = [31, 63, 125, 250, 500, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 12000, 14000, 16000, 18000]


def load(path):
    rate, y = wavfile.read(path)
    assert rate == RATE, path
    return y.astype(np.float64)


def db(x):
    return 10 * np.log10(np.maximum(x, 1e-30))


def edc_db(x):
    e = np.cumsum(x[::-1] ** 2)[::-1]
    return db(e / max(e[0], 1e-30))


def rt60(x, top=-5.0, bottom=-25.0):
    """RT60 from the Schroeder curve between `top` and `bottom` dB (T20 by default), by a line fit."""
    d = edc_db(x)
    a, b = np.argmax(d < top), np.argmax(d < bottom)
    if b <= a + 10:
        return float("nan")
    t = np.arange(a, b) / RATE
    slope = np.polyfit(t, d[a:b], 1)[0]
    return -60.0 / slope


def octave(x, fc):
    hi = min(fc * 2 ** 0.5, 0.45 * RATE)
    return sosfilt(butter(3, [fc / 2 ** 0.5, hi], "bandpass", fs=RATE, output="sos"), x)


def band_rt60(y):
    """RT60 per octave, both channels' energy."""
    return {fc: rt60(np.sqrt(octave(y[:, 0], fc) ** 2 + octave(y[:, 1], fc) ** 2)) for fc in OCTAVES}


def spectrum(y, t0, t1, bands=THIRDS, ref=1000):
    """Energy per third-octave of y[t0:t1] (seconds), dB relative to the `ref` band (both channels)."""
    seg = y[int(t0 * RATE):int(t1 * RATE)]
    n = 1 << 17
    p = np.abs(np.fft.rfft(seg[:, 0], n)) ** 2 + np.abs(np.fft.rfft(seg[:, 1], n)) ** 2
    f = np.fft.rfftfreq(n, 1 / RATE)
    v = {b: db(np.mean(p[(f > b / 2 ** (1 / 6)) & (f < b * 2 ** (1 / 6))])) for b in bands}
    return {b: v[b] - v[ref] for b in bands}


def envelope(y, channel, step_ms=2.0, length_ms=300.0):
    w = int(step_ms / 1000 * RATE)
    x = y[:, channel]
    return np.array([db(np.sum(x[i:i + w] ** 2)) for i in range(0, int(length_ms / 1000 * RATE), w)])


def onset_ms(y, channel, threshold=1e-3):
    peak = np.max(np.abs(y[:, channel]))
    return float(np.argmax(np.abs(y[:, channel]) > threshold * peak) / RATE * 1000)


def echo_density(y, channel, times=(0.01, 0.02, 0.03, 0.05, 0.08, 0.12, 0.2, 0.3, 0.5), win=0.02):
    """Abel and Huang's normalized echo density: 1 is Gaussian noise, low is sparse echoes."""
    x, w = y[:, channel], int(win * RATE)
    out = {}
    for t in times:
        s = int(t * RATE)
        seg = x[max(0, s - w // 2):s + w // 2]
        out[t] = float(np.mean(np.abs(seg) > np.std(seg)) / 0.3173)
    return out


def energy_db(y):
    return {"L": float(db(np.sum(y[:, 0] ** 2))), "R": float(db(np.sum(y[:, 1] ** 2)))}


def correlation(y, t0=0.1, t1=2.0):
    seg = y[int(t0 * RATE):int(t1 * RATE)]
    return float(np.corrcoef(seg[:, 0], seg[:, 1])[0, 1])


def tone_spread(y, freq, start, length=1.0):
    """How wide a held tone's tail spreads in frequency: the width (Hz) of the band around `freq` holding
    90 % of the tail's energy near it, from `start` seconds (after the tone stops)."""
    seg = y[int(start * RATE):int((start + length) * RATE)]
    n = 1 << 18
    win = np.hanning(len(seg))
    p = np.abs(np.fft.rfft(seg[:, 0] * win, n)) ** 2 + np.abs(np.fft.rfft(seg[:, 1] * win, n)) ** 2
    f = np.fft.rfftfreq(n, 1 / RATE)
    near = (f > freq * 0.7) & (f < freq * 1.3)
    pf, ff = p[near], f[near]
    order = np.argsort(np.abs(ff - freq))
    cum = np.cumsum(pf[order]) / np.sum(pf)
    return float(2 * np.abs(ff[order][np.argmax(cum > 0.9)] - freq))


def report(y):
    lines = []
    lines.append("energy " + " ".join(f"{k}:{v:.1f}" for k, v in energy_db(y).items()))
    lines.append(f"onset L {onset_ms(y, 0):.2f} ms  R {onset_ms(y, 1):.2f} ms   L/R corr {correlation(y):+.3f}")
    lines.append("RT60 " + " ".join(f"{k}:{v:.2f}" for k, v in band_rt60(y).items()))
    for t0, t1 in [(0, 0.08), (0.08, 0.5), (0.5, 2.0)]:
        lines.append(f"spec {t0}-{t1}s " + " ".join(f"{k}:{v:+.0f}" for k, v in spectrum(y, t0, t1).items()))
    for ch in range(2):
        lines.append(f"NED {'LR'[ch]} " + " ".join(f"{k}:{v:.2f}" for k, v in echo_density(y, ch).items()))
        lines.append(f"env {'LR'[ch]} " + " ".join(f"{v:.0f}" for v in envelope(y, ch, 5.0, 300.0)))
    return lines


if __name__ == "__main__":
    reports = [report(load(p)) for p in sys.argv[1:]]
    for i in range(len(reports[0])):
        for r in reports:
            print(r[i])
        if len(reports) > 1:
            print()
