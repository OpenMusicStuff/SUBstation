# The progression ideas' data

How [intelligence/src/harmony/ProgressionData.inc](../../intelligence/src/harmony/ProgressionData.inc), compiled into
SUBstation, is made: the moods' progressions and how chords follow each other, which the piano roll's progression
ideas are made from. How the ideas are made from them: [docs/intelligence.md](../../docs/intelligence.md#progression-ideas).

Nothing here is part of the build. It needs Python 3.10 or newer, and nothing else.

## The sources

Downloaded by hand:

| What | Where from | Licence | Taken |
|---|---|---|---|
| free-midi-chords's `chords.py`: 194 chord progressions in Roman numerals, each tagged with moods | `https://raw.githubusercontent.com/ldrolez/free-midi-chords/95533fae2c1a2aae032211b9abed0441ac8ba298/chords.py` (SHA-256 `18b003160cea0f06435b2a9cb8546aa0953ce56ae174de4f9170632d2851fd7b`) | MIT (its [LICENSE](../../intelligence/third_party/free-midi-chords/LICENSE), shipped with every build) | the 80 progressions tagged Sad, Dark or Nostalgic: their numerals, tags and chords |
| The McGill Billboard Project's chord annotations, `billboard-2.0-salami_chords.tar.gz`: 890 annotations of 740 songs from the Billboard charts, 1958 to 1991 | `https://www.dropbox.com/s/2lvny9ves8kns4o/billboard-2.0-salami_chords.tar.gz?dl=1` (as [mirdata](https://mirdata.readthedocs.io) has it; MD5 `6954a6fad962a111e69c9c80cb87d3a5`), unpacked | CC0 (public domain); cite Burgoyne, Wild and Fujinaga, "An Expert Ground Truth Set for Audio Chord Recognition and Music Analysis", ISMIR 2011 | how often each chord changes to each other, by where they are in the key, in major keys and in minor ones: counts, not songs |

## Making the table

```sh
python -I tools/progressions/build_tables.py --chords <downloads>/chords.py \
    --billboard <downloads>/McGill-Billboard --out intelligence/src/harmony/ProgressionData.inc
```

It prints what it took: `80 progressions (Sad 13, Dark 15, Nostalgic 59); Billboard: 890 annotations, 846 major and
163 minor stretches, 64634 and 12714 changes, 10 unclear`.

- **The progressions** ([build_tables.py](build_tables.py) `mood_seeds`): `chords.py` is read with Python's `ast`, its
  lists as literals; nothing of it runs. Its numerals are read as free-midi-chords' MIDI is made from them (by
  mingus): the numeral is a degree of the key's scale whatever its case (the major scale for `prog_maj` and
  `prog_modal`, the natural minor for `prog_min`), a suffix makes the chord on that degree's root (`M` major, `m`
  minor, `dim`, `sus4`, `M7`...; none: the key's own triad there, `7`: its own seventh), and a `b` or `#` before it
  moves the whole chord. Chords SUBstation doesn't name become the nearest it does (a sixth or an added ninth its
  triad, a ninth its seventh, a power chord the key's triad); a slash's bass is dropped. A modal progression is in a
  minor key if its tonic's own chord is minor (`im bIIIM bVIIM IV`). Kept: each one's numerals and tags, whether it
  is minor, its chords (semitones above the tonic, quality) and the moods it has of the three.
- **The transitions** (`billboard_tables`): each annotation's chords in runs, a chord held over bars once, a run cut
  where the key changes (`# tonic:`), at a pause or where no chord plays, and at a chord that is no triad (a power
  chord, a bare note). Each chord is where it is in the key (semitones above the tonic) and the kind of chord it is:
  major, minor, diminished, augmented, sus2 or sus4 (a seventh, a ninth, a sixth count as their triad). A stretch of
  a song in one key is in a minor key when its tonic's chord is more often minor than major; those where it is
  neither (10) are left out. Kept: per mode, how often each of the 72 chords changes to each other, and how often
  each is played.
