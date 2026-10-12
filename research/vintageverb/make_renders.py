"""Runs synthetic test signals through Valhalla VintageVerb, for building a reverb like it.

Makes the test signals, renders every case in the matrix below through the plug-in with
substation-plugin-render (tools/plugin-render: the engine's own VST3 host), and writes:

    renders/inputs/<signal>.wav             the dry signals
    renders/<group>/<case>__<signal>.wav    the plug-in's output (100% wet, 32-bit float)
    renders/manifest.json                   every render: its input, parameters (normalized and as
                                            the plug-in shows them) and group

Usage:  python make_renders.py [--exe build-render/bin/substation-plugin-render.exe] [--only group,...]

Parameter values the plug-in shows (seconds, Hz, ms) are turned into its normalized values by
inverting the curves in parameters-full.txt (made with substation-plugin-render --list).
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PLUGIN = r"C:\Program Files\Common Files\VST3\ValhallaDSP\ValhallaVintageVerb.vst3"
RATE = 48000

# ---------------------------------------------------------------------------
# The plug-in's parameters

MODES = ["Concert Hall", "Plate", "Room", "Chamber", "Random Space", "Chorus Space", "Ambience",
         "Bright Hall", "Sanctuary", "Dirty Hall", "Dirty Plate", "Smooth Plate", "Smooth Room",
         "Smooth Random", "Nonlin", "Chaotic Chamber", "Chaotic Hall", "Chaotic Neutral", "Cathedral",
         "Palace", "Chamber1979", "Hall1984"]
COLORS = ["seventies", "eighties", "now"]

# ReverbMode is floor(value * 24) (0 and 1 both Concert Hall); ColorMode floor(value * 3) (0 and 1
# both seventies, 3 is 1.0 only). Values in the middle of each step, away from the edges.
def mode_value(name):
    return (MODES.index(name) + 1.5) / 24.0


def color_value(name):
    return min(1.0, (COLORS.index(name) + 1.5) / 3.0)


def load_curves():
    """{param: (normalized[], text[])} from parameters-full.txt."""
    curves, current = {}, None
    for line in (HERE / "parameters-full.txt").read_text().splitlines():
        if re.match(r"^\d+\t", line):
            current = line.split("\t")[1]
            curves[current] = ([], [])
        elif current and " = " in line:
            value, text = line.strip().split(" = ", 1)
            curves[current][0].append(float(value))
            curves[current][1].append(text)
    return curves


CURVES = load_curves()


def number(text):
    return float(re.match(r"[-+]?[0-9.]+", text).group())


def normalized(param, shown):
    """The normalized value at which `param` shows `shown` (its unit's number), interpolated."""
    values, texts = CURVES[param]
    shows = np.array([number(t) for t in texts])
    values = np.array(values)
    order = np.argsort(shows, kind="stable")
    shows, values = shows[order], values[order]
    keep = np.concatenate([[True], np.diff(shows) > 0])  # flat stretches: their first value
    if not shows[keep][0] <= shown <= shows[keep][-1]:
        raise ValueError(f"{param}: {shown} is out of range")
    return float(np.interp(shown, shows[keep], values[keep]))


DEFAULTS = {  # the plug-in's own, as normalized values (Mix, ReverbMode, ColorMode set per case)
    "PreDelay": 0.25, "Decay": 0.42048, "Size": 1.0, "Attack": 0.5, "BassMult": 0.623039,
    "BassXover": 0.381065, "HighShelf": 0.0, "HighFreq": 0.5, "EarlyDiffusion": 1.0,
    "LateDiffusion": 1.0, "ModRate": 0.245455, "ModDepth": 0.38, "HighCut": 0.590539, "LowCut": 0.0,
}

# ---------------------------------------------------------------------------
# Test signals (stereo, float, RATE). Each is the dry part followed by silence for the tail.

def seconds(s):
    return int(round(s * RATE))


def stereo(left, right=None):
    return np.stack([left, left if right is None else right], axis=1).astype(np.float32)


def pad(signal, total):
    out = np.zeros((seconds(total), 2), np.float32)
    out[: len(signal)] = signal[: len(out)]
    return out


def impulse(total, channel):  # channel: 0 left, 1 right, 2 both
    out = np.zeros((seconds(total), 2), np.float32)
    if channel in (0, 2):
        out[0, 0] = 1.0
    if channel in (1, 2):
        out[0, 1] = 1.0
    return out


def sine(freq, length, level_db, total):
    t = np.arange(seconds(length)) / RATE
    fade = np.minimum(1.0, np.minimum(t, length - t) / 0.01)  # 10 ms fades
    tone = 10 ** (level_db / 20) * np.sin(2 * np.pi * freq * t) * fade
    return pad(stereo(tone), total)


def exp_sweep(f1, f2, length, total):
    """Farina's exponential sine sweep (deconvolve with its inverse filter, see README)."""
    t = np.arange(seconds(length)) / RATE
    k = np.log(f2 / f1)
    sweep = 0.5 * np.sin(2 * np.pi * f1 * length / k * (np.exp(t * k / length) - 1))
    fade = np.minimum(1.0, np.minimum(t / 0.05, (length - t) / 0.01))
    return pad(stereo(sweep * fade), total)


def pink(n, rng):
    spectrum = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / RATE)
    spectrum[1:] /= np.sqrt(f[1:])
    spectrum[0] = 0
    x = np.fft.irfft(spectrum, n)
    return x / np.max(np.abs(x))


