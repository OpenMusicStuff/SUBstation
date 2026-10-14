"""The labelled sample libraries the sound tagger is trained and evaluated on.

    python -I corpora.py --data <downloads> --work <work folder>

<downloads> holds the corpora as they come (README.md says where from):

    tdm/machines/<Machine>/<machine>-<kind>/*.wav   tidal-drum-machines: 70 drum machines' one-shots
    nsynth-valid/, nsynth-test/                      NSynth: examples.json and audio/*.wav (16 kHz notes)
    tinysol/                                         TinySOL: orchestral notes, 44.1 kHz (and meta.csv)
    fsops/                                           Freesound One-Shot Percussive Sounds: its audio, its
                                                     sound_info_analysis.json (tags) and licenses.txt (uploaders)
    libri/LibriSpeech/dev-clean/                     LibriSpeech's dev-clean: speech, chopped
    vocalset/                                        VocalSet: sung notes and phrases, chopped
    dirt/<folder>/*.wav                              Dirt-Samples: held out, never trained on

It writes, under <work>:

    lists/<corpus>.txt   the files, a path a line (what sound_features_dump reads)
    labels.json          {item: {"corpus", "group", "kind", "loop", "source", "qualities", "pitch", "words"}}

An item is a file's path, or a part of one: "path<TAB>start<TAB>length" (seconds): the notes cut short and
the vocal chops, analysed as SUBstation analyses an audio clip's part.
    loops/               loops made from the drum machines' hits and NSynth's notes (labelled loops)
    synths/              synth one-shots, basses and drums made by a small synth (Synth)

"group" is what cross-validation keeps together: a drum machine, an NSynth instrument, a TinySOL
instrument, a Freesound uploader, a speaker, a singer, a Dirt-Samples folder. "words" are what people call
a Freesound sound (its tags, read into WORDS). Nothing here runs code from the downloads: it reads WAV files and
JSON/CSV metadata only.
"""

import argparse
import csv
import json
import math
import os
import random
import struct
import sys
import wave
from pathlib import Path

import numpy as np

# The kinds a sound can be (the tagger's classes), in the order the model has them.
DRUM_KINDS = ["kick", "snare", "clap", "closed hat", "open hat", "crash", "ride", "tom", "rim", "cowbell",
              "shaker", "tambourine", "percussion"]
TONAL_KINDS = ["bass", "keys", "organ", "guitar", "mallet", "strings", "brass", "woodwind", "vocal", "synth"]
KINDS = DRUM_KINDS + TONAL_KINDS

# NSynth's qualities, in its order.
QUALITIES = ["bright", "dark", "distortion", "fast_decay", "long_release", "multiphonic", "nonlinear_env",
             "percussive", "reverb", "tempo-synced"]
SOURCES = ["acoustic", "electronic", "synthetic"]

TDM_KINDS = {"bd": "kick", "sd": "snare", "sd2": "snare", "cp": "clap", "hh": "closed hat", "oh": "open hat",
             "cr": "crash", "rd": "ride", "lt": "tom", "mt": "tom", "ht": "tom", "rim": "rim", "cb": "cowbell",
             "sh": "shaker", "tb": "tambourine", "perc": "percussion"}
NSYNTH_KINDS = {"bass": "bass", "keyboard": "keys", "organ": "organ", "guitar": "guitar", "mallet": "mallet",
                "string": "strings", "brass": "brass", "flute": "woodwind", "reed": "woodwind", "vocal": "vocal"}
TINYSOL_KINDS = {"Brass": "brass", "Strings": "strings", "Winds": "woodwind"}

# Dirt-Samples' folders whose names say what they hold (as sound_similarity_bench reads them, and more).
DIRT_KINDS = {
    "kick": ["bd", "808bd", "clubkick", "hardkick", "kicklinn", "popkick", "reverbkick", "bassdm"],  # (bassdm: bass drums)
    "snare": ["sn", "sd", "808sd"],
    "clap": ["cp", "realclaps"],
    "closed hat": ["hh", "hh27", "linnhats"],
    "open hat": ["oh", "ho", "808oh"],
    "crash": ["cr", "808cy"],
    "tom": ["lt", "mt", "ht", "808lt", "808mt", "808ht"],
    "rim": ["rm", "rs"],
    "cowbell": ["cb"],
    "percussion": ["808lc", "808mc", "808hc", "tabla", "tabla2", "hand"],
    "bass": ["bass", "bass0", "bass1", "bass3", "jvbass", "jungbass", "bassfoo"],  # (bass2: hardcore's "bass", kicks)
    "keys": ["superpiano", "casio", "notes", "newnotes"],
    "guitar": ["gtr", "sitar"],
    "brass": ["trump"],
    "woodwind": ["sax"],
    "vocal": ["speech", "diphone", "diphone2", "numbers", "alphabet", "yeah", "speakspell"],
    "synth": ["stab", "pluck", "arpy", "hoover"],
}
DIRT_LOOPS = ["breaks125", "breaks152", "breaks157", "breaks165"]

