"""Renders a study corpus through Xynth Audio's Chroma (VST3) with substation-fxrender.

Test signals (synthetic, and excerpts of real samples) go through the plug-in
under a matrix of settings, for working out what it does: which pitches it
favours, how sharply, how fast, and what each control changes. Everything lands
in one folder:

    signals/<name>.wav            the inputs, 48 kHz float32 stereo
    renders/<config>__<signal>.wav  the plug-in's outputs: a new instance each, after 0.5 s of
                                  silence; latency the plug-in reports removed (it reports
                                  none but High Quality's 1024: align by cross-correlation)
    params.jsonl                  the plug-in's parameters, as it reports them
    manifest.json                 every render: signal, config, settings, files
    signals.json                  what each signal is (frequencies, timing)

    python tools/fxrender/chroma_study.py [--out DIR] [--samples DIR] [--exe PATH]
"""

import argparse
import json
import math
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import scipy.io.wavfile as wavfile
from scipy.signal import resample_poly

SR = 48000
PLUGIN = r"C:\Program Files\Common Files\VST3\Chroma.vst3"
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
A4 = 440.0
LEVEL = 10 ** (-12 / 20)  # -12 dBFS peak for test tones
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

rng = np.random.default_rng(1234)


def hz(midi):
    return A4 * 2 ** ((midi - 69) / 12)


def fade(x, ms=5):
    n = int(SR * ms / 1000)
    if n and len(x) > 2 * n:
        ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))
        if x.ndim == 2:
            ramp = ramp[:, None]
        x[:n] *= ramp
        x[-n:] *= ramp[::-1]
    return x


def sine(f, seconds, amp=LEVEL, phase=0.0):
    t = np.arange(int(SR * seconds)) / SR
    return amp * np.sin(2 * np.pi * f * t + phase)


def additive(f0, seconds, partials, amp=LEVEL):
    """Sum of partials (ratio, gain); band-limited below 20 kHz, normalised to `amp` peak."""
    t = np.arange(int(SR * seconds)) / SR
    x = np.zeros_like(t)
    for k, (ratio, gain) in enumerate(partials):
        if f0 * ratio < 20000:
            x += gain * np.sin(2 * np.pi * f0 * ratio * t + rng.uniform(0, 2 * np.pi))
    return amp * x / np.max(np.abs(x))


def saw_partials(n=60):
    return [(k, 1 / k) for k in range(1, n + 1)]


def pink(seconds, amp=LEVEL):
    n = int(SR * seconds)
    spectrum = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    spectrum[1:] /= np.sqrt(f[1:])
    spectrum[0] = 0
    x = np.fft.irfft(spectrum, n)
    return amp * x / np.max(np.abs(x))


