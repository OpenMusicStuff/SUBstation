"""The data progression ideas are made from, compiled into SUBstation (intelligence/src/harmony/ProgressionData.inc).

    python -I build_tables.py --chords <free-midi-chords>/chords.py --billboard <McGill-Billboard> \
        [--out ../../intelligence/src/harmony/ProgressionData.inc]

- The moods' progressions: free-midi-chords' progressions in Roman numerals, each tagged with moods (chords.py:
  prog_maj, prog_min and prog_modal), those tagged Sad, Dark or Nostalgic. chords.py is read as text (Python's
  ast, its lists as literals): nothing of it runs.
- How chords follow each other: the McGill Billboard annotations (<McGill-Billboard>/*/salami_chords.txt), every
  change from one chord to the next counted by where the two are in the key (semitones above its tonic) and what
  kind of chord each is (major, minor, diminished, augmented, sus2, sus4), apart for stretches of songs in major
  keys and in minor ones.

README.md says where both come from, their licences, and what is taken from each.
"""

import argparse
import ast
import pathlib
import re

# Quality's order in Chords.h.
QUALITIES = ["Major", "Minor", "Diminished", "Augmented", "Sus2", "Sus4",
             "Dominant7", "Major7", "Minor7", "HalfDiminished7", "Diminished7"]
INTERVALS = {
    "Major": (0, 4, 7), "Minor": (0, 3, 7), "Diminished": (0, 3, 6), "Augmented": (0, 4, 8),
    "Sus2": (0, 2, 7), "Sus4": (0, 5, 7), "Dominant7": (0, 4, 7, 10), "Major7": (0, 4, 7, 11),
    "Minor7": (0, 3, 7, 10), "HalfDiminished7": (0, 3, 6, 10), "Diminished7": (0, 3, 6, 9),
}
# A chord's kind: the triad it is (a seventh is its triad's), the first six qualities.
KIND = {"Major": 0, "Minor": 1, "Diminished": 2, "Augmented": 3, "Sus2": 4, "Sus4": 5,
        "Dominant7": 0, "Major7": 0, "Minor7": 1, "HalfDiminished7": 2, "Diminished7": 2}
KINDS = 6
STATES = 12 * KINDS  # a chord in a key: semitones above the tonic × its kind

MOODS = ["Sad", "Dark", "Nostalgic"]  # Mood's order in Progressions.h: bit 1 << index

# --- free-midi-chords --------------------------------------------------------------------------------------------

MAJOR_SCALE = (0, 2, 4, 5, 7, 9, 11)
MINOR_SCALE = (0, 2, 3, 5, 7, 8, 10)  # natural minor
NUMERALS = {"I": 0, "II": 1, "III": 2, "IV": 3, "V": 4, "VI": 5, "VII": 6}

# A numeral's suffix as the progressions are written for mingus (which free-midi-chords turns them into MIDI with):
# the chord on the degree's root; "" is the key's own triad there, "7" its own seventh. Chords SUBstation doesn't
# name are read as the nearest it does: a sixth, an added ninth or eleventh as its triad, a ninth as its seventh,
# a power chord ("5") as the key's triad.
SUFFIXES = {
    "": "triad", "7": "seventh", "5": "triad",
    "M": "Major", "m": "Minor", "dim": "Diminished", "aug": "Augmented", "+": "Augmented",
    "sus2": "Sus2", "sus4": "Sus4", "sus": "Sus4",
    "M7": "Major7", "maj7": "Major7", "m7": "Minor7", "dom7": "Dominant7", "9": "Dominant7",
    "M9": "Major7", "maj9": "Major7", "m9": "Minor7", "m7b5": "HalfDiminished7", "m7-5": "HalfDiminished7",
    "dim7": "Diminished7", "6": "Major", "69": "Major", "add9": "Major", "2": "Major", "add11": "Major",
    "m6": "Minor", "madd9": "Minor", "m69": "Minor",
}


