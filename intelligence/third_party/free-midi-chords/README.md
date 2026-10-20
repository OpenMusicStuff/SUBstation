# free-midi-chords's progressions

[free-midi-chords](https://github.com/ldrolez/free-midi-chords) by Ludovic Drolez, under the MIT licence
([LICENSE](LICENSE)), is a collection of chords and chord progressions as MIDI files, made from lists of progressions
in Roman numerals, each tagged with moods (its `chords.py`).

SUBstation takes the progressions tagged Sad, Dark or Nostalgic (80 of them, at commit
`95533fae2c1a2aae032211b9abed0441ac8ba298`): their numerals, tags and chords are compiled in, in
[ProgressionData.inc](../../src/harmony/ProgressionData.inc), which
[tools/progressions/build_tables.py](../../../tools/progressions/build_tables.py) writes. They are where the piano roll's
progression ideas start ([docs/intelligence.md](../../../docs/intelligence.md#progression-ideas)). Nothing else of
free-midi-chords is part of SUBstation; this folder holds its licence, which ships with every build.