def synthetic_signals():
    """name -> (samples, description dict)."""
    s = {}

    x = np.zeros(SR * 3)
    for i in range(5):
        x[int(SR * (0.25 + 0.5 * i))] = 0.5
    s["impulses"] = (x, {"kind": "impulse train", "at_s": [0.25 + 0.5 * i for i in range(5)], "amp": 0.5})

    # Exponential sine sweep (Farina): the transfer function of a linear, static
    # system; and where the comb's peaks sit, at a glance in its spectrogram.
    T, f1, f2 = 12.0, 20.0, 20000.0
    t = np.arange(int(SR * T)) / SR
    L = T / math.log(f2 / f1)
    s["sweep_log"] = (fade(LEVEL * np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1)), 20),
                      {"kind": "exponential sine sweep", "f1": f1, "f2": f2, "seconds": T, "amp": LEVEL})

    s["noise_white"] = (fade(LEVEL * np.clip(rng.standard_normal(SR * 8) / 3.5, -1, 1)), {"kind": "white noise", "seconds": 8})
    s["noise_pink"] = (fade(pink(8)), {"kind": "pink noise", "seconds": 8})

    # Steady sines, one per step: fine (10 cents, A3..A4 and back to A3's neighbours)
    # for the shape of a peak; coarse (semitones, 55 Hz..14 kHz) for the range.
    step = 0.25
    fine = [57 + k * 0.1 for k in range(0, 121)]  # A3 to A4 in 10-cent steps
    s["steps_fine"] = (np.concatenate([fade(sine(hz(m), step)) for m in fine]),
                       {"kind": "stepped sines", "step_s": step, "fade_ms": 5, "midi": fine, "hz": [hz(m) for m in fine]})
    coarse = list(range(33, 128))  # A1 (55 Hz) to G9; above 20 kHz dropped
    coarse = [m for m in coarse if hz(m) < 16000]
    s["steps_semitones"] = (np.concatenate([fade(sine(hz(m), step)) for m in coarse]),
                            {"kind": "stepped sines", "step_s": step, "fade_ms": 5, "midi": coarse, "hz": [hz(m) for m in coarse]})
    quarter = [m + 0.5 for m in range(45, 93)]  # quarter tones (A2+50c .. G#6+50c): worst case for a quantizer
    s["steps_quartertones"] = (np.concatenate([fade(sine(hz(m), step)) for m in quarter]),
                               {"kind": "stepped sines", "step_s": step, "fade_ms": 5, "midi": quarter, "hz": [hz(m) for m in quarter]})

    for name, f in [("sine_A4_440", 440.0), ("sine_A4p30c", hz(69.3)), ("sine_A4p50c", hz(69.5)),
                    ("sine_C4_261", hz(60)), ("sine_1k", 1000.0), ("sine_A2_110", 110.0), ("sine_A6_1760", 1760.0)]:
        s[name] = (fade(sine(f, 3)), {"kind": "sine", "hz": f, "seconds": 3, "amp": LEVEL})

    for name, m in [("saw_A3", 57), ("saw_A3p50c", 57.5), ("saw_C3", 48), ("saw_A1", 33)]:
        s[name] = (fade(additive(hz(m), 3, saw_partials())), {"kind": "band-limited saw", "hz": hz(m), "midi": m, "seconds": 3})
    s["square_A3"] = (fade(additive(hz(57), 3, [(k, 1 / k) for k in range(1, 80, 2)])),
                      {"kind": "band-limited square", "hz": hz(57), "midi": 57, "seconds": 3})
    s["bell_inharmonic"] = (fade(additive(hz(57), 3, [(1, 1), (2.76, .7), (5.40, .5), (8.93, .35), (13.34, .25)])),
                            {"kind": "inharmonic partials", "f0": hz(57), "ratios": [1, 2.76, 5.40, 8.93, 13.34]})
    s["chord_Cmaj_sines"] = (fade(sum(sine(hz(m), 3, LEVEL / 3) for m in (60, 64, 67))),
                             {"kind": "sine chord", "midi": [60, 64, 67]})
    s["two_tone_A_Asharp"] = (fade(sine(hz(69), 3, LEVEL / 2) + sine(hz(70), 3, LEVEL / 2)),
                              {"kind": "two sines a semitone apart", "midi": [69, 70]})

    # Pitch moving: vibrato (±100 cents at 5.5 Hz) and a glide (A3 to A4 over 6 s).
    t = np.arange(SR * 4) / SR
    cents = 100 * np.sin(2 * np.pi * 5.5 * t)
    phase = 2 * np.pi * np.cumsum(440 * 2 ** (cents / 1200)) / SR
    s["vibrato_A4"] = (fade(LEVEL * np.sin(phase)), {"kind": "vibrato", "hz": 440, "depth_cents": 100, "rate_hz": 5.5})
    t = np.arange(SR * 6) / SR
    f = 220 * 2 ** (t / 6)
    s["glide_A3_A4"] = (fade(LEVEL * np.sin(2 * np.pi * np.cumsum(f) / SR)), {"kind": "log glide", "from_hz": 220, "to_hz": 440, "seconds": 6})
    s["glide_saw_A2_A4"] = (fade(sum((1 / k) * np.sin(2 * np.pi * k * np.cumsum(110 * 2 ** (2 * t / 6)) / SR)
                                     for k in range(1, 40)) * LEVEL / 2.2),
                            {"kind": "log glide of a saw", "from_hz": 110, "to_hz": 440, "seconds": 6, "partials": 39})

    # Level steps (gate, dynamics), bursts (attack/release, smearing).
    levels = [-60, -48, -36, -24, -12, -6, 0]
    s["level_steps_A4"] = (np.concatenate([fade(sine(440, 0.6, 10 ** (db / 20))) for db in levels]),
                           {"kind": "sine level steps", "hz": 440, "step_s": 0.6, "dbfs": levels})
    s["level_steps_saw_A3"] = (np.concatenate([fade(additive(hz(57), 0.6, saw_partials(), 10 ** (db / 20))) for db in levels]),
                               {"kind": "saw level steps", "hz": hz(57), "step_s": 0.6, "dbfs": levels})
    burst = np.zeros(SR * 3)
    for i in range(6):
        start = int(SR * (0.2 + 0.45 * i))
        burst[start:start + int(SR * 0.05)] = fade(sine(440, 0.05), 1)
    s["bursts_A4_50ms"] = (burst, {"kind": "50 ms sine bursts", "hz": 440, "at_s": [0.2 + 0.45 * i for i in range(6)]})
    return s