def scale_chord(scale, degree, size):
    """The quality of the chord of 3 or 4 notes the scale stacks on a degree."""
    steps = [scale[(degree + k) % 7] + 12 * ((degree + k) // 7) for k in (0, 2, 4, 6)]
    intervals = tuple(s - steps[0] for s in steps[:size])
    for name, iv in INTERVALS.items():
        if iv == intervals:
            return name
    raise ValueError(f"no quality for {intervals}")


def parse_numeral(text):
    """mingus's reading: accidentals and the numeral's letters (their case ignored), then the suffix."""
    acc, roman, i = 0, "", 0
    for c in text:
        if c == "#":
            acc += 1
        elif c == "b":
            acc -= 1
        elif c.upper() in "IV":
            roman += c.upper()
        else:
            break
        i += 1
    if roman not in NUMERALS:
        raise ValueError(f"not a numeral: {text!r}")
    return NUMERALS[roman], acc, text[i:]


def parse_chord(text, scale):
    """(semitones above the tonic, quality) of a chord of a progression. A slash's bass is dropped: the voice
    leading picks the inversions."""
    text = text.split("/", 1)[0]
    degree, acc, suffix = parse_numeral(text)
    if suffix not in SUFFIXES:
        raise ValueError(f"unknown suffix {suffix!r} in {text!r}")
    quality = SUFFIXES[suffix]
    if quality == "triad":
        quality = scale_chord(scale, degree, 3)
    elif quality == "seventh":
        quality = scale_chord(scale, degree, 4)
    return (scale[degree] + acc) % 12, quality


def read_progressions(path):
    """The lists prog_maj, prog_min and prog_modal of chords.py, without running it."""
    tree = ast.parse(pathlib.Path(path).read_text(encoding="utf-8"))
    lists = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            if node.targets[0].id in ("prog_maj", "prog_min", "prog_modal"):
                lists[node.targets[0].id] = ast.literal_eval(node.value)
    missing = {"prog_maj", "prog_min", "prog_modal"} - lists.keys()
    if missing:
        raise SystemExit(f"{path}: no {', '.join(sorted(missing))}")
    return lists


def mood_seeds(path):
    """The progressions tagged with one of MOODS, each (numerals, tags, minor, [(offset, quality)], moods' bits),
    major ones first, then by their numerals."""
    seeds = []
    for list_name, entries in read_progressions(path).items():
        for entry in entries:
            numerals, _, tags = entry.partition(" =")
            words = [w for w in tags.split() if w != "New"]  # ("New": recently added)
            bits = sum(1 << i for i, mood in enumerate(MOODS) if mood in words)
            if not bits:
                continue
            minor = list_name == "prog_min"
            chords = [parse_chord(c, MINOR_SCALE if minor else MAJOR_SCALE) for c in numerals.split()]
            if list_name == "prog_modal":
                # Written over the major scale; the tonic's own chord says the mode ("im bIIIM bVIIM IV": minor).
                tonic = next((q for root, q in chords if root == 0), "Major")
                minor = KIND[tonic] in (1, 2)
            seeds.append((numerals, " ".join(words), minor, chords, bits))
    seeds.sort(key=lambda s: (s[2], s[0]))
    return seeds


# --- McGill Billboard ----------------------------------------------------------------------------------------------

NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
# Harte's qualities (the part before the bass and any added notes) by the kind of chord they are. Power chords
# ("5"), bare notes ("1") and lists of notes say no kind: they cut a run.
HARTE_KIND = {
    "maj": 0, "maj6": 0, "maj7": 0, "maj9": 0, "maj11": 0, "maj13": 0, "7": 0, "9": 0, "11": 0, "13": 0,
    "min": 1, "min6": 1, "min7": 1, "min9": 1, "min11": 1, "min13": 1, "minmaj7": 1,
    "dim": 2, "dim7": 2, "hdim7": 2,
    "aug": 3,
    "sus2": 4,
    "sus4": 5,
}


def pitch(name):
    pc = NOTE[name[0]]
    for c in name[1:]:
        pc += 1 if c == "#" else -1
    return pc % 12


def harte(token):
    """(root pitch class, kind) of a chord like "A:min7/b3" or "Bb:sus4(b7,9)"; None when it says no kind."""
    root, _, rest = token.partition(":")
    if not re.fullmatch(r"[A-G][b#]*", root):
        raise ValueError(f"not a chord: {token!r}")
    kind = HARTE_KIND.get(rest.split("/")[0].split("(")[0])
    return None if kind is None else (pitch(root), kind)


def song_runs(path):
    """A song's runs of chords, each (tonic, [(root, kind), ...]) with a chord held over bars once: cut where the
    key changes, at a pause or where no chord plays, and at a chord with no kind. A line's "xN" plays its bars N
    times."""
    tonic, run, runs = None, [], []

    def cut():
        nonlocal run
        if tonic is not None and run:
            runs.append((tonic, run))
        run = []

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("#"):
            m = re.match(r"#\s*tonic:\s*([A-G][b#]*)", line)
            if m:
                cut()
                tonic = pitch(m.group(1))
            continue
        bars = re.findall(r"\|([^|]*)(?=\|)", line)
        repeat = re.search(r"\|\s*x(\d+)", line)
        for _ in range(int(repeat.group(1)) if repeat else 1):
            for bar in bars:
                for token in bar.split():
                    if token.startswith("(") or token == ".":  # a bar's metre ("(3/4)"); the chord before, again
                        continue
                    chord = None if token in ("N", "X", "&pause", "*") else harte(token)
                    if chord is None:
                        cut()
                    elif not run or run[-1] != chord:
                        run.append(chord)
    cut()
    return runs


def mode_of(tonic, runs):
    """0 (major) or 1 (minor): whether the tonic's own chord is more often major or minor; None if neither."""
    major = sum(1 for run in runs for root, kind in run if root == tonic and kind == 0)
    minor = sum(1 for run in runs for root, kind in run if root == tonic and kind == 1)
    return None if major == minor else int(minor > major)


def billboard_tables(folder):
    """Per mode (0 major, 1 minor): how often each chord changes to each other ([from * STATES + to]) and how often
    each is played ([state]); and what was counted."""
    files = sorted(pathlib.Path(folder).glob("*/salami_chords.txt"))
    if not files:
        raise SystemExit(f"{folder}: no */salami_chords.txt")
    transitions = [[0] * (STATES * STATES) for _ in range(2)]
    counts = [[0] * STATES for _ in range(2)]
    stats = {"songs": len(files), "keys": [0, 0], "changes": [0, 0], "unclear": 0}
    for path in files:
        # A song's stretches in one key: its runs one after another with the same tonic.
        stretches = []
        for tonic, run in song_runs(path):
            if stretches and stretches[-1][0] == tonic:
                stretches[-1][1].append(run)
            else:
                stretches.append((tonic, [run]))
        for tonic, runs in stretches:
            mode = mode_of(tonic, runs)
            if mode is None:
                stats["unclear"] += 1
                continue
            stats["keys"][mode] += 1
            for run in runs:
                states = [((root - tonic) % 12) * KINDS + kind for root, kind in run]
                for s in states:
                    counts[mode][s] += 1
                for a, b in zip(states, states[1:]):
                    transitions[mode][a * STATES + b] += 1
                    stats["changes"][mode] += 1
    return transitions, counts, stats


# --- Writing it ----------------------------------------------------------------------------------------------------


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def ints(values, per_line=24):
    rows = [",".join(str(v) for v in values[i:i + per_line]) for i in range(0, len(values), per_line)]
    return "{\n    " + ",\n    ".join(rows) + "}"


def write_inc(path, seeds, transitions, counts, stats):
    chords = [c for seed in seeds for c in seed[3]]
    starts = [0]
    for seed in seeds:
        starts.append(starts[-1] + len(seed[3]))
    out = [
        "// Generated by tools/progressions/build_tables.py: do not edit. What progression ideas are made from",
        "// (Progressions.h).",
        "//",
        "// The moods' progressions: free-midi-chords' (https://github.com/ldrolez/free-midi-chords, chords.py),",
        "// Copyright (c) 2019-2026 Ludovic Drolez, MIT licence (intelligence/third_party/free-midi-chords/LICENSE),",
        f"// those tagged {', '.join(MOODS)}: {len(seeds)} of them.",
        "// How chords follow each other: counted from the McGill Billboard Project's chord annotations (Burgoyne,",
        "// Wild and Fujinaga, \"An Expert Ground Truth Set for Audio Chord Recognition and Music Analysis\", ISMIR",
        f"// 2011; CC0): {stats['songs']} annotations, {stats['keys'][0]} stretches in major keys "
        f"({stats['changes'][0]} chord changes) and {stats['keys'][1]} in minor ones ({stats['changes'][1]}); "
        f"{stats['unclear']} whose mode",
        "// couldn't be told were left out.",
        "#pragma once",
        "namespace sub::intelligence::harmony::data {",
        "",
        "// The moods' progressions, each a chord a bar: its numerals and moods as free-midi-chords writes them,",
        "// whether it is in a minor key, the moods it is tagged with (1 << Mood), and where its chords are in",
        "// kSeedChords (from kSeedStart[i] to kSeedStart[i + 1]): each (semitones above the tonic, Quality).",
        f"inline constexpr int kSeedCount = {len(seeds)};",
        f"inline constexpr const char* kSeedNumerals[kSeedCount] = {{{','.join(c_string(s[0]) for s in seeds)}}};",
        f"inline constexpr const char* kSeedTags[kSeedCount] = {{{','.join(c_string(s[1]) for s in seeds)}}};",
        f"inline constexpr bool kSeedMinor[kSeedCount] = {{{','.join('true' if s[2] else 'false' for s in seeds)}}};",
        f"inline constexpr int kSeedMoods[kSeedCount] = {{{','.join(str(s[4]) for s in seeds)}}};",
        f"inline constexpr int kSeedStart[kSeedCount + 1] = {{{','.join(str(s) for s in starts)}}};",
        f"inline constexpr int kSeedChords[{2 * len(chords)}] = "
        + ints([v for root, quality in chords for v in (root, QUALITIES.index(quality))]) + ";",
        "",
        "// Billboard's chords in major keys ([0]) and minor ones ([1]), each a state: (semitones above the tonic)",
        "// * kKinds + its kind (the first six Qualities; a seventh counts as its triad). kTransitions[mode][a *",
        "// kStates + b]: how often chord a changed to chord b; kCounts[mode][s]: how often chord s was played",
        "// (a chord held over bars once).",
        f"inline constexpr int kKinds = {KINDS};",
        f"inline constexpr int kStates = {STATES};",
        "inline constexpr int kTransitions[2][kStates * kStates] = {"
        + ",".join(ints(t) for t in transitions) + "};",
        "inline constexpr int kCounts[2][kStates] = {" + ",".join(ints(c) for c in counts) + "};",
        "",
        "}  // namespace sub::intelligence::harmony::data",
        "",
    ]
    pathlib.Path(path).write_text("\n".join(out), encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--chords", required=True, help="free-midi-chords' chords.py")
    parser.add_argument("--billboard", required=True, help="the folder of the Billboard annotations' numbered folders")
    here = pathlib.Path(__file__).resolve().parent
    parser.add_argument("--out", default=here.parent.parent / "intelligence/src/harmony/ProgressionData.inc")
    args = parser.parse_args()

    seeds = mood_seeds(args.chords)
    transitions, counts, stats = billboard_tables(args.billboard)
    write_inc(args.out, seeds, transitions, counts, stats)
    per_mood = {m: sum(1 for s in seeds if s[4] & (1 << i)) for i, m in enumerate(MOODS)}
    print(f"{len(seeds)} progressions ({', '.join(f'{m} {n}' for m, n in per_mood.items())}); Billboard: "
          f"{stats['songs']} annotations, {stats['keys'][0]} major and {stats['keys'][1]} minor stretches, "
          f"{stats['changes'][0]} and {stats['changes'][1]} changes, {stats['unclear']} unclear")


if __name__ == "__main__":
    main()