def noise_burst(length, total, seed, decorrelated=False):
    rng = np.random.default_rng(seed)
    n = seconds(length)
    fade = np.minimum(1.0, np.minimum(np.arange(n), n - np.arange(n)) / seconds(0.002))
    left = 0.5 * pink(n, rng) * fade
    right = 0.5 * pink(n, rng) * fade if decorrelated else left
    return pad(stereo(left, right), total)


def snare(total):
    rng = np.random.default_rng(7)
    t = np.arange(seconds(0.25)) / RATE
    body = 0.5 * np.sin(2 * np.pi * 185 * t) * np.exp(-t / 0.04)
    noise = rng.standard_normal(len(t)) * np.exp(-t / 0.06)
    noise = np.convolve(noise, [1, -0.6], "same")  # a little brighter
    hit = 0.8 * (body + 0.35 * noise) / np.max(np.abs(body + 0.35 * noise))
    return pad(stereo(hit), total)


def pluck(freq, length, seed):
    """Karplus-Strong string."""
    rng = np.random.default_rng(seed)
    period = int(round(RATE / freq))
    buffer = rng.uniform(-1, 1, period)
    out = np.zeros(seconds(length))
    for i in range(len(out)):
        out[i] = buffer[i % period]
        buffer[i % period] = 0.996 * 0.5 * (buffer[i % period] + buffer[(i + 1) % period])
    return out


def staccato_chords(total):
    """Short plucked chords (Am7, Fmaj7, C, G), one a beat at 100 bpm, panned a little."""
    chords = [[220.0, 261.63, 329.63, 392.0], [174.61, 220.0, 261.63, 329.63],
              [261.63, 329.63, 392.0, 523.25], [196.0, 246.94, 293.66, 392.0]]
    beat = 0.6
    left = np.zeros(seconds(total))
    right = np.zeros(seconds(total))
    for c, chord in enumerate(chords):
        start = seconds(c * beat)
        for v, f in enumerate(chord):
            note = pluck(f, 0.3, seed=10 * c + v)
            note *= np.minimum(1.0, (len(note) - np.arange(len(note))) / seconds(0.02))  # damp
            pan = (v - 1.5) / 3.0
            left[start:start + len(note)] += note * (1 - pan) * 0.2
            right[start:start + len(note)] += note * (1 + pan) * 0.2
    return stereo(left, right)


def vocal_ah(total):
    """A sung 'ah' at A3 with vibrato: a sawtooth through three formants, 1.5 s."""
    from scipy.signal import lfilter
    length = 1.5
    t = np.arange(seconds(length)) / RATE
    f0 = 220 * (1 + 0.01 * np.sin(2 * np.pi * 5.5 * t))
    phase = np.cumsum(f0) / RATE
    saw = 2 * (phase % 1) - 1
    voice = np.zeros_like(saw)
    for freq, bw, gain in [(730, 90, 1.0), (1090, 110, 0.5), (2440, 170, 0.25)]:
        r = np.exp(-np.pi * bw / RATE)
        a = [1, -2 * r * np.cos(2 * np.pi * freq / RATE), r * r]
        voice += gain * lfilter([1 - r], a, saw)
    voice *= np.minimum(1.0, np.minimum(t / 0.05, (length - t) / 0.15))
    return pad(stereo(0.5 * voice / np.max(np.abs(voice))), total)


# ---------------------------------------------------------------------------
# The matrix: groups of cases, each case a set of parameters and the signals it renders.