# What people call sounds, as Freesound's uploaders tag them: each word and the tags that say it. Learned
# from the tags (the tagger's word head), so "punchy", "dubstep" or "acoustic" find what people mean by them.
WORDS = {
    "punchy": ["punchy", "punch"], "hard": ["hard", "harsh", "aggressive", "hit-hard"],
    "soft": ["soft", "gentle", "quiet", "smooth"], "bright": ["bright", "crisp", "sharp", "brilliant"],
    "deep": ["deep", "low", "sub", "boomy", "boom"], "high": ["high", "high-pitched"],
    "tight": ["tight", "short", "snappy", "snap", "staccato"], "fat": ["fat", "thick", "big", "huge", "phat", "massive"],
    "heavy": ["heavy"], "distorted": ["distorted", "distortion", "dirty", "overdrive", "gritty", "crunchy", "crunch",
                                      "saturated", "saturation", "fuzz", "noisy"],
    "reverb": ["reverb", "reverberant", "room", "roomy", "hall", "echo", "wet"], "dry": ["dry"],
    "acoustic": ["acoustic", "real", "realistic", "live", "natural", "organic", "drumset", "drumkit", "kit"],
    "electronic": ["electronic", "electro", "digital", "synth", "synthesizer", "synthesized", "synthetic", "analog",
                   "analogue", "drum-synth", "machine", "drum-machine", "drummachine", "modular"],
    "vintage": ["vintage", "old", "retro", "classic", "old-school", "oldschool"],
    "lofi": ["lo-fi", "lofi", "bitcrush", "bitcrushed", "crushed", "8bit", "8-bit", "chiptune"],
    "processed": ["processed", "layered", "layer", "layers", "fx", "effect", "effects"],
    "metallic": ["metallic", "metalic", "steel", "iron", "clang", "anvil"], "wooden": ["wood", "wooden", "woodblock"],
    "glitch": ["glitch", "glitchy"], "industrial": ["industrial"],
    "house": ["house", "deep-house", "tech-house"], "techno": ["techno", "minimal"], "trance": ["trance", "psytrance"],
    "dubstep": ["dubstep", "brostep", "riddim"], "dnb": ["dnb", "drum-and-bass", "drumandbass", "drum-n-bass", "drumnbass",
                                                        "jungle", "neurofunk", "liquid"],
    "hiphop": ["hip-hop", "hiphop", "rap", "boom-bap", "boombap"], "trap": ["trap"],
    "edm": ["edm", "dance", "electro-house", "bigroom", "big-room", "club"], "rock": ["rock", "punk", "metal-music"],
    "808": ["808", "tr808", "tr-808"], "909": ["909", "tr909", "tr-909"],
}
# A Freesound sound's kind, from its tags and name: when exactly one kind is said.
FS_KINDS = {
    "kick": ["kick", "bassdrum", "kickdrum", "kick-drum", "bass-drum", "bd"], "snare": ["snare", "snaredrum", "snare-drum"],
    "clap": ["clap", "handclap", "claps"], "closed hat": ["closed-hihat", "closed-hat", "closedhat", "chh"],
    "open hat": ["open-hihat", "open-hat", "openhat", "ohh"], "crash": ["crash"], "ride": ["ride"],
    "tom": ["tom", "toms", "floor-tom", "tom-tom", "rack-tom"], "rim": ["rim", "rimshot", "rim-shot", "sidestick"],
    "cowbell": ["cowbell"], "shaker": ["shaker", "maracas"], "tambourine": ["tambourine"],
    "percussion": ["conga", "bongo", "clave", "claves", "timbale", "djembe", "cajon", "agogo", "guiro", "cabasa",
                   "woodblock", "tabla"],
}
HAT_TAGS = ["hihat", "hi-hat", "hat", "hats", "hh"]


