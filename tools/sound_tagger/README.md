# The sound tagger's training

How the sound tagger's model ([intelligence/src/tagging/TaggerModel.inc](../../intelligence/src/tagging/TaggerModel.inc),
compiled into SUBstation) is made: from open, labelled sample libraries, on the descriptors the sound similarity
computes anyway. What the tagger does and how well it works: [docs/intelligence.md](../../docs/intelligence.md#sound-tagging-what-a-sound-is-and-the-words-for-it).

Nothing here is part of the build. It needs Python 3.10+ with numpy and PyTorch (CPU is enough: about an hour on 4
cores), and the benchmarks' `sound_features_dump` (`-DSUBSTATION_BUILD_BENCHMARKS=ON`).

## The corpora

Downloaded by hand into one folder (`<data>`), each in its own subfolder:

| Folder | What | Where from | Licence | Used for |
|---|---|---|---|---|
| `tdm/` | tidal-drum-machines: 2 599 one-shots of 70 drum machines, in folders by kind (bd, sd, hh, oh, cp, cr, rd, lt/mt/ht, rim, cb, sh, tb, perc) | `git clone https://github.com/ritchse/tidal-drum-machines tdm` (6577395) | none stated (the machines' own samples, as collected for TidalCycles) | kinds of drums, by machine |
| `fsops/` | Freesound One-Shot Percussive Sounds: 10 254 one-shots (16 kHz), their Freesound tags and uploaders | Zenodo record 4687854: `one_shot_percussive_sounds.zip` (unzipped here), `sound_info_analysis.json`, `licenses.txt` | each sound its own: only CC0 and CC BY ones are used (9 535); CC BY-NC and Sampling+ ones are left out | kinds of real drums, acoustic or electronic, and the words people call sounds |
| `nsynth-valid/`, `nsynth-test/` | NSynth's validation and test splits: 16 774 four-second notes (16 kHz) of 10 instrument families, with qualities | `https://storage.googleapis.com/download.magenta.tensorflow.org/datasets/nsynth/nsynth-{valid,test}.jsonwav.tar.gz` | CC BY 4.0 | tonal kinds, acoustic/electric/synthesized, NSynth's qualities (distortion, reverb) |
| `tinysol/` | TinySOL: 2 913 orchestral notes (44.1 kHz) of 14 instruments; `meta.csv` its metadata | Zenodo record 3685367: `TinySOL.tar.gz` (unpacked here), `TinySOL_metadata.csv` as `meta.csv` | CC BY 4.0 | strings, brass, woodwinds |
| `libri/` | LibriSpeech's dev-clean: read speech (16 kHz), 40 speakers | `https://www.openslr.org/resources/12/dev-clean.tar.gz` | CC BY 4.0 | vocal chops (spoken) |
| `vocalset/` | VocalSet 1.1: sung notes, scales, arpeggios and excerpts (44.1 kHz), 20 singers | Zenodo record 1442513: `VocalSet11.zip` (unzipped here) | CC BY 4.0 | vocal chops (sung) |
| `dirt/` | Dirt-Samples: 2 038 files of TidalCycles' default sample set | `git clone https://github.com/tidalcycles/Dirt-Samples dirt` (c74fc80) | various (GPL-3.0 for the collection) | **held out**: never trained on, only measured on |

## Making the model

```sh
cmake -B build -DSUBSTATION_BUILD_BENCHMARKS=ON && ninja -C build sound_features_dump
python -I tools/sound_tagger/corpora.py --data <data> --work <work>
for c in tdm fsops nsynth tinysol excerpts vocals loops synths dirt; do
    build/bin/sound_features_dump --list <work>/lists/$c.txt --out <work>/feat/$c.bin
done
for c in tdm tinysol excerpts vocals loops synths; do     # band-limited copies: sounds with no top octaves
    build/bin/sound_features_dump --list <work>/lists/$c.txt --out <work>/feat/${c}_band8k.bin --band 8000
done
python -I tools/sound_tagger/train.py --work <work> --out intelligence/src/tagging/TaggerModel.inc \
    --test-reference tests/intelligence/TaggerReference.inc --report <work>/report.json
```

- [corpora.py](corpora.py) lists the files and their labels (`labels.json`): each corpus's kinds read from its own
  folders and metadata, Freesound's tags read into the tagger's words (`WORDS`), and three kinds of sounds made from
  the others: **excerpts** (7 000 NSynth and TinySOL notes cut short, 0.08 to 1.5 s from their start, as sample
  libraries chop them: a tonal one-shot needn't be long), **vocal chops** (5 000 parts of LibriSpeech's and VocalSet's
  recordings, a syllable to a phrase), **loops** (1 200 drum loops made from one machine's hits at 70 to 180 BPM, and
  500 tonal loops from one NSynth instrument's notes) and **synthesized sounds** (`Synth`, a small subtractive synth:
  2 400 synth plucks, stabs, leads, pads and arp notes, the synth kind, and sub, 808, reese, square and saw basses;
  1 600 drums made of oscillators and noise as drum machines and producers make them, sine-sweep kicks with a click
  and without, tone-and-noise snares, noise and square-wave hats, burst claps, toms, rims, cowbells). Excerpts and chops aren't files: they are a file's part
  (`path<TAB>start<TAB>length`), analysed as SUBstation analyses an audio clip's part.
- `sound_features_dump` fingerprints each list with the similarity's extractor (Essentia's 81 descriptors);
  `--band 8000` first limits a sound to 8 kHz (resampled to 16 kHz and back), so the band-limited corpora (NSynth,
  Freesound's, LibriSpeech) don't teach the model that tonal sounds have no air.
- [train.py](train.py) trains, measures and writes the model; `--quick` trains one network where it would train five;
  `--skip-cv --skip-final --temperature T --threshold P` measures on Dirt-Samples alone (with the cross-validation's
  temperature and threshold, as the report gave them).

`train.py` reports what it measured (`--report`): grouped cross-validation, whole drum machines, instruments,
uploaders, speakers and singers held out together; Dirt-Samples, held out entirely, with its near-duplicates of the
drum machines' sounds left out of training first; each learned word's AUC, over all sounds and among a kind's own.
Words whose held-out AUC is under 0.7 are written as not learned (`kWordLearned`): searches then read them as the
attributes they stand for.

A new model changes `kModelId`, which is part of the fingerprints' schema: SUBstation analyses a library again with
it, once.