SWEEP_MODES = ["Concert Hall", "Plate", "Random Space", "Chorus Space"]
CORE_MODES = ["Concert Hall", "Bright Hall", "Plate", "Chamber", "Random Space", "Chorus Space",
              "Smooth Random", "Hall1984"]
IR = ["impulse_L", "impulse_R"]


def signals_for(decay_s):
    """Impulse lengths long enough for a decay (RT60) of decay_s to fall well below -100 dB."""
    return max(6.0, round(2.0 * decay_s + 4.0))


def build_matrix():
    cases = []  # (group, name, params, [signals], tail seconds)

    def case(group, name, params, signals, tail=12.0):
        cases.append((group, name, params, signals, tail))

    base = {"Mix": 1.0, "PreDelay": 0.0}  # wet only, no predelay: IRs start at the input

    # 1. Every mode in every color, at the plug-in's defaults (4 s decay): left and right impulses
    #    give the 2x2 stereo IR; a decorrelated noise burst the decay with the modulation averaged;
    #    silence the noise some modes make by themselves (Sanctuary to Nonlin: about -90 dB).
    for color in COLORS:
        for mode in MODES:
            p = dict(base, ReverbMode=mode_value(mode), ColorMode=color_value(color))
            case("modes", f"{slug(mode)}_{color}", p, IR + ["burst_pink_stereo_500ms", "silence"])

    # 2. Parameter sweeps on four core modes in 1970s color: the left impulse (L->L and L->R).
    for color in ["seventies"]:
        for mode in SWEEP_MODES:
            m = dict(base, ReverbMode=mode_value(mode), ColorMode=color_value(color))
            tag = f"{slug(mode)}_{color}"
            for d in [0.5, 1.0, 2.0, 4.0, 8.0, 16.0]:
                case("decay", f"{tag}_decay{d:g}s", dict(m, Decay=normalized("Decay", d)), ["impulse_L"], signals_for(d))
            for s in [0.0, 0.25, 0.5, 0.75]:
                case("size", f"{tag}_size{int(s * 100)}", dict(m, Size=s), ["impulse_L"])
            for a in [0.0, 1.0]:
                case("attack", f"{tag}_attack{int(a * 100)}", dict(m, Attack=a), ["impulse_L"])
            for e, l in [(0.0, 1.0), (0.5, 1.0), (1.0, 0.0), (1.0, 0.5), (0.0, 0.0)]:
                case("diffusion", f"{tag}_early{int(e * 100)}_late{int(l * 100)}",
                     dict(m, EarlyDiffusion=e, LateDiffusion=l), ["impulse_L"])
            for depth in [0.0, 0.25, 0.5, 0.75, 1.0]:
                case("moddepth", f"{tag}_depth{int(depth * 100)}", dict(m, ModDepth=depth),
                     ["impulse_L", "sine_1000Hz_-12dB_3s"])
            for rate in [0.25, 1.0, 5.0, 10.0]:
                case("modrate", f"{tag}_rate{rate:g}Hz", dict(m, ModRate=normalized("ModRate", rate)),
                     ["impulse_L", "sine_1000Hz_-12dB_3s"])
    # Filters and the decay's EQ: Concert Hall 1970s only (they should act alike in every mode).
    m = dict(base, ReverbMode=mode_value("Concert Hall"), ColorMode=color_value("seventies"))
    for hz in [1000, 2000, 4000, 8000, 12000, 20000]:
        case("highcut", f"concert_hall_seventies_highcut{hz}", dict(m, HighCut=normalized("HighCut", hz)), ["impulse_L"])
    for hz in [10, 100, 300, 800, 1500]:
        case("lowcut", f"concert_hall_seventies_lowcut{hz}", dict(m, LowCut=normalized("LowCut", hz)), ["impulse_L"])
    for mult in [0.25, 0.5, 1.0, 1.5, 2.5, 4.0]:
        case("bassmult", f"concert_hall_seventies_bassmult{mult:g}", dict(m, BassMult=normalized("BassMult", mult)), ["impulse_L"])
    for hz in [100, 300, 700, 2000]:
        case("bassxover", f"concert_hall_seventies_bassxover{hz}",
             dict(m, BassMult=normalized("BassMult", 3.0), BassXover=normalized("BassXover", hz)), ["impulse_L"])
    for db in [-24, -12, -6, -3, 0]:
        case("highshelf", f"concert_hall_seventies_highshelf{db}dB", dict(m, HighShelf=normalized("HighShelf", db)), ["impulse_L"])
    for hz in [1000, 3000, 6000, 12000]:
        case("highfreq", f"concert_hall_seventies_highfreq{hz}",
             dict(m, HighShelf=normalized("HighShelf", -12), HighFreq=normalized("HighFreq", hz)), ["impulse_L"])
    for ms in [0, 20, 100, 250]:
        case("predelay", f"concert_hall_seventies_predelay{ms}ms", dict(m, PreDelay=normalized("PreDelay", ms)), ["impulse_L"])

    # 3. Time variance: the same impulse five times (the modulation is free-running), and with no modulation.
    for mode in ["Concert Hall", "Chorus Space", "Random Space", "Plate"]:
        mm = dict(base, ReverbMode=mode_value(mode), ColorMode=color_value("seventies"))
        for depth_name, extra in [("defaultmod", {}), ("nomod", {"ModDepth": 0.0})]:
            for k in range(5):
                case("repeat", f"{slug(mode)}_seventies_{depth_name}_take{k + 1}", dict(mm, **extra), ["impulse_L"])

    # 4. Level (linearity, saturation, noise floor) in each color: impulses and a sine at several levels, silence.
    for color in COLORS:
        mm = dict(base, ReverbMode=mode_value("Concert Hall"), ColorMode=color_value(color))
        case("level", f"concert_hall_{color}", mm,
             ["impulse_L_-40dB", "impulse_L", "sine_1000Hz_-36dB_3s", "sine_1000Hz_-12dB_3s",
              "sine_1000Hz_0dB_3s", "sine_100Hz_-6dB_3s", "sine_8000Hz_-6dB_3s", "silence"])

    # 5. Sustained tones, sweeps and music-like sounds through the core modes, vintage colors (listening, spectra).
    for color in ["seventies", "eighties"]:
        for mode in CORE_MODES:
            mm = dict(base, ReverbMode=mode_value(mode), ColorMode=color_value(color))
            case("program", f"{slug(mode)}_{color}", mm,
                 ["sine_220Hz_-12dB_3s", "sine_3000Hz_-12dB_3s", "sweep_20-20000Hz_8s", "snare",
                  "staccato_chords", "vocal_ah"])

    # 6. A few lush settings as a listener would dial them in (long, dark, deep modulation).
    lush = [
        ("lush_hall", "Concert Hall", "seventies", {"Decay": normalized("Decay", 6.0), "ModDepth": 0.6,
            "ModRate": normalized("ModRate", 0.6), "HighCut": normalized("HighCut", 5000), "PreDelay": normalized("PreDelay", 30)}),
        ("lush_chorus_space", "Chorus Space", "seventies", {"Decay": normalized("Decay", 5.0), "ModDepth": 0.7,
            "HighCut": normalized("HighCut", 6000), "PreDelay": normalized("PreDelay", 20)}),
        ("lush_random_space", "Random Space", "eighties", {"Decay": normalized("Decay", 8.0), "ModDepth": 0.5,
            "HighCut": normalized("HighCut", 7000), "PreDelay": normalized("PreDelay", 40)}),
        ("lush_plate", "Plate", "seventies", {"Decay": normalized("Decay", 3.0), "ModDepth": 0.5,
            "HighCut": normalized("HighCut", 8000), "PreDelay": normalized("PreDelay", 10)}),
        ("lush_smooth_random", "Smooth Random", "eighties", {"Decay": normalized("Decay", 10.0), "ModDepth": 0.6,
            "HighCut": normalized("HighCut", 4500), "BassMult": normalized("BassMult", 1.4)}),
    ]
    for name, mode, color, extra in lush:
        p = dict(base, ReverbMode=mode_value(mode), ColorMode=color_value(color), **extra)
        case("lush", name, p, IR + ["snare", "staccato_chords", "vocal_ah", "sweep_20-20000Hz_8s"], 24.0)
    return cases