def load_real(path, seconds=12.0):
    sr, x = wavfile.read(path)
    if x.dtype == np.int16:
        x = x / 32768.0
    elif x.dtype == np.int32:
        x = x / 2147483648.0
    elif x.dtype == np.uint8:
        x = (x - 128) / 128.0
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    x = x[:, :2] if x.shape[1] >= 2 else np.repeat(x, 2, 1)
    if sr != SR:
        g = math.gcd(sr, SR)
        x = resample_poly(x, SR // g, sr // g, axis=0)
    return fade(x[: int(SR * seconds)].copy(), 10), sr


REAL = {
    # name: (path relative to the samples folder, key of the material)
    "vocal_lead_Am": ("91Vocals - Vocal Hooks Magenta/loops/Vocal_Hooks/Vocal_Hooks_110_BPM_Am/Dry/91V_VM_110_vocal_hook_got_that_lead_dry_Am.wav", "A minor"),
    "vocal_stack_Am": ("91Vocals - Vocal Hooks Magenta/loops/Vocal_Hooks/Vocal_Hooks_110_BPM_Am/Dry/91V_VM_110_vocal_hook_got_that_stack_dry_Am.wav", "A minor"),
    "guitar_Am": ("Cymatics - Crystals Guitar Loops Vol 2/Cymatics - Broken Guitar Loop - 76 BPM A Min.wav", "A minor"),
    "guitar_Dmaj": ("Cymatics - Crystals Guitar Loops Vol 2/Cymatics - Blue Dream Guitar Loop - 76 BPM D Maj.wav", "D major"),
    "piano_Am": ("Cymatics Mystery Black Friday Edition/Cymatics - Black Friday Mystery Pack/Piano Compositions/Cymatics - Anime - 58 BPM A Min.wav", "A minor"),
    "pad_Cmaj": ("Disciple Samples Chime and Ace Aura Melodic Riddim Vol. 1/CHIME_ACEAURA_instruments/CHIME_inst_pad_koshi_aria_Cmaj.wav", "C major"),
    "bass_F": ("Black Octopus Sound - Blackwarp - Dubstep & Riddim Vol. 1/Black_Octopus_Sound_Blackwarp_Dubstep_And_Riddim_Volume_1_(PACK)/_Bass_(One-Shots)/BWDR_Bass_One_Shot_001_F.wav", "F"),
    "drums_loop": ("Cymatics - Atomic Dubstep Sample Pack (WAV)/FX/Cymatics - Buildup Drum Loop Ninja - 140 BPM.wav", "none"),
}


def notes(*names):
    return {f"Note_{n}": 1 for n in names}


SCALES = {
    "none": {},
    "chromatic": notes(*NOTE_NAMES),
    "A": notes("A"),
    "C": notes("C"),
    "C_E_G": notes("C", "E", "G"),
    "C_major": notes("C", "D", "E", "F", "G", "A", "B"),
    "A_minor_pent": notes("A", "C", "D", "E", "G"),
    "D_major": notes("D", "E", "F#", "G", "A", "B", "C#"),
    "whole_tone": notes("C", "D", "E", "F#", "G#", "A#"),
}

# The reference for the parameter sweeps: a C major triad.
REF = SCALES["C_E_G"]


def configs():
    """config name -> (settings dict, signals it runs on). Settings: '=' plain, '~' normalized."""
    c = {}
    every = "all"
    c["bypass"] = ({"Enable": 0}, every)
    for name, notes_on in SCALES.items():
        c[f"scale_{name}"] = (dict(notes_on), every)

    sweep_signals = ["sweep_log", "noise_pink", "steps_fine", "steps_semitones", "saw_A3", "saw_A3p50c",
                     "bursts_A4_50ms", "level_steps_saw_A3", "glide_saw_A2_A4", "vocal_lead_Am", "guitar_Am", "pad_Cmaj"]
    for v in (0, 0.25, 0.75, 1):
        c[f"color_{v}"] = ({**REF, "Color~": v}, sweep_signals)
    for v in (0, 1):
        c[f"wetgain_{v}"] = ({**REF, "Wet_Gain~": v}, sweep_signals)
    for p in ("Pre-Shift", "Post-Shift"):
        for v in (0, 0.25, 0.4, 0.6, 0.75, 1):
            c[f"{p.lower()}_{v}"] = ({**REF, f"{p}~": v}, sweep_signals)
    for v in (0, 0.5, 1):
        c[f"start_{v}"] = ({**REF, "Start_Range~": v}, sweep_signals)
        c[f"end_{v}"] = ({**REF, "End_Range~": v}, sweep_signals)
    c["range_full"] = ({**REF, "Start_Range~": 0, "End_Range~": 1}, sweep_signals)
    c["fft512"] = ({**REF, "FFT_Size": 0}, sweep_signals)
    c["hq"] = ({**REF, "High_Quality": 1}, sweep_signals)
    c["hq_fft512"] = ({**REF, "High_Quality": 1, "FFT_Size": 0}, sweep_signals)
    c["multirate_off"] = ({**REF, "Multirate_Enabled": 0}, sweep_signals)
    c["expand_range"] = ({**REF, "Expand_Range": 1}, sweep_signals)
    c["mute_low"] = ({**REF, "Mute_Low_Band": 1}, sweep_signals)
    c["mute_high"] = ({**REF, "Mute_High_Band": 1}, sweep_signals)
    c["mute_both"] = ({**REF, "Mute_Low_Band": 1, "Mute_High_Band": 1}, sweep_signals)
    for v in (0, 0.25, 0.5, 0.75, 1):
        c[f"gate_{v}"] = ({**REF, "Spectral_Gate_Enabled": 1, "Spectral_Gate_Threshold~": v}, sweep_signals)
    for v in (0, 0.5, 1):
        c[f"morph_{v}"] = ({**REF, "Spectral_Morph_Enabled": 1, "Spectral_Morph_Amount~": v}, sweep_signals)
    c["color_1_hq_fft512"] = ({**REF, "Color~": 1, "High_Quality": 1, "FFT_Size": 0}, sweep_signals)
    # MIDI: notes held through the render instead of the note buttons.
    midi_signals = sweep_signals
    c["midi_off_notes"] = ({**REF, "notes": "60,64,67"}, midi_signals)  # notes given, MIDI disabled
    c["midi_C3_E3_G3"] = ({"MIDI_Enabled": 1, "notes": "60,64,67"}, midi_signals)
    c["midi_C1_E5_G7"] = ({"MIDI_Enabled": 1, "notes": "36,76,103"}, midi_signals)
    c["midi_A3"] = ({"MIDI_Enabled": 1, "notes": "57"}, midi_signals)
    c["midi_none"] = ({"MIDI_Enabled": 1}, midi_signals)
    c["midi_full_range_C3_E3_G3"] = ({"MIDI_Enabled": 1, "Full_Range_MIDI": 1, "notes": "60,64,67"}, midi_signals)
    c["midi_full_range_A3"] = ({"MIDI_Enabled": 1, "Full_Range_MIDI": 1, "notes": "57"}, midi_signals)
    c["midi_plus_buttons"] = ({"MIDI_Enabled": 1, "notes": "57", **notes("C")}, midi_signals)
    return c


def job_args(settings):
    out = []
    for key, value in settings.items():
        if key == "notes":
            out.append(f"notes={value}")
        elif key.endswith("~"):
            out.append(f"{key[:-1]}~{value}")
        else:
            out.append(f"{key}={value}")
    return out


def write_signal(path, x):
    x = np.asarray(x, dtype=np.float32)
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    wavfile.write(path, SR, x)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "chroma-study"))
    ap.add_argument("--samples", default=r"H:\Music\Samples")
    ap.add_argument("--exe", default=str(ROOT / "build-fx" / "bin" / "substation-fxrender.exe"))
    ap.add_argument("--tail", type=float, default=1.0)
    ap.add_argument("--preroll", type=float, default=0.5)
    args = ap.parse_args()

    out = Path(args.out)
    (out / "signals").mkdir(parents=True, exist_ok=True)
    (out / "renders").mkdir(parents=True, exist_ok=True)

    signals = {}
    for name, (x, info) in synthetic_signals().items():
        write_signal(out / "signals" / f"{name}.wav", x)
        signals[name] = {"synthetic": True, **info, "frames": len(x)}
    for name, (rel, key) in REAL.items():
        src = Path(args.samples) / rel
        if not src.exists():
            print(f"missing sample, skipped: {src}", file=sys.stderr)
            continue
        x, source_rate = load_real(src)
        write_signal(out / "signals" / f"{name}.wav", x)
        signals[name] = {"synthetic": False, "source": str(src), "source_rate": source_rate, "key": key,
                         "frames": len(x), "excerpt_s": len(x) / SR}
    (out / "signals.json").write_text(json.dumps(signals, indent=1))

    params = subprocess.run([args.exe, PLUGIN, "params", str(SR)], capture_output=True, text=True, check=True).stdout
    (out / "params.jsonl").write_text("\n".join(l for l in params.splitlines() if "MIDI CC" not in l) + "\n")

    jobs, manifest = [], []
    for config, (settings, which) in configs().items():
        names = list(signals) if which == "all" else [n for n in which if n in signals]
        for signal in names:
            rel_in = f"signals/{signal}.wav"
            rel_out = f"renders/{config}__{signal}.wav"
            jobs.append(" ".join([rel_in, rel_out, f"tail={args.tail}", f"preroll={args.preroll}", *job_args(settings)]))
            manifest.append({"config": config, "signal": signal, "input": rel_in, "output": rel_out,
                             "settings": settings, "tail_s": args.tail, "preroll_s": args.preroll})
    # Determinism check: the first job again at the end (state must not leak between jobs).
    jobs.append(jobs[0].replace(manifest[0]["output"], "renders/_repeat_check.wav"))

    print(f"{len(signals)} signals, {len(jobs)} renders", file=sys.stderr)
    result = subprocess.run([args.exe, PLUGIN, "render", str(SR)], input="\n".join(jobs) + "\n",
                            capture_output=True, text=True, cwd=out)
    answers = [json.loads(l) for l in result.stdout.splitlines() if l.startswith("{")]
    errors = [a for a in answers if "error" in a]
    for entry, answer in zip(manifest, answers):
        entry["result"] = answer
    (out / "manifest.json").write_text(json.dumps({
        "plugin": "Chroma 1.2.0 (Xynth Audio), VST3", "sample_rate": SR, "block": 512, "a4_hz": A4, "fresh_instance_per_render": True,
        "host": "substation-fxrender (tools/fxrender)", "renders": manifest}, indent=1))

    _, a = wavfile.read(out / manifest[0]["output"])
    _, b = wavfile.read(out / "renders" / "_repeat_check.wav")
    same = a.shape == b.shape and float(np.max(np.abs(a - b))) == 0.0
    print(f"done: {len(answers) - len(errors)} rendered, {len(errors)} errors; repeat identical: {same}", file=sys.stderr)
    for e in errors[:20]:
        print(e, file=sys.stderr)
    return 1 if errors or result.returncode else 0


if __name__ == "__main__":
    sys.exit(main())