def read_wav(path):
    """Mono float samples and the rate of a PCM or float WAV; None if it can't be read."""
    try:
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
            return None
        pos, fmt, raw = 12, None, None
        while pos + 8 <= len(data):
            cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
            body = data[pos + 8:pos + 8 + size]
            if cid == b"fmt ":
                fmt = list(struct.unpack("<HHIIHH", body[:16]))
                if fmt[0] == 0xFFFE and len(body) >= 26:  # WAVE_FORMAT_EXTENSIBLE: its sub-format
                    fmt[0] = struct.unpack("<H", body[24:26])[0]
            elif cid == b"data":
                raw = body
            pos += 8 + size + (size & 1)
        if not fmt or raw is None:
            return None
        tag, channels, rate, _, _, bits = fmt
        if tag == 3 and bits == 32:
            x = np.frombuffer(raw[:len(raw) // 4 * 4], "<f4").astype(np.float64)
        elif bits == 8:
            x = (np.frombuffer(raw, np.uint8).astype(np.float64) - 128) / 128
        elif bits == 16:
            x = np.frombuffer(raw[:len(raw) // 2 * 2], "<i2") / 32768.0
        elif bits == 24:
            b = np.frombuffer(raw[:len(raw) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
            v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
            x = np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)
        elif bits == 32:
            x = np.frombuffer(raw[:len(raw) // 4 * 4], "<i4") / float(1 << 31)
        else:
            return None
        x = x[: len(x) // channels * channels].reshape(-1, channels).mean(axis=1)
        return np.nan_to_num(x), rate
    except Exception:
        return None


def to_rate(x, rate, target=44100):
    if rate == target or len(x) < 2:
        return x
    n = int(round(len(x) * target / rate))
    return np.interp(np.arange(n) * (rate / target), np.arange(len(x)), x)


def write_wav(path, x, rate=44100):
    peak = np.max(np.abs(x)) or 1.0
    pcm = (np.clip(x / peak * 0.9, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def entry(corpus, group, kind=None, loop=False, source=None, qualities=None, pitch=None, words=None):
    return {"corpus": corpus, "group": group, "kind": kind, "loop": loop, "source": source,
            "qualities": qualities, "pitch": pitch, "words": words}


def tdm(data, labels):
    hits = {}  # machine -> kind -> [paths]
    for machine in sorted((data / "tdm" / "machines").iterdir()):
        if not machine.is_dir():
            continue
        for folder in sorted(machine.iterdir()):
            kind = TDM_KINDS.get(folder.name.rsplit("-", 1)[-1])
            if not folder.is_dir() or not kind:
                continue
            for f in sorted(folder.iterdir()):
                if f.suffix.lower() in (".wav", ".wave"):
                    labels[str(f)] = entry("tdm", machine.name, kind)
                    hits.setdefault(machine.name, {}).setdefault(kind, []).append(f)
    return hits


def nsynth(data, labels):
    notes = []
    for split in ("nsynth-valid", "nsynth-test"):
        meta = data / split / "examples.json"
        if not meta.exists():
            continue
        for name, v in json.load(open(meta)).items():
            f = data / split / "audio" / (name + ".wav")
            if not f.exists():
                continue
            labels[str(f)] = entry("nsynth", v["instrument_str"], NSYNTH_KINDS[v["instrument_family_str"]],
                                   source=v["instrument_source_str"], qualities=v["qualities"], pitch=v["pitch"])
            notes.append((f, v))
    return notes


def tinysol(data, labels):
    root = data / "tinysol"
    meta = data / "tinysol" / "meta.csv"
    if not meta.exists():
        return
    for row in csv.DictReader(open(meta, newline="")):
        family = row["Path"].split("/")[0]
        kind = TINYSOL_KINDS.get(family)
        f = root / row["Path"]
        if kind and f.exists():
            labels[str(f)] = entry("tinysol", row["Instrument (abbr.)"], kind, source="acoustic",
                                   pitch=int(row["Pitch ID"]) if row.get("Pitch ID") else None)


def fsops(data, labels):
    root = data / "fsops"
    info = root / "sound_info_analysis.json"
    if not info.exists():
        return
    users = json.load(open(root / "licenses.txt")) if (root / "licenses.txt").exists() else {}
    files = {int(f.stem): f for f in (root / "one_shot_percussive_sounds").glob("*/*.wav")}
    tag_to_word = {t: w for w, tags in WORDS.items() for t in tags}
    seen = set()
    for v in json.load(open(info)):
        f = files.get(v["id"])
        if f is None or v["id"] in seen:
            continue
        seen.add(v["id"])
        # Only what may be used for anything (CC0, CC BY): no non-commercial or Sampling+ sounds.
        license = users.get(str(v["id"]), {}).get("license", "")
        if "by-nc" in license or "sampling" in license or not license:
            continue
        tags = {t.lower() for t in v.get("tags") or []}
        name = v.get("name", "").lower()
        name_words = set(name.replace("_", " ").replace("-", " ").replace(".", " ").split())
        words = sorted({tag_to_word[t] for t in tags if t in tag_to_word})
        said = {k for k, ts in FS_KINDS.items() if tags & set(ts) or name_words & set(ts)}
        if tags & set(HAT_TAGS) or name_words & set(HAT_TAGS) or "hihat" in name:
            opened = "open" in tags or "open" in name_words or "ohh" in name_words
            closed = "closed" in tags or "closed" in name_words or "chh" in name_words
            if opened != closed:
                said.add("open hat" if opened else "closed hat")
            elif not said & {"open hat", "closed hat"}:
                said.add("hat?")
        if "cymbal" in tags and not said & {"crash", "ride"}:
            said.add("cymbal?")
        kind = next(iter(said)) if len(said) == 1 and "?" not in next(iter(said)) else None
        source = ("acoustic" if "acoustic" in words and "electronic" not in words else
                  "synthetic" if "electronic" in words and "acoustic" not in words else None)
        user = users.get(str(v["id"]), {}).get("username", str(v["id"]))
        labels[str(f)] = entry("fsops", user, kind, source=source, words=words)


def excerpts(labels, rng, count=7000):
    """Notes cut short (from their start, 0.08 to 1.5 s): tonal one-shots as sample libraries chop them, so
    that being short says nothing of being a drum."""
    notes = [(p, v) for p, v in labels.items() if v["corpus"] in ("nsynth", "tinysol")]
    for p, v in rng.sample(notes, min(count, len(notes))):
        length = math.exp(rng.uniform(math.log(0.08), math.log(1.5)))
        labels[f"{p}\t0\t{length:.3f}"] = entry("excerpts", v["group"], v["kind"], source=v["source"], pitch=v["pitch"])


def vocals(data, labels, rng, speech=2000, sung=3000):
    """Vocal chops: parts of LibriSpeech's utterances (a syllable to a phrase) and of VocalSet's singing."""
    libri = sorted((data / "libri" / "LibriSpeech" / "dev-clean").glob("*/*/*.flac"))
    for f in rng.sample(libri, min(speech, len(libri))):
        start, length = rng.uniform(0, 4), math.exp(rng.uniform(math.log(0.15), math.log(3)))
        labels[f"{f}\t{start:.3f}\t{length:.3f}"] = entry("vocals", "speaker:" + f.parent.parent.name, "vocal",
                                                            loop=False if length < 1.5 else None, source="acoustic")
    sung_files = sorted(f for f in (data / "vocalset").rglob("*.wav")
                        if "__MACOSX" not in f.parts and not f.name.startswith("._"))
    for f in rng.sample(sung_files, min(sung, len(sung_files))):
        singer = next((x for x in f.parts if x.rstrip("0123456789") in ("female", "male") and x[-1:].isdigit()),
                      f.parent.name)
        start, length = rng.uniform(0, 3), math.exp(rng.uniform(math.log(0.15), math.log(4)))
        labels[f"{f}\t{start:.3f}\t{length:.3f}"] = entry("vocals", "singer:" + singer, "vocal",
                                                            loop=False if length < 1.5 else None, source="acoustic")


def dirt(data, labels):
    root = data / "dirt"
    by_folder = {f: k for k, folders in DIRT_KINDS.items() for f in folders}
    for folder in sorted(root.iterdir()):
        if not folder.is_dir():
            continue
        for f in sorted(folder.iterdir()):
            if f.suffix.lower() not in (".wav", ".wave", ".flac", ".mp3"):
                continue
            loop = folder.name in DIRT_LOOPS or "loop" in f.name.lower()
            labels[str(f)] = entry("dirt", folder.name, None if loop else by_folder.get(folder.name), loop=loop)


class Synth:
    """A small subtractive synth: band-limited oscillators (PolyBLEP saw and pulse, a sine, detuned stacks),
    a resonant low-pass (a state-variable filter) swept by an envelope, an ADSR, drive. What synth one-shots
    and synth basses are made of, so the tagger learns them from sounds made as they are made."""

    RATE = 44100

    def __init__(self, rng):
        self.rng = rng

    @staticmethod
    def _polyblep(t, dt):
        out = np.zeros_like(t)
        a = t < dt
        x = t[a] / dt[a]
        out[a] = x + x - x * x - 1
        b = t > 1 - dt
        x = (t[b] - 1) / dt[b]
        out[b] = x * x + x + x + 1
        return out

    def osc(self, shape, freq):
        """`freq`: Hz per sample (an array: glides)."""
        dt = np.clip(freq / self.RATE, 1e-6, 0.45)
        phase = (np.cumsum(dt) + self.rng.random()) % 1.0
        if shape == "sine":
            return np.sin(2 * np.pi * phase)
        if shape == "tri":
            return 2 * np.abs(2 * phase - 1) - 1
        saw = 2 * phase - 1 - self._polyblep(phase, dt)
        if shape == "saw":
            return saw
        width = 0.5 if shape == "square" else self.rng.uniform(0.1, 0.4)
        phase2 = (phase + width) % 1.0
        saw2 = 2 * phase2 - 1 - self._polyblep(phase2, dt)
        return saw - saw2

    def lowpass(self, x, cutoff, resonance):
        return self.svf(x, cutoff, resonance, "low")

    def svf(self, x, cutoff, resonance, mode):
        """A state-variable filter (Chamberlin's), its cutoff (Hz) per sample (or one): stable to a fifth of the
        rate with some damping, so it is held there. `mode`: "low", "band" or "high"."""
        cutoff = np.broadcast_to(np.asarray(cutoff, dtype=float), x.shape)
        f = 2 * np.sin(np.pi * np.clip(cutoff, 20, 0.2 * self.RATE) / self.RATE)
        q = 1 - min(resonance, 0.7)
        low = band = 0.0
        out = np.empty_like(x)
        for i in range(len(x)):  # (short sounds: fast enough)
            low += f[i] * band
            high = x[i] - low - q * band
            band += f[i] * high
            out[i] = low if mode == "low" else band if mode == "band" else high
        return out

    def noise(self, n):
        return self.rng.uniform(-1, 1, n)

    def drum(self, kind):
        """A drum as drum machines (and producers) synthesize them."""
        r = self.rng
        R = self.RATE
        t = lambda seconds: np.arange(int(R * seconds)) / R  # noqa: E731
        decay = lambda x, tau: x * np.exp(-np.arange(len(x)) / (R * tau))  # noqa: E731
        if kind in ("kick", "tom"):
            lo, hi = ((35, 70), (90, 320)) if kind == "kick" else ((80, 220), (150, 450))
            end, start = r.uniform(*lo), r.uniform(*hi)
            tt = t(r.uniform(0.25, 1.2) if kind == "kick" else r.uniform(0.3, 0.9))
            freq = end + (start - end) * np.exp(-tt / r.uniform(0.008, 0.06))
            x = np.sin(2 * np.pi * np.cumsum(freq) / R) * np.exp(-tt / r.uniform(0.08, 0.6))
            if r.random() < 0.6:  # a click
                click = decay(self.noise(len(tt)), r.uniform(0.001, 0.006)) * r.uniform(0.1, 0.6)
                x = x + self.svf(click, r.uniform(2000, 8000), 0.2, "band")
            if kind == "tom" and r.random() < 0.5:
                x = x + decay(self.noise(len(tt)), 0.05) * 0.05
        elif kind == "snare":
            tt = t(r.uniform(0.2, 0.6))
            hz = r.uniform(150, 280)
            tone = (np.sin(2 * np.pi * hz * tt) + 0.5 * np.sin(2 * np.pi * hz * 1.6 * tt)) * np.exp(-tt / r.uniform(0.02, 0.09))
            body = self.svf(self.noise(len(tt)), r.uniform(1500, 6000), 0.3, r.choice(["band", "high", "low"]))
            body = body * np.exp(-tt / r.uniform(0.06, 0.25))
            mix = r.uniform(0.4, 0.9)
            x = (1 - mix) * tone + mix * body / (np.max(np.abs(body)) + 1e-9)
        elif kind in ("closed hat", "open hat"):
            tt = t(r.uniform(0.15, 0.3) if kind == "closed hat" else r.uniform(0.6, 1.5))
            if r.random() < 0.5:  # the 808's six square waves
                base = r.uniform(180, 420)
                src = sum(np.sign(np.sin(2 * np.pi * base * m * tt)) for m in (1, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637))
            else:
                src = self.noise(len(tt))
            x = self.svf(src, r.uniform(6000, 8800), 0.3, "high")
            x = x * np.exp(-tt / (r.uniform(0.01, 0.06) if kind == "closed hat" else r.uniform(0.15, 0.6)))
        elif kind == "clap":
            tt = t(r.uniform(0.3, 0.7))
            env = np.zeros(len(tt))
            gap = r.uniform(0.007, 0.016)
            for k in range(r.choice([3, 4])):
                at = int(k * gap * R)
                env[at:] += np.exp(-np.arange(len(tt) - at) / (R * 0.003)) * (1 if k else 0.8)
            env += np.exp(-tt / r.uniform(0.06, 0.25)) * (tt > 3 * gap)
            x = self.svf(self.noise(len(tt)), r.uniform(900, 2200), 0.5, "band") * env
        elif kind == "rim":
            tt = t(r.uniform(0.08, 0.2))
            hz = r.uniform(400, 1800)
            x = np.sin(2 * np.pi * hz * tt) * np.exp(-tt / r.uniform(0.008, 0.03))
            x = x + decay(self.noise(len(tt)), 0.002) * 0.5
        else:  # cowbell
            tt = t(r.uniform(0.2, 0.7))
            base = r.uniform(450, 650)
            x = np.sign(np.sin(2 * np.pi * base * tt)) + np.sign(np.sin(2 * np.pi * base * 1.48 * tt))
            x = self.svf(x, r.uniform(700, 1500), 0.5, "band") * np.exp(-tt / r.uniform(0.05, 0.25))
        if r.random() < 0.3:  # drive
            drive = r.uniform(1.5, 6)
            x = np.tanh(drive * x / (np.max(np.abs(x)) + 1e-9)) / np.tanh(drive)
        return x

    def adsr(self, n, attack, decay, sustain, release, held):
        t = np.arange(n) / self.RATE
        env = np.where(t < attack, t / max(attack, 1e-4), sustain + (1 - sustain) * np.exp(-(t - attack) / max(decay, 1e-3)))
        after = t > held
        env[after] *= np.exp(-(t[after] - held) / max(release, 1e-3))
        return env

    def voice(self, kind, midi):
        r = self.rng
        n = int(self.RATE * {"pluck": r.uniform(0.25, 1.2), "stab": r.uniform(0.2, 0.9), "lead": r.uniform(0.6, 2.5),
                             "pad": r.uniform(1.5, 4.0), "arp": r.uniform(0.1, 0.35), "sub": r.uniform(0.3, 2.0),
                             "808": r.uniform(0.5, 2.5), "reese": r.uniform(0.5, 2.5), "square bass": r.uniform(0.2, 1.2),
                             "saw bass": r.uniform(0.2, 1.2)}[kind])
        hz = 440.0 * 2 ** ((midi - 69) / 12)
        freq = np.full(n, hz)
        if kind == "808":  # a short glide down to the note
            freq *= 1 + r.uniform(0.0, 0.6) * np.exp(-np.arange(n) / (self.RATE * r.uniform(0.01, 0.06)))
        if kind == "lead" and r.random() < 0.6:  # vibrato
            freq *= 2 ** (r.uniform(0.1, 0.4) / 12 * np.sin(2 * np.pi * r.uniform(4, 7) * np.arange(n) / self.RATE))
        shapes = {"sub": ["sine"], "808": ["sine", "tri"], "reese": ["saw"], "square bass": ["square", "pulse"],
                  "saw bass": ["saw"]}.get(kind, ["saw", "square", "pulse", "tri", "saw"])
        shape = r.choice(shapes)
        stack = 1 if kind in ("sub", "808", "square bass") else (r.choice([2, 3, 5, 7]) if kind in ("pad", "reese", "stab") else r.choice([1, 1, 2, 3]))
        detune = r.uniform(0.05, 0.35)
        x = sum(self.osc(shape, freq * 2 ** (detune * (k - (stack - 1) / 2) / max(stack - 1, 1) / 12)) for k in range(stack)) / stack
        if kind == "stab" and r.random() < 0.7:  # a chord
            chords = [(4, 7), (3, 7), (3, 7, 10), (4, 7, 11), (5, 7)]
            for interval in chords[r.integers(len(chords))]:
                x = x + self.osc(shape, freq * 2 ** (interval / 12)) * 0.8
        env_t = np.arange(n) / self.RATE
        base = hz * r.uniform(1.5, 6)
        sweep = hz * r.uniform(4, 40) if kind in ("pluck", "stab", "arp", "saw bass", "square bass") else hz * r.uniform(0, 8)
        cutoff = base + sweep * np.exp(-env_t / r.uniform(0.03, 0.4))
        if kind not in ("sub", "808"):
            x = self.lowpass(x, cutoff, r.uniform(0.0, 0.8))
        attack = {"pad": r.uniform(0.2, 1.0), "lead": r.uniform(0.002, 0.08)}.get(kind, r.uniform(0.001, 0.01))
        decay = {"pluck": r.uniform(0.08, 0.4), "arp": r.uniform(0.05, 0.15), "stab": r.uniform(0.1, 0.3)}.get(kind, r.uniform(0.2, 1.0))
        sustain = {"pluck": 0.0, "arp": 0.0, "stab": r.uniform(0, 0.4)}.get(kind, r.uniform(0.5, 1.0))
        held = n / self.RATE * r.uniform(0.5, 0.85)
        x = x * self.adsr(n, attack, decay, sustain, r.uniform(0.05, 0.5), held)
        if r.random() < 0.35:  # drive
            drive = r.uniform(1.5, 8)
            x = np.tanh(drive * x) / np.tanh(drive)
        return x


def synth_drums(out, labels, rng, count=1600):
    """Drums synthesized as drum machines (and producers) make them: sine-sweep kicks (with a click or without:
    an 808's has none), tone-and-noise snares, noise and square-wave hats, burst claps, toms, rims, cowbells."""
    out.mkdir(parents=True, exist_ok=True)
    synth = Synth(np.random.default_rng(rng.randrange(1 << 30)))
    kinds = ["kick", "snare", "closed hat", "open hat", "clap", "tom", "rim", "cowbell"]
    for i in range(count):
        kind = kinds[i % len(kinds)]
        x = synth.drum(kind)
        if not np.all(np.isfinite(x)) or np.max(np.abs(x)) == 0:
            continue
        f = out / f"{kind.replace(' ', '')}_{i:04d}.wav"
        write_wav(f, x)
        labels[str(f)] = entry("synths", f"drum:{kind}:{i // len(kinds) % 10}", kind, source="synthetic")


def synth_sounds(out, labels, rng, count=2400):
    """Synth one-shots (plucks, stabs, leads, pads, arp notes: the synth kind) and synth basses (sub, 808, reese,
    square, saw: bass), each patch family a group of its own."""
    out.mkdir(parents=True, exist_ok=True)
    synth = Synth(np.random.default_rng(rng.randrange(1 << 30)))
    kinds = {"pluck": "synth", "stab": "synth", "lead": "synth", "pad": "synth", "arp": "synth",
             "sub": "bass", "808": "bass", "reese": "bass", "square bass": "bass", "saw bass": "bass"}
    names = list(kinds)
    for i in range(count):
        kind = names[i % len(names)]
        midi = rng.randint(24, 50) if kinds[kind] == "bass" else rng.randint(45, 84)
        x = synth.voice(kind, midi)
        if not np.all(np.isfinite(x)) or np.max(np.abs(x)) == 0:
            continue
        f = out / f"{kind.replace(' ', '')}_{i:04d}.wav"
        write_wav(f, x)
        # (Families: the patch's kind and a tenth of the seeds, so a fold holds out whole families.)
        labels[str(f)] = entry("synths", f"{kind}:{i // len(names) % 10}", kinds[kind], source="synthetic", pitch=midi)


def make_loops(hits, notes, out, labels, rng, drum_loops=1200, tonal_loops=500):
    """Drum loops from one machine's hits (kick, snare, hats, now and then more), 70-180 BPM, one to four bars,
    and tonal loops (arpeggios, basslines) from one NSynth instrument's notes: labelled loops, as many groups
    as their sources'."""
    out.mkdir(parents=True, exist_ok=True)
    cache = {}

    def load(path):
        if path not in cache:
            r = read_wav(path)
            cache[path] = None if r is None else to_rate(*r)
        return cache[path]

    machines = [m for m, kinds in hits.items() if "kick" in kinds and ("snare" in kinds or "clap" in kinds)]
    for i in range(drum_loops):
        m = rng.choice(machines)
        kinds = hits[m]
        bpm = rng.uniform(70, 180)
        bars = rng.choice([1, 1, 2, 2, 4])
        step = 60.0 / bpm / 4  # sixteenths
        steps = 16 * bars
        n = int((steps * step + 0.5) * 44100)
        x = np.zeros(n)
        voices = {"kick": [0, 10] if rng.random() < .5 else [0, 8], "snare": [4, 12]}
        if "snare" not in kinds:
            voices["clap"] = voices.pop("snare")
        hat = "closed hat" if "closed hat" in kinds else None
        if hat:
            voices[hat] = list(range(0, 16, rng.choice([1, 2, 2, 4])))
        for extra in ("open hat", "tom", "rim", "shaker", "cowbell", "percussion", "clap", "ride"):
            if extra in kinds and extra not in voices and rng.random() < 0.25:
                voices[extra] = sorted(rng.sample(range(16), rng.randint(1, 4)))
        if rng.random() < 0.4:
            voices["kick"] = sorted(set(voices["kick"] + rng.sample(range(16), 2)))
        chosen = {k: load(rng.choice(kinds[k])) for k in voices}
        for k, pattern in voices.items():
            s = chosen[k]
            if s is None:
                continue
            for b in range(bars):
                for p in pattern:
                    at = int(((b * 16 + p) * step + rng.gauss(0, 0.003) * (rng.random() < .5)) * 44100)
                    at = max(at, 0)
                    seg = s[: n - at] * rng.uniform(0.6, 1.0)
                    x[at:at + len(seg)] += seg
        if np.max(np.abs(x)) == 0:
            continue
        f = out / f"drumloop_{i:04d}.wav"
        write_wav(f, x)
        labels[str(f)] = entry("loops", "tdm:" + m, None, loop=True)

    by_instrument = {}
    for f, v in notes:
        by_instrument.setdefault(v["instrument_str"], []).append((f, v))
    instruments = sorted(by_instrument)
    for i in range(tonal_loops):
        name = rng.choice(instruments)
        pool = by_instrument[name]
        if len(pool) < 3:
            continue
        bpm = rng.uniform(70, 180)
        step = 60.0 / bpm / rng.choice([2, 4])
        count = rng.choice([8, 16])
        n = int((count * step + 0.5) * 44100)
        x = np.zeros(n)
        picks = rng.sample(pool, min(len(pool), 4))
        for k in range(count):
            f, _ = rng.choice(picks)
            s = load(f)
            if s is None:
                continue
            at = int(k * step * 44100)
            seg = s[: min(len(s), int(step * 44100 * rng.uniform(0.5, 1.0)), n - at)].copy()
            fade = min(len(seg), 441)
            if fade:
                seg[-fade:] *= np.linspace(1, 0, fade)
            x[at:at + len(seg)] += seg
        if np.max(np.abs(x)) == 0:
            continue
        f = out / f"tonalloop_{i:04d}.wav"
        write_wav(f, x)
        labels[str(f)] = entry("loops", "nsynth:" + name, None, loop=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--work", required=True, type=Path)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    labels = {}
    hits = tdm(args.data, labels)
    notes = nsynth(args.data, labels)
    tinysol(args.data, labels)
    make_loops(hits, notes, args.work / "loops", labels, rng)
    fsops(args.data, labels)
    excerpts(labels, rng)
    vocals(args.data, labels, rng)
    synth_sounds(args.work / "synths", labels, rng)
    synth_drums(args.work / "synths", labels, rng)
    dirt(args.data, labels)
    (args.work / "lists").mkdir(parents=True, exist_ok=True)
    corpora = sorted({v["corpus"] for v in labels.values()})
    for c in corpora:
        paths = [p for p, v in labels.items() if v["corpus"] == c]
        (args.work / "lists" / f"{c}.txt").write_text("\n".join(paths) + "\n")
        print(c, len(paths), file=sys.stderr)
    json.dump(labels, open(args.work / "labels.json", "w"))


if __name__ == "__main__":
    main()