def slug(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def make_signal(name, tail):
    """The named signal, followed by silence to `tail` seconds after it ends."""
    if name.startswith("impulse_"):
        channel = {"L": 0, "R": 1, "LR": 2}[name.split("_")[1]]
        x = impulse(tail, channel)
        if name.endswith("dB"):
            x *= 10 ** (float(name.split("_")[2][:-2]) / 20)
        return x
    if name == "silence":
        return np.zeros((seconds(tail), 2), np.float32)
    if name.startswith("sine_"):
        _, f, level, length = name.split("_")
        length = float(length[:-1])
        return sine(float(f[:-2]), length, float(level[:-2]), length + tail)
    if name.startswith("sweep_"):
        f1, f2 = (float(v) for v in name.split("_")[1][:-2].split("-"))
        length = float(name.split("_")[2][:-1])
        return exp_sweep(f1, f2, length, length + tail)
    if name == "burst_pink_stereo_500ms":
        return noise_burst(0.5, 0.5 + tail, seed=1, decorrelated=True)
    if name == "snare":
        return snare(tail + 0.25)
    if name == "staccato_chords":
        return staccato_chords(tail + 2.4)
    if name == "vocal_ah":
        return vocal_ah(tail + 1.5)
    raise ValueError(name)


# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(ROOT / "build-render" / "bin" / "substation-plugin-render.exe"))
    parser.add_argument("--out", default=str(HERE / "renders"))
    parser.add_argument("--only", default="", help="comma-separated groups")
    args = parser.parse_args()
    out = Path(args.out)
    only = set(filter(None, args.only.split(",")))

    cases = [c for c in build_matrix() if not only or c[0] in only]
    (out / "inputs").mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="vv-"))

    # The dry signals, as raw floats for the renderer and as WAVs to keep.
    inputs = {}
    for _, _, _, signals, tail in cases:
        for s in signals:
            key = f"{s}__{tail:g}s"
            if key in inputs:
                continue
            x = make_signal(s, tail)
            raw = work / f"{key}.f32"
            x.astype("<f4").tofile(raw)
            wavfile.write(out / "inputs" / f"{key}.wav", RATE, x)
            inputs[key] = raw

    jobs, manifest = [], []
    for group, name, params, signals, tail in cases:
        (out / group).mkdir(exist_ok=True)
        assignment = ";".join(f"{k}={v:.6f}" for k, v in params.items())
        full = dict(DEFAULTS, **params)
        for s in signals:
            key = f"{s}__{tail:g}s"
            wav = out / group / f"{name}__{s}.wav"
            raw = work / f"{group}__{name}__{s}.f32"
            jobs.append(f"{raw}|{inputs[key]}|{assignment}")
            manifest.append({"file": str(wav.relative_to(out)).replace("\\", "/"), "group": group, "case": name,
                             "input": f"inputs/{key}.wav", "signal": s, "params": full,
                             "params_shown": shown(full), "raw": str(raw)})
    jobs_file = work / "jobs.txt"
    jobs_file.write_text("\n".join(jobs) + "\n")
    print(f"{len(jobs)} renders, {len(inputs)} signals; working in {work}", flush=True)

    env = dict(os.environ)
    env["PATH"] = r"C:\Qt\Tools\mingw1310_64\bin;" + env["PATH"]  # the MinGW runtime the tool links
    done = subprocess.run([args.exe, PLUGIN, "--jobs", str(jobs_file), "--rate", str(RATE), "--warmup", "1.5"],
                          env=env, stdout=subprocess.PIPE, text=True)
    if done.returncode != 0:
        sys.exit(f"render failed ({done.returncode})")

    for entry in manifest:
        raw = Path(entry.pop("raw"))
        y = np.fromfile(raw, "<f4").reshape(-1, 2)
        wavfile.write(out / entry["file"], RATE, y)
        entry["peak_db"] = float(20 * np.log10(np.max(np.abs(y)) + 1e-30))
        raw.unlink()
    previous = []
    if only and (out / "manifest.json").exists():
        previous = [e for e in json.loads((out / "manifest.json").read_text()) if e["group"] not in only]
    (out / "manifest.json").write_text(json.dumps(previous + manifest, indent=1))
    for raw in inputs.values():
        raw.unlink()
    jobs_file.unlink()
    work.rmdir()
    print(f"wrote {len(manifest)} renders to {out}")


def shown(params):
    """The parameters as the plug-in shows them (from the curves in parameters-full.txt)."""
    result = {}
    for k, v in params.items():
        if k == "ReverbMode":
            result[k] = MODES[max(0, int(v * 24) - 1)]
        elif k == "ColorMode":
            result[k] = COLORS[max(0, int(v * 3) - 1)]
        elif k in CURVES:
            values, texts = CURVES[k]
            result[k] = texts[int(np.argmin(np.abs(np.array(values) - v)))]
    return result


if __name__ == "__main__":
    main()
