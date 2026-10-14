# Intelligence

The intelligence module is what SUBstation works out about music and sound: for now **sound similarity** (Find
Similar Sounds in the browser, on threads of its own; its descriptors made with
[Essentia](../intelligence/third_party/essentia), vendored), **sound tagging** (what each sample is and how it sounds,
"Snare · Punchy · Bright", by a model trained on open sample libraries, from the same descriptors; the browser's
searches by description, "hard trap snare", and its Best sort, which suggests what fits the project), **harmony** (a song's chords and key, inferred from its MIDI:
the piano roll's chord lane, its notes out of the key in red, and Generate's block chords and bass lines) and
**humanizing** (the piano roll's Humanize › Velocity: velocities from HUMANBRO's XGBoost model, through its C++ library,
vendored) and **track labels** (what each track is, in a few words: "Washed Out Serum Pluck", from its names, presets,
notes, effects and mixer; the tooltip over a track's name); later MIDI generation by machine learning (melodies,
accompaniment), a timing model for Humanize › Timing, and chords from audio. The MCP server agents read the project
through ([app/mcp.md](app/mcp.md)) hands them the harmony and the track labels. It is a layer of its own, as
the browser's backend is: the static library `sub_intelligence` ([intelligence/src](../intelligence/src), namespace
`sub::intelligence`), with no Qt, and knowing nothing of the engine or the browser; its application side is
[app/src/intelligence](../app/src/intelligence) (`SoundSimilarity`, `Session.similarity`, which hands out the tags
too; `SoundSuggestions`, `Session.suggestions`; `Harmony`,
`Session.harmony`; `Humanizer`, `Session.humanizer`; `TrackLabels`, `Session.trackLabels`). How Find Similar behaves
for the user is in [guide/browser.md](guide/browser.md#find-similar-sounds), the chords, the key, Generate and
Humanize in [guide/midi.md](guide/midi.md#chords-and-key), the track labels in
[guide/arrangement.md](guide/arrangement.md#what-a-track-is).

```
 application thread (Qt)                         sub::intelligence (C++, no Qt)
 -----------------------                         ----------------------------------------------
 BrowserController ── findSimilar(path, part) ─► SoundSimilarity::find ─► SoundIndex
   │                                                                       keeper (background priority)
   │  FileIndex::updated ──► libraryChanged ────────────────────────────►    sound-index.bin <-> fingerprints
   │                         source: the browser's snapshot ◄── called ──   takes the library from its source
   │                                                                       analysers ×N (background priority)
   │                                                                         decode (miniaudio), 44.1 kHz -> EssentiaExtractor
   │                                                                       search thread (normal priority)
   │                                                                         the sound's fingerprint, then every file's
   │  queued call ◄──────────────────────────────────────────────────── wake callback (result / progress)
   │  SoundSimilarity::take() -> found(SimilarSounds)
   ▼
 FileIndex::search(text, "similar", ..., scorer) ─► sub::browser: Sort::Score, by each file's similarity
```

## Sound similarity: the method, and why

### What was weighed

| Approach | Quality | Cost | Verdict |
|---|---|---|---|
| Learned embeddings (CLAP, PANNs, OpenL3, VGGish; what Ableton Live 12's similarity search and Sononym's use, by their own accounts) | best at *what* a sound is (kick vs snare vs vocal), which is what they are trained on | a runtime (ONNX Runtime, ~20 MB) and a model (CLAP: 89–158 M parameters, hundreds of MB), ~50 ms a file on a fast CPU: 300 000 files are 4 hours | not now: too heavy to start in the background of a DAW, and to ship. The extractor is replaceable for when one is small enough ([below](#what-would-make-it-better)). |
| Hand-made descriptors (MFCCs, spectral shape, envelope; Freesound's Essentia/Gaia, research on timbre spaces) | good at *how* a sound sounds, which is what picking the next kick needs | ~10 ms a file | **chosen**, several kinds together, made with **Essentia** |
| MFCCs alone | the classic, but blind to the attack and the envelope | cheapest | not enough on their own (below) |

Research on percussive timbre agrees on what listeners hear first: the **log attack time** and the **spectral
centroid** (Lakatos 2000; McAdams), then the spectral envelope (MFCCs) and how the sound dies away. Sononym, a sample
browser built on similarity, lets its users weigh *spectrum*, *timbre*, *pitch* and *amplitude* separately. The
fingerprint follows both: several aspects, each a group of features, compared each as a whole.

The descriptors are [Essentia](https://essentia.upf.edu)'s (the Music Technology Group's audio analysis library,
Freesound's descriptors), vendored at its latest release, 2.1-beta5: only its core and the 29 algorithms used, with
KISS FFT, nothing else (no FFTW, FFmpeg, libsamplerate, YAML, Gaia or TensorFlow;
[VERSION.txt](../intelligence/third_party/essentia/VERSION.txt)). SUBstation decodes and resamples, and hands it PCM.
It is AGPLv3: [licensing.md](licensing.md). It replaced the module's own descriptors (the same first six aspects,
hand-written; in git's history), on which it adds four aspects of descriptors Essentia has and they hadn't, and which
measurably find more sounds of a query's kind ([How well it works](#how-well-it-works)).

### The fingerprint ([EssentiaExtractor.h](../intelligence/src/similarity/EssentiaExtractor.h))

81 descriptors in ten aspects, then what the sound tagger makes of them: an embedding of 32 numbers (the eleventh
aspect, Embedding) and the tagger's outputs (Tags: never compared; [below](#sound-tagging-what-a-sound-is-and-the-words-for-it)).
Every sound is analysed at 44.1 kHz (decoded, or resampled, to it: a 22, 48 or 96 kHz copy
of a sound is measured as the 44.1 kHz one is), from where it starts (leading silence skipped, the next 6 s; a 2 ms
pre-roll always the same, so where frames fall doesn't depend on the silence before), its level made relative to its
peak (MFCC 0, a frame's level, is left out): neither the level nor the rate changes a fingerprint.

| Aspect | Features (Essentia's algorithms) | Why |
|---|---|---|
| Timbre | MFCCs 1–12 (`MFCC`: 40 HTK mel bands, 20 Hz–16 kHz, power in dB, DCT-II), the mean over the sound, louder frames counting more (those 50 dB down not at all) | the spectral envelope, independent of level |
| TimbreMotion | MFCCs 1–4 of the attack (first 30 ms), the body (to 250 ms) and the tail | a snare's crack and its ring, a clap's bursts: an attack's character kept apart, not averaged away |
| TimbreSpread | each MFCC's spread over the sound | a loop's timbre moves from hit to hit, a pad's hardly; the best single gain on loops |
| Spectrum | centroid (`Centroid`) and its spread over time, bandwidth (`CentralMoments`, `DistributionShape`), 85% roll-off (`RollOff`), flatness (`Flatness`, dB), the share below 120 Hz and above 8 kHz (`EnergyBand`, dB), the attack's centroid and flatness | brightness, noisiness, sub-bass (808s), air (hats); a click against a thump |
| Contrast | peaks against valleys in six bands, and the valleys (`SpectralContrast`) | tonal against noisy, band by band: the best single aspect |
| SpectralShape | skewness and kurtosis (`DistributionShape`), `Crest`, `ZeroCrossingRate`, change from frame to frame (`Flux`, from the second frame on), slope (`Decrease`) | the shape beyond its centre and spread |
| Tonality | how many peaks within 60 dB of the loudest, 20 Hz–16 kHz (`SpectralPeaks`: the spectral complexity), how they beat (`Dissonance`, the lowest 100), how strongly the spectrum repeats 200 Hz–5 kHz apart (`PitchSalience`, every fourth frame) | metal (cymbals, cowbells), detuned stabs, harmonic against inharmonic |
| Envelope | log attack time (`LogAttackTime`, 20%→90% of the peak amplitude), effective duration (`EffectiveDuration`, within 30 dB of the peak), temporal centroid (`Centroid` over time), the level in eight octave-wide windows after the peak (20 ms … 2.6 s) | closed vs open hats, tight vs boomy kicks, one-shots vs pads |
| Pitch | how periodic (`PitchYin`, YIN, on 93 ms windows from the peak, down to 21.5 Hz), and that confidence times the pitch in octaves | tuned 808s and tonal one-shots near their pitch; noise near noise |
| Embedding | the sound tagger's 32-number embedding: a learned layer where sounds of a kind lie together (trained on labelled libraries to tell 23 kinds apart, loops from one-shots, and what people call sounds) | what a sound *is*, across the descriptors: the best single aspect on held-out sounds ([below](#how-well-it-works)) |
| Rhythm | onsets per second after the first (spectral flux of the mel bands that also raises the level 3 dB), the file's length | loops apart from one-shots, a 4-bar loop apart from a 1-bar one |

Frames are 23 ms (1024 samples), a quarter hop; the amplitude envelope is in 2 ms blocks. Essentia computes each
frame's descriptors and the envelope's; the module frames, sums up (weighted means; the attack, body and tail apart)
and adds what Essentia has no algorithm for, or one that doesn't suit one-shots: the level after the peak, onsets
after the first, the pitch's summary ([Descriptors.h](../intelligence/src/similarity/Descriptors.h)). So a fingerprint
is the same size for a 5 ms click and a 6 s loop, and a short one-shot's attack isn't lost in an average.

What was learnt making it:

- **`PitchYinFFT` can't hear an 808.** Its loudness weighting leaves nothing of a 40 Hz fundamental (no pitch at all
  at 41 Hz); `PitchYin` on the sound brought down to 11 kHz finds it to 5 cents.
- **`SpectralContrast` wants magnitudes below 1.** It raises each band's peak/valley to 1/ln(the band's mean) and blows
  up (to 10³³) where a mean nears 1, which unnormalised FFT magnitudes do. It is handed the spectrum scaled to a full
  scale of 1.
- **miniaudio's resampler is linear**, which takes a few dB off the top octave: a 48 kHz copy of a sound analysed
  darker than the 44.1 kHz one. Resampling is band-limited (a Kaiser-windowed sinc, `resampleMono`), as a polyphase
  filter made once for a pair of rates: 6.5 s from 48 kHz in 4 ms, from 192 kHz in 10 ms.
- **Some of Essentia's algorithms don't fit as they are.** `SpectralComplexity` counts peaks only between 100 Hz and
  5 kHz (a hat's, all above, count as none): the peaks `SpectralPeaks` finds in the whole band are counted instead.
  `HFC` sums the whole spectrum, so anything above 16 kHz (there in a WAV, gone from its MP3) moved it without bound;
  over the band alone it is the centroid again, and leaving it out lost nothing on the benchmark. `Flux`'s first frame
  is measured against silence, so it is left out: a click's flux would otherwise be its spectrum.
- **Essentia's logger isn't thread-safe** (a global indent counter): it is compiled out (`DEBUGGING_ENABLED=0`).
- Descriptors that didn't help, and so aren't in: GFCCs (ERB-band cepstra), inharmonicity and the odd-to-even harmonic
  ratio, and the envelope's `DerivativeSFX` and `FlatnessSFX` (each lowered the held-out precision).

### Comparing ([Similarity.h](../intelligence/src/similarity/Similarity.h))

Each feature is measured in spreads *of the library searched* (z-scores; never dividing by less than a small spread
of the feature's own, set at 5–10% of its typical spread), its squared difference clipped at 9 (3 spreads: one wild
feature, a pitch an octave off, can't outweigh the rest); an aspect is the mean of its features', and the distance the
aspects' weighted mean, so an aspect counts by its weight however many features it has. Similarity is
`exp(-distance / 2)`: 1 for the same sound, about 0.37 for two unrelated ones. Neither level nor length can dominate:
the level isn't a feature, and the length and the effective duration are one feature each of aspects of several.

The library's statistics (each feature's spread) are measured robustly, its extreme 1% at either end held at the 1st
and 99th percentiles (winsorized), so a few broken or freakish files don't flatten the scale for everything else. The
index keeps them and saves them with the fingerprints, measured again when it saves after a change, or when a search
finds that fingerprints changed for a tenth as many files as they describe, or the library grew or shrank by a tenth
(on at most 20 000 of its fingerprints, without holding up the index's other threads).

The weights: Timbre 0.5, TimbreMotion 1.5, Spectrum 2.5, Envelope 2, Pitch 0.5, Rhythm 0.5 (tuned on a 5 000-file
Splice library for the module's own descriptors, rounded rather than taken at the optimum), TimbreSpread, Contrast,
SpectralShape, Tonality 1 each: untuned, round, and each measured to help on its own (below); Embedding 3 (measured on
both halves of Dirt-Samples, which the tagger never trained on: more gained nothing more); Tags 0, always (the
distance stops before them: they cost a search nothing). They are the
extractor's (`FeatureSchema::weights`); a search could take others (`SoundIndexOptions::weights`).

### How well it works

[benchmarks/sound_similarity_bench.cpp](../benchmarks/sound_similarity_bench.cpp) on
[Dirt-Samples](https://github.com/tidalcycles/Dirt-Samples) (TidalCycles' sample set, not in the repository: 2 068 WAV
files, 221 folders, at 44.1, 22.05, 48, 16 kHz and odd rates), with 30 drum loops added (made from its kicks, snares and
hats at 90–174 BPM). Labels come from folder and file names (kick, snare, clap, closed and open hat, tom, cymbal, rim,
808, synth stab, pluck, synth bass, sub bass, arp; loops). Each of 583 labelled one-shots is a query over the whole
library; precision@10 is the share of its 10 nearest that are one-shots of its kind (unlabelled files count as misses,
so these are lower bounds). Each of the 40 loops is a query too, its hits other loops. Both extractors on the same
files, labels and queries:

| P@10 | the module's own descriptors (before) | **Essentia's** |
|---|---|---|
| one-shots, mean over kinds | 0.467 | **0.492** |
| one-shots, all queries | 0.580 | **0.602** |
| P@1, mean over kinds | 0.671 | **0.683** |
| loops | 0.462 | **0.528** |
| kicks (113 queries) | 0.455 | **0.504** |
| snares (129) | 0.692 | **0.692** |
| toms (75) | 0.895 | **0.901** |
| cymbals (62) | 0.673 | **0.687** |
| closed hats (56) | **0.352** | 0.348 |
| open hats (20) | 0.370 | **0.390** |
| claps (11) | **0.455** | 0.436 |
| synth stabs (23) | 0.804 | **0.870** |
| plucks (17) | 0.794 | **0.835** |
| synth bass (28) | 0.414 | **0.504** |
| 808s (11) | 0.227 | **0.264** |
| sub bass (20) | **0.150** | 0.140 |

Whether each added aspect helps was decided on half the queries held out: the queries' folders split in two by a hash
(`--split`), each aspect added alone to the six, and kept only if one-shot precision (over kinds and over all
queries) rose on the held-out half and didn't fall on the other. Held out (320 queries): the six aspects 0.469 (mean
over kinds) / 0.588 (all queries) / 0.491 (loops); all ten 0.514 / 0.628 / 0.591. Weights
tuned on one half did *worse* on the other (0.514 → 0.498, while the half tuned on rose from 0.407 to 0.426): the halves hold different kinds, so the round weights
stay. Alone, Contrast (0.390) beats every one of the module's own aspects (the best, TimbreMotion, 0.368). What is left
are mostly neighbours a listener would call close (claps and snares, open hats and crashes, sub basses and long 808
kicks) or files whose names don't say what they are.

**The tagger's embedding** (the Embedding aspect, [below](#sound-tagging-what-a-sound-is-and-the-words-for-it)),
measured the same way on Dirt-Samples as it is in the repository's history now (2 038 files, without the 30 loops
added above, so 583 one-shot and 10 loop queries), the queries halved by folder (`--split`), the tagger never having
heard any of them:

| P@10, one-shots | descriptors alone | **with the embedding (weight 3)** |
|---|---|---|
| mean over kinds, one half | 0.505 | **0.535** |
| mean over kinds, the other | 0.407 | **0.439** |
| all queries, the other half | 0.577 | **0.610** |
| P@1, mean over kinds, one half | 0.640 | **0.704** |

Alone, the embedding (0.495 on the first half) beats every descriptor aspect (Contrast 0.423): it was trained to say
what a sound *is*, and puts sounds of a kind together; the descriptors still tell one kick from another. Weights 2 to 5
all help on both halves, from 3 on no more.

The module's own descriptors had 0.694 on the 5 091-file Splice library they were tuned on (kicks 0.84, snares 0.79,
closed hats 0.53): that library's labels are cleaner than Dirt-Samples' file names, so the two sets' numbers don't
compare with each other, only the extractors on one set.

Speed (a release build on a 4-core cloud machine, the index's own analysers at background priority):

| | |
|---|---|
| analysing a file | median 10 ms (95%: 58 ms; a 6 s loop up to 130 ms): the module's own descriptors took 1.4 ms. About half of it is the four aspects added (their gain was judged worth it); most of the rest the spectrum, PitchYin and MFCCs |
| analysing 2 068 files, first time, 4 analysers | 8.2 s (252 files a second): 300 000 files are about 20 minutes, once |
| next start: reading the saved fingerprints | 3 ms (0.8 MB), all files checked against their stamps in 8 ms |
| a search over 2 068 fingerprints | 0.7 ms, asked to taken (300 000 files: about 100 ms) |

### What would make it better

The Embedding aspect is now the sound tagger's, learned from its descriptors ([below](#sound-tagging-what-a-sound-is-and-the-words-for-it)).
An audio model's embedding (an EfficientAT or a distilled OpenL3, a few MB) would see what the descriptors don't (they
are the tagger's only input), and would help most *across* kinds; it would bring a runtime (ONNX Runtime is the
lightest) and decoding's cost again. The descriptors stay what tells one kick from another.

## Sound tagging: what a sound is, and the words for it

[tagging/](../intelligence/src/tagging) (namespace `sub::intelligence::tagging`) tells what every sound of the
library is (a kick, a snare, a closed hat... bass, a synth, keys, vocals: 23 kinds), whether it is a loop, its note, how it
sounds among its own kind (bright, hard, long, low, distorted, wet, acoustic, electric, synthesized, pitched) and what
people would call it (punchy, dubstep, lo-fi, 808...); and reads what a search's words ask for ("hard trap snare",
"low chill kick", "long crash", "vocal chop", "c#2 bass"). The browser searches and labels files with it, and its Best
sort weighs how well each fits the project ([browser.md](browser.md)). It costs no analysis of its own: it reads the
similarity's fingerprint, and runs as part of making it.

### What was weighed

| Approach | Quality | Cost | Verdict |
|---|---|---|---|
| A learned audio model (a CNN or transformer on spectrograms: PANNs, CLAP, EfficientAT; what commercial taggers use) | the most accurate at *what* a sound is, given millions of labelled clips | a runtime and a model of tens to hundreds of MB, 10–50 ms a file, and decoding again | not here: the overhead the similarity avoided ([above](#what-was-weighed)) |
| Rules over the descriptors (a centroid under 200 Hz is a kick...) | brittle: a tom, an 808, a bass note are all low | none | no |
| **A small learned model over the descriptors the similarity already has** | the descriptors were made to tell sounds apart; a model learns which combinations say "snare" | 22 µs a sound, 0.8 MB of weights compiled in | **chosen** |
| Tags from file names only (as the browser's search was) | as good as the names: "SD_0003.wav", "Untitled 7.wav" say nothing | none | kept: names still match first |

### The model ([SoundTagger.h](../intelligence/src/tagging/SoundTagger.h), [tools/sound_tagger](../tools/sound_tagger))

Five small networks (an ensemble), each 80 inputs (the fingerprint's descriptors less the file's length, held to their
training range and standardized) → 192 → 96 → 32 (ReLU) → five heads: the kinds (softmax, the members' logits
averaged and temperature-scaled), loop or one-shot, acoustic / electric / synthesized, NSynth's ten qualities (two of
which, distortion and reverb, are used), and 32 words people call sounds (each a sigmoid). The 32-number layer is the
sound's **embedding**: where sounds of a kind lie together. Weights are compiled in
([TaggerModel.inc](../intelligence/src/tagging/TaggerModel.inc), generated): no file to load or lose, nothing to
download. Trained with AdamW and a one-cycle schedule for 40 epochs, inputs noised and features dropped at random,
labels smoothed, each head's loss counted only where a corpus labels it, kinds balanced (a kind's sounds weigh
1/√its count) and each word's positives as much as its negatives.

What it learned from (all open; [tools/sound_tagger/README.md](../tools/sound_tagger/README.md) says where each comes
from and how to make the model again):

| Corpus | Sounds | Teaches |
|---|---|---|
| tidal-drum-machines: 70 drum machines | 2 599 | 13 kinds of drums, from machines the model never saw |
| Freesound One-Shot Percussive Sounds (CC0 and CC BY only) | 9 535, 610 uploaders | real drums, acoustic and electronic; and the words: each sound's Freesound tags read into 32 words |
| NSynth (validation and test splits) | 16 774 notes | 10 instrument families; acoustic, electric or synthesized; distortion, reverb |
| TinySOL | 2 224 notes | strings, brass, woodwinds at 44.1 kHz |
| Notes cut short (NSynth's, TinySOL's), 0.08–1.5 s | 7 000 | a tonal sound can be short: shortness isn't a drum's |
| Vocal chops: LibriSpeech dev-clean, VocalSet | 4 777 (of 5 000 parts: the rest were silence) | spoken and sung vocals, a syllable to a phrase |
| Loops made from the drum machines' hits and NSynth's notes, 70–180 BPM | 1 700 | loops against one-shots |
| Sounds synthesized as producers make them: plucks, stabs, leads, pads, arps; sub, 808, reese, square and saw basses; sine-sweep kicks (with a click and without), tone-and-noise snares, noise and square-wave hats, burst claps, toms, rims, cowbells | 4 000 | the synth kind (NSynth's free splits have no synth leads), synth basses, and drums made of oscillators and noise rather than recorded |

NSynth, Freesound's sounds and LibriSpeech are 16 kHz: nothing above 8 kHz. So every full-band corpus is trained on
twice, once as it is and once band-limited to 8 kHz (resampled through 16 kHz): what tells a hat from a tom has to be
in more than the top octave, and a library's 16 kHz samples are tagged as its 44.1 kHz ones are.

### Reading it ([SoundTagger.h](../intelligence/src/tagging/SoundTagger.h))

- **The kind** is said only when its probability is at least 0.70: where held-out predictions are right 90% of
  the time. Below that the sound has no kind (its row says how it sounds), and searches still find it by its three
  likeliest kinds together (at least 0.5).
- **Attributes, among its own kind.** Each kind's 20th and 80th percentiles of a descriptor in the training corpora
  are 0.1 and 0.9 (a logistic between): Bright by the spectral centroid, Hard by the log attack time (shorter: harder),
  Long by the effective duration, Low by the sub-bass share (drums) or the note (tonal sounds). So a bright kick is one
  of the brighter kicks, not a kick as bright as a hat. Distorted and Wet are NSynth's qualities, Acoustic, Electric
  and Synthetic the source head, Pitched YIN's confidence.
- **Words, among its own kind**: a sound is called a word when its score is above the word's 60th percentile among
  the training sounds of its kind (the "hard" snares are the harder 40% of snares). A word is used only if, held out,
  it told the sounds people tag with it from the others (AUC ≥ 0.7, `kWordLearned`); otherwise searches read it as
  the attributes it stands for.
- **The note**: the fingerprint's pitch (YIN's, `PitchConfidence` and `Pitch`), for tonal sounds and tuned kicks and
  toms.

### Words for sounds ([SoundWords.h](../intelligence/src/tagging/SoundWords.h))

A search's words are read as kinds ("kicks", "hi-hat", "cymbal": crash or ride, "piano": keys, "vox": vocal, "pluck":
a synth...), loops or one-shots ("break", "chop"), notes ("c2", "f#"), descriptions (a blend of attributes and learned
words, each weighted: "punchy" is 0.6 the learned word and 0.4 a hard attack), genres (0.6 the learned word, 0.4 the
attributes it stands for: "dubstep" is the learned word with distortion, synthesis and a hard attack; a word not
learned well enough is left out of its blend, so "trap", which only 24 sounds were tagged with, is the learned hip-hop
word with synthesis and a hard attack), and moods ("chill", "emo", "cinematic"...), which no corpus labels: they are blends of
attributes too, but they only order sounds, never leave one out. A sound is heard as a word when its degree is 0.5 or
more; a search's score for a sound is the geometric mean of its words' degrees. A sound's label is its kind, "Loop",
its note, and its three clearest traits, each saying something different ("Snare · Punchy · Bright").

### Fitting a project ([ProjectFit.h](../intelligence/src/tagging/ProjectFit.h))

How well a sound fits a project, for the browser's Best sort: 0.35 × its genre and character (the learned words the
project's sounds are clearly more than not, weighed by how much, against the sound's degrees of them) + 0.35 × its
nearest neighbour among the project's sounds (how close its description, attributes and learned words, is to the
closest of up to 12 of them) + 0.3 × its key (a pitched sound on the project key's tonic or fifth 1, in its scale
0.85, out of it 0.1; unpitched 0.5). A project the tagger knows nothing of fits everything at 0.5. The project's
sounds are the files its audio clips and samplers play; its key the project's, or the one its notes are in
(`Session.harmony`).

### How well it works

Measured by [train.py](../tools/sound_tagger/train.py) in two ways. **Grouped cross-validation** over the training
corpora, five folds, whole drum machines, instruments, Freesound uploaders, speakers and singers held out together
(so a fold's sounds are of machines and people the model never heard):

| Held out | Sounds | Accuracy | Macro F1 | Right where it names a kind | Names a kind for |
|---|---|---|---|---|---|
| Drums (13 kinds), whole drum machines | 2 599 | 78.5% | 0.750 | 89.9% | 68.9% |
| NSynth's notes, whole instruments | 16 774 | 77.2% | 0.741 | 88.3% | 65.7% |
| TinySOL's notes, whole instruments | 2 224 | 81.0% | 0.744 | 87.8% | 79.0% |
| **All 23 kinds, every corpus** | 43 991 | **78.6%** | 0.742 | **90.0%** | 70.3% |

By kind (recall): kicks 92%, claps 93%, shakers 89%, tambourines 88%, closed hats 84%, rides 84%, snares 81%, toms
79%, crashes 79%, cowbells 71%, open hats 67%, other percussion 59%, rims 50%; strings 97%, organs 84%, mallets 83%,
guitars 81%, woodwinds 81%, keys 80%, basses 73%, brass 54%, NSynth's vocals 27% (its "vocal" family is mostly
synthesized voices, which now go with the synths; real speech and singing are found: Dirt-Samples, below). Loops
against one-shots: 99.2% (AUC 0.9995). The note: 92% of notes have one, its pitch class right for 93.6% of them
(to the semitone 83%, octave errors being YIN's). Acoustic against electronic drums (Freesound's): AUC 0.73.

And **Dirt-Samples** (TidalCycles' 2 038 files, the similarity's benchmark library), never trained on, its
134 near-duplicates among the drum machines' sounds (the same machines' samples) left out of training first.
Labels are its folders' names (bd, sn, hh, cp... `DIRT_KINDS` in corpora.py: 779 of its files; its bassdm folder holds
bass drums, kicks), its loops the breaks folders and
files named loops:

| Dirt-Samples | Sounds | Accuracy | Right where it names a kind | Names a kind for |
|---|---|---|---|---|
| Drums | 426 | 75.8% | **91.5%** | 66% |
| Tonal sounds | 353 | 44.2% | 76.2% | 42% |
| **All** | 779 | **61.5%** | **86.2%** | 55% |

By kind (recall, then precision): kicks 91% / 66%, snares 89% / 84%, toms 100% / 53%, open hats 91% / 67%, crashes
81% / 71%, claps 83% / 62%, closed hats 16% / 62%, other percussion (congas, tablas) 55% / 54%; vocals (speech, a
speech synthesizer, spoken numbers) 65% / 75%, synths (stabs, plucks, arps, hoovers) 47% / 68%, basses 43% / 90%,
woodwinds (saxophone notes) 18%, keys (a Casio's notes) 3%. Loops against one-shots: AUC 0.999 (96% right at 0.5).
Of the sounds in Dirt-Samples' 150 folders that name no kind (birds, glitches, speech fragments, numbers...), 46% are
said to be one of the kinds anyway: the kinds are what a sample browser lists, not every sound there is.

What it gets wrong are mostly neighbours (claps and snares, closed and open hats, a sub bass and a long 808 kick, a
tom and a low conga) and Dirt's oddities (its "vocals" are a talking calculator's numbers and a speech synthesizer's
diphones). The words, held out by uploader (AUC: 0.5 is chance; "among its kind" is how well it orders the snares
called hard among snares):

| Word | Sounds tagged with it | AUC | AUC among its kind |
|---|---|---|---|
| dubstep | 179 | 0.91 | 0.88 |
| techno | 226 | 0.85 | 0.83 |
| punchy | 295 | 0.83 | 0.81 |
| rock | 67 | 0.82 | 0.85 |
| trance | 101 | 0.82 | 0.57 |
| hard | 201 | 0.79 | 0.82 |
| electronic | 1 071 | 0.79 | 0.71 |
| distorted | 96 | 0.79 | 0.75 |
| fat | 184 | 0.79 | 0.75 |
| deep | 178 | 0.78 | 0.62 |
| hip-hop | 90 | 0.78 | 0.43 |
| lo-fi | 41 | 0.76 | 0.94 |
| industrial | 59 | 0.76 | – |
| glitch | 42 | 0.76 | 0.76 |
| wooden | 295 | 0.76 | 0.74 |
| dnb | 199 | 0.73 | 0.74 |
| acoustic | 1 158 | 0.71 | 0.78 |
| *not used* (under 0.7): 808 0.68, vintage 0.68, heavy 0.67, 909 0.66, edm 0.66, reverb 0.61, house 0.60, processed 0.57, high 0.52, trap 0.51 (24 sounds), metallic 0.49, tight 0.46, bright 0.45, dry 0.39, soft 0.34 | | | |

The words not learned are mostly ones Freesound's uploaders use for what is measured better: bright, soft, tight,
high, dry (the attributes do them, among the kind), or too few sounds were tagged with (trap: 24).

Speed (a release build, one core of a 4-core cloud machine):

| | |
|---|---|
| the model, a sound (the ensemble of five, on the analyser's thread) | 22 µs: 0.2% of the sound's analysis (about 10 ms) |
| reading a fingerprint into tags (`describe()`) | 0.25 µs |
| a library's tag table, 200 000 files | 45 ms, on a thread of its own, at most every 1.5 s while files are analysed |
| a search hearing "hard trap snare" | 2.6 ms more per 10 000 files |
| the Best sort's project fit | 0.8 µs a file the first time, then kept until the project or the tags change |

### What would make it better

- A larger, better labelled one-shot library (Freesound's tags are noisy: a sound not tagged "punchy" may well be):
  the model, the corpora script and the measures are ready for it.
- Moods from data: no open library labels sound by mood; listeners' ratings of a few thousand samples would let
  "chill" and "emo" be learned as the genres are.
- Keys from samples' names (the audio clips' parser) as the tagger's note for loops, which YIN can't give.
- An audio model's embedding, if one small enough to run in the background appears: as another head's input.

## Harmony: chords and keys from MIDI

[harmony/](../intelligence/src/harmony) (namespace `sub::intelligence::harmony`) infers a song's chords and key from
its notes, and writes parts from chords. It is pure functions: no threads, no state, nothing saved. The application
asks again whenever the song has changed and somebody wants to know (see [the application side](#harmony-the-application-side)).

### What was weighed

| Approach | Quality | Cost | Verdict |
|---|---|---|---|
| Learned models (an HMM trained on annotated songs, a transformer over MIDI) | the best at ambiguous passages and at style | a model to train, ship and run; annotated data | not now: the rules below get the clear cases right, and the module is where a model would go |
| Chord templates against pitch-class profiles, smoothed (Pardo and Birmingham; Fujishima's chroma templates; Sheh and Ellis's HMM smoothing, its transitions written by hand) | right wherever chords or a bass are played; a melody alone is a guess | a song's thousands of notes in a few milliseconds | **chosen** |
| Note counting per bar | breaks on passing notes and on chords that change mid-bar | the cheapest | no |

### The chords ([ChordInference.h](../intelligence/src/harmony/ChordInference.h))

The song is cut into steps of an eighth note (longer for a song past 65 536 steps). Each step has two profiles of the
twelve pitch classes: how long each sounds (louder notes counting up to twice as much as the softest), and how long
each is the lowest note, counting fully up to E2, a quarter from G3 up (a melody's lowest note is no bass).

Each of 132 chords (12 roots × major, minor, diminished, augmented, sus2, sus4, 7, maj7, m7, m7b5, dim7; a sixth
chord is its relative minor seventh with another bass: C6 is Am7/C) is scored against each step:

| Term | Value |
|---|---|
| explained | the share of the step's profile on the chord's notes, less half the share off them |
| missing notes | for each chord note below 30 % of the step's loudest pitch class, up to 0.25 for the root or a seventh, 0.15 for the third (a sus chord's second or fourth, a diminished or augmented fifth), 0.05 for a perfect fifth |
| bass | + 0.25 × how much the root is the bass, + 0.08 for its other notes, − 0.1 for notes outside it |
| quality | 0 for major and minor; − 0.02 to − 0.04 for sevenths, − 0.06 to − 0.1 for the rest |
| key | + 0.05 when every note is in the key |
| thin steps | with fewer than three pitch classes sounding (a melody, a bass line), up to + 0.3 for the key's primary triads (I, IV, V; i, iv), less for the others, − 0.2 outside the key, − 0.3 for sus, augmented and diminished sevenths: a melody alone implies plain triads |

A step counts by how much sounds in it (two notes all through it count fully), so a lone melody note weighs less than
a chord. Then a Viterbi search finds the sequence of chords (or "no chord", which scores 0.1 a beat where nothing
sounds) with the best total, where changing chord costs 0.25 on a bar line, 0.45 on half a bar, 0.7 on a beat and 1.0
between beats. So a passing note doesn't make a chord, a chord played clearly changes where it is played (an eighth
before the bar, too), a rest of a beat keeps the chord and one of more than a bar and a half has none. With "stay or
change to the best", the search is linear in the chords: steps × 133.

A chord's bass is the pitch class lowest under it the longest; if that is another of its notes, and is the bass under
at least half of it at full strength, it is an inversion ("C/E").

### The key

Krumhansl and Kessler's major and minor key profiles, correlated with how long each pitch class sounds, counting again
how long it is the bass (by the same register weighting): the bass tells C → G (C major) from the same notes in E
minor. Fewer than 2 beats of notes or three pitch classes, or a best correlation under 0.4, is no key. A minor key's
scale is its natural minor (as in Ableton's scales); the classic confusion of a key with its relative changes no note
in or out of it.

### Parts from chords ([Accompaniment.h](../intelligence/src/harmony/Accompaniment.h))

- **Chords**: a close voicing for each, between C2 and G4, the first in root position near D3, each next one the
  inversion and octave that moves the voices least (each note to the nearest of the other chord's, both ways, plus a
  quarter of how far it strays from D3): C, G, Am, F come out as C3 E3 G3, B2 D3 G3, C3 E3 A3, C3 F3 A3. Velocity 90.
- **Bass**: each chord's bass note (an inversion's, else its root) in the octave from C1. Velocity 100.
- Both are struck again at every bar line.
- **A progression to start from**, where a song has no chords: a chord a bar, I V vi IV in a major key, i VI III VII in
  a minor one.

Rules, not a model: a melody, or an accompaniment with a rhythm of its own, needs machine learning, which comes later.

### How well it works

The tests ([tests/intelligence/test_harmony.cpp](../tests/intelligence/test_harmony.cpp)) hold it to triads and
sevenths in root position, inversions, a progression with its bass on another track, a melody running over chords
(heard as the chords), arpeggios (as the chords they spell), rests short and long, and a melody alone (Twinkle,
Twinkle: C | F G | F C | Dm C). Speed: a 400-bar song of 4 400 notes, chords and a melody, in about 5 ms (a release
build, one core of a 4-core cloud machine); a typical song is under a millisecond.

### What would make it better

Chords from audio: the same scoring over the chroma of audio clips (the similarity's FFTs and frames). Time signature
changes, and tempo changes, if the project gets them. A model trained on annotated MIDI as one more term of the score
(or in place of the hand-set transitions), for the ambiguous passages.

## Humanizing: velocities by machine learning

[humanize/](../intelligence/src/humanize) (namespace `sub::intelligence::humanize`) gives a part's notes the
velocities a pianist would play them with. Velocity and timing are separate models: velocity is this one; timing is
to come (Humanize › Timing nudges starts at random until then: `notes::humanizedTiming`).

### The model

HUMANBRO, the project's own model: an XGBoost regressor (1 986 trees of depth 8) trained on the MAESTRO v3
performances (1 276 recorded piano performances, 7 million notes), which predicts each note's velocity from 127
features of its musical context, every one of them something a score shows: its pitch and register, where it falls in
the bar (downbeat, beat, offbeat), its length, the intervals to the notes before and after it and the melody's contour,
the texture around it (how many notes in the last and next seconds and beats, their range), its place in its chord
(top, bottom, inner voice), the phrase (rests, how far into it), and the piece as a whole. It sees no velocity: the
same notes always get the same prediction, and a part's own velocities don't leak into it. How it was trained,
evaluated and checked for leakage is in HUMANBRO's README; its C++ runtime is vendored in
[intelligence/third_party/humanbro](../intelligence/third_party/humanbro) (its own README and VERSION.txt: what was
copied, and the one local change, loading a model from memory so the file is opened through the module's wide-path
`platform::openFile`). It is built as its own static library, `humanbro`, with its strict floating-point flags
(`/fp:precise`; `-ffp-contract=off -fno-fast-math`): its features match its Python pipeline bit for bit.

The model shipped is [intelligence/models/velocity.hbm](../intelligence/models/velocity.hbm) (15.7 MB, in the
repository), HUMANBRO's *quantized* model: trained on the performances with every note snapped to the beat grid, and
snapping the notes it is given to the grid itself. A DAW's notes are drawn, quantized or played in against a tempo
map, which is what it suits; HUMANBRO's other model, trained on the performances' own timing, does much worse on
quantized notes (it leans on the timing pianists play with). The build copies it to `bin/models/velocity.hbm`, next
to the executables, where the application looks for it.

| On MAESTRO's test split (177 performances, 741 410 notes) | MAE | Pearson r | within a performance r |
|---|---|---|---|
| the quantized model, on quantized notes | 11.3 | 0.62 | 0.59 |
| every note at the training set's mean velocity | 15.3 | – | – |
| each pitch at its mean velocity in the training set | 14.3 | 0.32 | 0.36 |

### A part ([VelocityModel.h](../intelligence/src/humanize/VelocityModel.h))

A note's features look at the notes before and after it, so a part is humanized as a whole: `humanize(part, meter,
amount)` takes every note of it, those to humanize marked `target`, the others context, and returns the part's
velocities with only the targets' changed.

- **Where it is**: notes are in quarter-note beats on the song's timeline, bar lines every bar from beat 0; the part
  is handed to the model from the bar its first note is in (at 960 ticks a quarter note, with the song's tempo and
  time signature), as a score would start: the features count beats and bars from the start, so a part whole bars
  later in the song gets the same velocities.
- **Its level**: how loud a part is overall a score doesn't say (MAESTRO's recordings differ by session and piano), so
  the model's velocities are levelled to the targets' own mean velocity: the targets keep their loudness and take the
  model's shape. Then each target moves `amount` (0..1) of the way to its levelled velocity, rounded, held to 1..127.
- **What it hears**: on a melody over chords it plays the melody about 25 louder than the accompaniment, a chord's
  top note a little louder than the rest, a phrase's first note softer. Its dynamics are compressed, as a
  squared-error model's are (HUMANBRO measured its unquantized model's spread at a standard deviation of 13 where the
  pianists' is 19); amounts are how far toward it, never past.

Speed (a release build on an Intel Core Ultra 7 270K Plus): loading the model, 18 ms and 16 MB; 200 notes, 29 ms;
500, 71 ms (on one core: the runtime spreads rows over cores from 512 notes on); 2 500, 81 ms; 10 000, 108 ms.

### Humanizing: the application side

`Humanizer` ([Humanizer.h](../app/src/intelligence/Humanizer.h), `Session.humanizer`) lives on the application's
thread. It loads the model the first time velocities are asked for (from `velocityModelPath()`,
`<the application's folder>/models/velocity.hbm`) and keeps it; if it can't (missing, damaged, locked), it says so
on `statusMessage`, gives no velocities, and tries again the next time. `velocityAvailable` (the file is there) is what greys out the menu entry.

`velocities(targets, amount)` takes notes of clips (each with its track and clip) and judges each track as one part:
every note its clips are heard playing (`Clip::heardNotes()`: none of a deactivated clip, nor deactivated notes, each
where it plays on the timeline) is context, and the targets are put where their clips play them (deactivated or
hidden by a trim, they are still humanized). Drum tracks (`Harmony::isDrumTrack()`) are left as they are, and the
status line says so: the model knows pianos. Each track's targets keep their own mean, so humanizing a quiet pad and a
loud lead together leaves each as loud as it was. Nothing runs in the background: a click on Humanize › Velocity
waits for it, a tenth of a second for the longest parts.

## Track labels: what each track is, in words

[labels/](../intelligence/src/labels) (namespace `sub::intelligence::labels`) says what each track of a project is, in
a few words, as a producer would name it: "Washed Out Serum Pluck", "Orchestral Trumpet", "Sad Piano Line", "Echoing
Bell Stab", "Drum Group (Kick, Snare)". People see it over a track's name (its tooltip); the MCP server hands it to
agents as context ([app/mcp.md](app/mcp.md): `list_tracks`, `get_track`), with the track's family ("synth", "drums"...), its role ("pluck", "kick"), every trait found, and
what the label was made from. It is pure functions of the facts it is told (`TrackFacts`: plain values, no Qt), as
harmony is: the same project always gives the same labels, and the details say why.

### What was weighed

| Approach | Quality | Cost | Verdict |
|---|---|---|---|
| A language model over the project's facts | fluent; knows any plug-in and preset by name | a model to run (or a service to call) at every change; not reproducible, not checkable | not here: an agent over the MCP server can do it on top of these labels and their details |
| The track's name alone (what a DAW shows) | says nothing of "3 Serum 2" or "# Audio", most tracks' names | none | where it starts |
| Rules over everything a producer would look at, each source weighed, combined over the project | right wherever names, presets, notes or effects say; the evidence shown | a few milliseconds a song | **chosen** |

### Where the words come from, and how much each weighs

| Source | What it says | Nouns | Adjectives |
|---|---|---|---|
| The track's name, if the user gave it ("vocals", "bass bridge") | what the user says it is | 3.0 | 0.7 |
| Its instrument's preset ("PL - Electric Flow", "Core - Trumpet") | what the preset is; a category before " - " counts 0.5 more, the words after it are flavour (adjectives 0.4, materials 0.3) | 2.5 | 0.55 |
| A rack's name ("Supersaw", "Metallic Dist") | the user's (or a preset's) name for what it holds | 2.5 holding the instrument, else 2.0 | 0.6 |
| Its audio files' names, by how much each plays; a sampler's sample | pack codes, tempos and keys left out | 2.5 × its share | 0.5 × its share |
| The instrument's own name ("Originals - Cinematic Soft Piano", "Superior Drummer"), its VST3 category ("Instrument\|Drum") | what a sample library plays | 2.0, 1.5 | 0.45 |
| A built-in synth's envelope | a quick decay to silence plucks; a slow attack, held, swells into a pad | 1.4 | |
| Its notes | the part they play | up to 1.3 | |
| Its effects, its sends, its fader and pan | how it sounds | | 0.15–0.9, by kind and amount |

A noun names a track from a weight of 1.0 (a pattern from 0.8).

### Words ([Words.h](../intelligence/src/labels/Words.h))

`readWords()` splits a name at separators, at camelCase ("BellStab": bell, stab) and between letters and digits
("fill110"), drops checksums, then reads the longest phrase the vocabulary knows at each word (three words at most:
"closed hi hat", "future bass", "one shot"), else a plural whose singular it knows, else a word glued from two or three
it knows ("bassline"). The vocabulary (about 450 words) has:

- instruments, by how specific (rank 4: Closed Hat, Violins, Kalimba, Acapella; 3: Kick, Piano, Vocal, Bell; 1:
  Drums, Strings),
- sounds (Pluck, Pad, Lead, Bass, Sub Bass, 808, Reese Bass, Stab, Supersaw, Riser, Impact, Sweep, Atmosphere, Wind...),
- patterns (Chords, Arp, Line, Loop, Fill, One-Shot),
- adjectives (Soft, Dark, Dirty, Fat, Cinematic, Washed Out...), and genres (Future Bass, DnB, Dubstep...): a genre is
  no sound, so "Future Bass - Shlow" is no bass.

Where a word is read changes what short codes mean: "PL", "BS", "LD", "PD", "KY", "CH" are a preset's categories
(Serum's and most sound designers'), but in a file's name capitals are a sample pack's code or name ("OS_LDNB_...",
"VRB2_...", "DECODEDDRUMBASS"). Numbers, keys ("A#min", "Fm"), tempos, filler ("init", "default", "dry", "wet",
"main"...) and sample labels ("Zenhiser", "KSHMR") say nothing. Other words it doesn't know are kept apart: names of
things ("Missme", "Dynasty"), which tell tracks apart when nothing else does.

### Effects ([Effects.h](../intelligence/src/labels/Effects.h))

What a device is (`classify()`): a built-in one by its kind; an instrument as such; a plug-in by its name: first a few
whose names mislead or say nothing (Trackspacer ducks, soothe2 tames resonances, Crystalline and Supermassive are
reverbs, SPL De-Verb takes reverb away, kHs Tape Stop glitches), then the words of names, as a producer reads them
("verb": a reverb; "dist": a distortion; "delay", "echo": a delay; "comp": a compressor; "satur", "tape", "tube":
saturation; "crush": lo-fi; "chorus", "flang", "phase", "filter", "pitch", "imager", "wide"...; short ones only as
whole words: "eq", "ott", "amp"), then its VST3 category ("Fx|Reverb"; many plug-ins say only "Fx").

How much it does (`readDevice()`), from its parameters where they are known: a built-in device's from the project; a
plug-in's from the engine while it is loaded (only those whose names the labeller reads: "Mix", "Dry/Wet", "Wet",
"Feedback", "Drive", "Depth", "Ratio"...: `readsParam()`), else from its saved state, where many keep them as text
(`paramsInState()`: Valhalla's `Mix="0.5"`, JUCE's `<PARAM id="mix" value="0.5"/>`). A value's share comes from its
text if it says percent ("35.0 %") or decibels, else from its normalized value.

| Effect | Trait (weight) |
|---|---|
| Reverb | half wet or more: Washed Out (0.9); a quarter: Spacious (0.65); a tenth: Roomy (0.35); how wet unknown: Spacious (0.45); a shimmer: Shimmering |
| Delay | Echoing: 0.45, more the wetter (+0.25 from 35 % wet) and the longer it feeds back (+0.1 from 70 %) |
| Distortion, saturation, amp | Distorted (0.55–0.8 by drive), Saturated (0.5), Overdriven |
| Lo-fi, vocoder, glitch, granular | Lo-Fi (0.75), Vocoded (0.75), Glitchy, Granular |
| OTT (Xfer's, Over The Top) | Squashed, by depth: 0.75 from 60 %, 0.5 from 30 % |
| Compressor | Squashed from 8:1; else barely Compressed (0.15) |
| Sidechain, volume shapers | Pumping (0.7); Trackspacer: Ducked |
| Width (a utility's width, imagers, wideners) | Wide (0.55–0.6), Mono |
| A built-in EQ's cuts | a high cut at 1.5 kHz or below: Muffled; at 4 kHz: Dark; a low cut from 400 Hz: Thin; both: Telephone (0.7). Cleaning the lows says nothing |
| Chorus, flanger, phaser, tremolo | Chorused, Flanged, Phased, Pulsing |
| Pitch correction | Auto-Tuned (0.6) |
| A built-in synth's filter | Dark, Mellow, Resonant |

Off, or dry (under 3 % wet): nothing. A send to a return whose effect is a reverb counts as the track's own reverb
(from -3 dB: Washed Out; from -12 dB: Spacious), to a delay as Echoing. A fader at -20 dB or below: Quiet; a pan past
60 %: Hard-Left or Hard-Right.

### Notes ([NoteProfile.h](../intelligence/src/labels/NoteProfile.h))

The notes a track is heard playing, where they play, as a profile: how many, the 5th to 95th percentile pitches, the
median pitch by time, the median length, how many sound at each onset (starts within a 32nd are struck together), the
share of onsets with three notes or more, onsets per beat while anything sounds.

| Notes | Part |
|---|---|
| three or more struck together (or 2.6 sounding on average) | Chords; held two beats or more: a Pad; short: a Stab |
| one at a time, the median below E1 (40) | Sub Bass; below F2 (53): Bass |
| one at a time, short and fast over more than a fifth | Arp |
| one at a time from C3 | a Lead (its Line) |

Notes name what nothing else does (an init Serum's sound), never over what an instrument is: a trumpet's notes are a
trumpet's, a drum's are no chords.

### Putting it together ([TrackLabels.h](../intelligence/src/labels/TrackLabels.h))

A track's best instrument, sound and pattern (by weight, then rank, then which came first) make its nouns: [a synth's
name] [instrument] [sound] [pattern], no word twice: "Serum Pluck Arp", "Bell Stab", "Drum Loop". A synth's name
always goes first ("Serum", "Synth"); another instrument's only when nothing else says what it plays ("Kontakt
Chords"; but "808", not "Sampler 808"). Patterns that say nothing more go (a pad's chords, a lead's line, a pluck
played one note at a time); a synth's family is its sound's ("Serum Vocal Lead" is a synth); nature sounds are an
ambience ("Wind Ambience"). Then at most two traits, one of each group (space, echo, drive, dynamics, width, tone...),
the first from 0.4, a second only from 0.55: "Washed Out Serum Pluck", "Echoing Distorted Serum Pluck". What a sound
is made of goes next to its noun ("Soft Acoustic Kick"). With nothing to name it by: its main file's own words
("Rousey Thing"), else "Audio", "MIDI", or "Empty Audio" (no clips, no instrument).

A group is named by what it holds: by its name if the user's says ("synths": Synth Group); else by the family of 60 %
of its tracks (Drum Group), by two families making 80 % (Synth & Bass Group); a group holding 60 % of the song's
tracks is its Mix Bus; else a Mixed Group; with a trait of its own effects (Pumping Drum Group). A return is named by
its effect (Reverb Return: how wet its reverb is says nothing on a return, which is all wet); the master is Main (as its header calls it),
its effects in its details.

### The project

- **Shared traits weigh less.** A trait on k of n tracks keeps 1 - 0.65 (k - 1)/(n - 1) of its weight (0.35 when all
  have it): the same reverb on every track doesn't make every label Spacious, but the one track drowned in it still
  says so. (It stays among each track's traits.)
- **Tracks alike are told apart**, each way in turn while they are still alike: by a trait one has that the others
  lack, by how much it says of the track itself however many share it (from 0.35: a preset's flavour words aren't
  enough; of six arps alike, the quiet ones are Quiet); by their register (the highest High, the lowest Low, if 7
  semitones or more apart); by their presets' own names, their files' own words or the user's names ("Serum Pluck
  (Dynasty)", "Riser (Stratosphere)"); groups by what they hold ("Drum Group (Kick, Snare)"); else numbered in the
  project's order ("Kick 2").

### How well it works

On real projects (14, about 340 tracks), as they open:

| The track | Its label |
|---|---|
| "# Serum 2": preset PLUCK - Dynasty in a rack "Fast Simple Arp - Octave shift random", ValhallaSupermassive half wet, fast notes over two octaves | Washed Out Fast Serum Pluck Arp |
| "# BBC Symphony Orchestra": Core - Trumpet; Core - Basses | Orchestral Trumpet; Orchestral Double Bass |
| "# Originals - Cinematic Soft Piano": preset Washed Out, chords | Washed Out Piano Chords |
| "# Kontakt 7" in a rack "Grand Piano with Pad" | Grand Piano Pad |
| "# Serum 2": BS - Dirty Fat BASS | Dirty Serum Bass |
| "# Serum 2": LEAD - Sounds Like a Vocal Shot, through a delay | Echoing Serum Vocal Lead |
| "# C_BellStab1" through kHs Delay | Echoing Bell Stab |
| "# OS_LDNB_174_Amaj_Sad_Piano_Line" | Sad Piano Line |
| "# VRB2_Missme_Acapella_DRY_120_A#min" through OTT at 77 % | Squashed Acapella |
| "# 91V_FRF_174_bass_synth_club_greedy_Fm" | Synth Bass |
| "# Cymbals_ClosedHiHat3", "# ct_clhat_quick" | Closed Hat |
| two risers | Riser (Stratosphere), Riser (Sustained) |
| a group of a kick, a snare and a chime | Drum Group (Kick, Snare) |

What it gets wrong is mostly where nothing says: a Kontakt instrument keeps no name in its state, an init Serum playing
a line may be a lead or a pluck, a take recorded here is a "Recording". Speed (a release build, an Intel Core Ultra 7):
51 tracks with 1 080 notes, 1.7 ms; 83 tracks, 3.5 ms; gathering the facts takes about as long again (a saved state is
decoded once while it stays the same).

### What would make it better

- An agent over the labels and their details (through the MCP server: [app/mcp.md](app/mcp.md)): to name a track better, or to answer "which is the
  lead?".
- Sound: the similarity's fingerprint of a track's audio (or of its frozen render) as one more source; a kick sounds
  like a kick whatever its file is called.
- More plug-ins' presets ([engine/plugins.md](engine/plugins.md#preset-names)) and their states' amounts.
- Words: the vocabulary is a table; a word a user's projects keep using is a line in Words.cpp.

### Track labels: the application side

`TrackLabels` ([TrackLabels.h](../app/src/intelligence/TrackLabels.h), `Session.trackLabels`) lives on the
application's thread. It tells the labeller what the project holds (`facts()`): each track (its tracks, returns and
master, in the arrangement's order) with its name and whether the user gave it (`namedByUser()`: not one SUBstation
gave after what it holds, its number, a file, an instrument, or a copy's " 2"); its devices and racks: a built-in
device's parameters with their names, units and normalized values; a plug-in's name, vendor, VST3 category (from the
plug-in index), its preset (while it is loaded, as the plug-in says now, `EngineBridge::pluginPresetName()`, even ""; else as its
saved state in the project says, `presetNameFromPreset()`: a loaded plug-in's saved state is only the last save's) and the parameters the labeller reads (the engine's while it is loaded, else
`paramsInState()` of its saved state; a saved state is decoded once while it stays the same); a sampler's sample; the
audio clips it plays (their files, their lengths in beats; not deactivated ones), its notes where they play
(`Clip::heardNotes()`), its sends, fader, pan and mute.

A change to what the labels depend on (tracks, clips, devices, their parameters and states, freezing, the tempo; a
plug-in's preset, its parameters edited in its editor, plug-ins loaded; the plug-in scan) marks them stale and,
`kSettleMs` (250 ms) after the last change of a burst (a knob dragged), emits `changed` (and `revision` goes up).
Values a plug-in's automation moves don't: it says so at every poll while it plays, and the labels read values as they
are whenever they are made. The track headers ask again only on `changed` (and for a new track), never at every refresh. They are made again the next time anything asks, on the application's thread: `label()`,
`toolTip()` (the label, then its details, a line each: the track header's `nameToolTip`, in the info view), and
`describe()` (every track for agents: id, name, kind, parent, label, family, role, traits, details).

### Finding tracks

`TrackSearch` ([TrackSearch.h](../app/src/intelligence/TrackSearch.h), `Session.trackSearch`) is the arrangement's
find bar's (Q: [ui/arrangement.md](ui/arrangement.md#finding-tracks)). A query matches a track when every one of its
words (split at what isn't a letter or a digit, case folded) is in what is searched. `find(project, labels, query)`
ranks the matches, the best first:

1. the track's name has every word at the start of one of its words (names split as `wordsOf()` splits them: at
   separators, between a lower-case letter and a capital, between letters and digits: `BellStab808`);
2. its label has every word at the start of one of its words (`Orchestral Horn`);
3. its name has every word anywhere (`horn` in `Thorn`);
4. its name, label, family, role, traits and details together have every word at the start of one of their words
   (`horn` in a BBC Symphony Orchestra's `Core - Horns`; `bbc horn` across the name and the preset);
5. those have every word anywhere.

Within each rank, the arrangement's order: its tracks top to bottom (those folded away in groups too), the returns,
the master. `query` (setting it makes the best match `current`), `matches`, `count`, `current`, `currentTrackId`;
`next()` and `previous()` step through them, wrapping around. The matches are worked out again on `TrackLabels::changed`
(a rename, a preset picked) and before each step, staying on the track shown if it still matches (else on the best).

From the clip view the search has an area: `setArea(trackId, clipIds)` (the clips as the clip view is given them:
ids on `trackId`, or `{trackId, clipId}` maps) makes it the beats from their earliest start to their latest end
(`clearArea()`: none). `clipInArea(trackId)` is the track's clip overlapping it most (the earliest of equals; only
touching it is not overlapping), "" for none: the find bar opens that clip in the clip view, or shows the track in
the arrangement.

## Files

### The module (`intelligence/src`)

| File | What it holds |
|---|---|
| [core/AudioReader.h](../intelligence/src/core/AudioReader.h) | `readMono()`: decodes a file (or a part) with miniaudio, mixed down, at most 48 kHz; `readMonoAt()`, at a given rate; `resampleMono()` (band-limited, Kaiser-windowed sinc, polyphase; cancellable); `MonoAudio`, `AudioError` |
| [similarity/FeatureSchema.h](../intelligence/src/similarity/FeatureSchema.h) | `Aspect`, `AspectWeights`, `FeatureInfo`, `FeatureSchema` (an extractor's features, weights, and what saved fingerprints must match: `key()`) |
| [similarity/FeatureExtractor.h](../intelligence/src/similarity/FeatureExtractor.h) | `FeatureExtractor` (`extract()` from PCM, `extractFile()`; cancellable), `SoundBuffer`, `Extraction`, `ExtractorFactory` (the schema, and how to make an extractor), `defaultExtractorFactory()` |
| [similarity/EssentiaExtractor.h](../intelligence/src/similarity/EssentiaExtractor.h) | `EssentiaExtractor` (one per thread), the fingerprint's layout (`feature::`), `featureInfo()`, `essentiaSchema()`, `kFeatureVersion`, `kAnalysisRate` |
| [similarity/Descriptors.h](../intelligence/src/similarity/Descriptors.h) | (internal) preparing a sound, its 2 ms envelope, the level after the peak, onsets, the pitch's summary |
| [similarity/Similarity.h](../intelligence/src/similarity/Similarity.h) | `FeatureStatistics` (`measure()`: winsorized spreads), `Comparison` (`fit`, `distance`, `similarity`) |
| [similarity/SoundStore.h](../intelligence/src/similarity/SoundStore.h) | sound-index.bin: `StoreWriter`, `writeStore()`, `readStore()`, `StoreContents` |
| [similarity/SoundIndex.h](../intelligence/src/similarity/SoundIndex.h) | `SoundIndex` (its threads; `find()`, `cancelSearch()`, `schema()`), `SoundQuery`, `SimilarityResult`, `SoundIndexStatus`, `SoundIndexOptions` |
| [third_party/essentia](../intelligence/third_party/essentia) | Essentia 2.1-beta5's core and 29 algorithms (the `essentia` library), [VERSION.txt](../intelligence/third_party/essentia/VERSION.txt), [vendor.sh](../intelligence/third_party/essentia/vendor.sh), [local-changes.patch](../intelligence/third_party/essentia/local-changes.patch) |
| [harmony/Chords.h](../intelligence/src/harmony/Chords.h) | `Quality`, `Chord` (its notes, its name: "Am7", "C/E"), `Key` (its scale, its triads), `pitchClass()`, `noteName()` |
| [harmony/ChordInference.h](../intelligence/src/harmony/ChordInference.h) | `Note`, `ChordSpan`, `InferenceOptions`, `Harmony`; `estimateKey()`, `inferHarmony()` |
| [harmony/Accompaniment.h](../intelligence/src/harmony/Accompaniment.h) | `GeneratedNote`; `chordPart()`, `bassPart()`, `starterProgression()` |
| [humanize/VelocityModel.h](../intelligence/src/humanize/VelocityModel.h) | `VelocityModel` (`humanize()`, `predict()`), `Note` (a part's, `target` or context), `Meter`, `ModelError` |
| [third_party/humanbro](../intelligence/third_party/humanbro) | HUMANBRO's C++ runtime (the `humanbro` library): `humanbro::Humanizer`, its features and tree walker |
| [models/velocity.hbm](../intelligence/models) | The velocity model (HUMANBRO's quantized one); [models/README.md](../intelligence/models/README.md) says where it came from and how to replace it |
| [labels/Facts.h](../intelligence/src/labels/Facts.h) | `TrackFacts` (a track as the labeller is told it: `DeviceFact`, `ParamFact`, `NoteFact`, `ClipFact`, `SendFact`), `TrackLabel` (its label, family, role, traits, details) |
| [labels/Words.h](../intelligence/src/labels/Words.h) | `readWords()` (a name's words, by `Source`), `splitWords()`, `Word`, `WordKind`, `Family`, `familyId()`, `familyTitle()`, `titleCase()`; the vocabulary (Words.cpp) |
| [labels/Effects.h](../intelligence/src/labels/Effects.h) | `Effect`, `classify()`, `readDevice()` (`Trait`s and a summary), `findParam()`, `fraction()`, `readsParam()`, `paramsInState()` |
| [labels/NoteProfile.h](../intelligence/src/labels/NoteProfile.h) | `NoteProfile`, `profile()`, `readNotes()`, `describe()`, `noteName()` |
| [labels/TrackLabels.h](../intelligence/src/labels/TrackLabels.h) | `labelTracks()`: a project's tracks' labels |
| [tagging/SoundTagger.h](../intelligence/src/tagging/SoundTagger.h) | `Kind`, `Attribute`, `infer()` (the model: the embedding and the outputs, `output::`), `SoundTags` and `describe()` (a fingerprint read: kinds, loop, attributes, words, note), `wordDegree()`, `kindThreshold()`, `modelId()`, `noteName()` |
| [tagging/TaggerModel.inc](../intelligence/src/tagging/TaggerModel.inc) | The model's weights, scaling, thresholds, each kind's percentiles and each word's cuts (generated by [tools/sound_tagger/train.py](../tools/sound_tagger/train.py)) |
| [tagging/SoundWords.h](../intelligence/src/tagging/SoundWords.h) | `Meaning` and `meaningOf()` (what a word asks of a sound), `WordQuery` (a search's words, heard in sounds), `labelWords()`, `label()`, `wordTitle()` |
| [tagging/SoundTagTable.h](../intelligence/src/tagging/SoundTagTable.h) | `SoundTagTable`: a library's tags by the hash of each path (`pathHash()`, `extend()`) |
| [tagging/ProjectFit.h](../intelligence/src/tagging/ProjectFit.h) | `ProjectFit`: how well a sound fits a project's sounds and key; `ProjectKey` |

What the module needs of the operating system (file stamps, `readFile()`/`writeFileAtomically()`, background priority,
path keys, WTF-8 to UTF-16) and the stores' fields and checksum (`ByteWriter`, `ByteReader`, `fnv1a()`) are the
platform layer's, which the browser and the engine share: [platform.md](platform.md).

### The application side (`app/src/intelligence`)

| File | What it holds |
|---|---|
| [SoundSimilarity.h](../app/src/intelligence/SoundSimilarity.h) | `SoundSimilarity` (`Session.similarity`): the index on the application's thread; `setLibrary(FileIndex*)`, `find()`, `cancel()`, `found(SimilarSounds)`, `progressChanged`; `SimilarSounds` (a result: `similarity(path)`, `best(n)`, `scorer()`) |
| [Harmony.h](../app/src/intelligence/Harmony.h) | `Harmony` (`Session.harmony`): the song's notes (`songNotes()`), its chords and key inferred from them when asked after a change, the key (the project's, else inferred), `shown` (the setting C toggles) |
| [Humanizer.h](../app/src/intelligence/Humanizer.h) | `Humanizer` (`Session.humanizer`): the velocity model loaded on first use (`velocityModelPath()`, `velocityAvailable`), `velocities(targets, amount)` with each track's notes as context, `statusMessage` |
| [TrackLabels.h](../app/src/intelligence/TrackLabels.h) | `TrackLabels` (`Session.trackLabels`): the project's facts (`facts()`, `namedByUser()`), its labels made again when asked after a change, `label()`, `toolTip()`, `describe()`, `changed`, `revision` |
| [SoundSuggestions.h](../app/src/intelligence/SoundSuggestions.h) | `SoundSuggestions` (`Session.suggestions`): the project's sounds and key as a `ProjectFit`, `fit()` for the browser's Best sort, `summary` ("Techno, Punchy · A Minor"), made again after the project, the key or the tags change |
| [TrackSearch.h](../app/src/intelligence/TrackSearch.h) | `TrackSearch` (`Session.trackSearch`): the find bar's matches, names first, then labels (`find()`), `query`, `next()`, `previous()`; the clip view's area (`setArea()`, `clipInArea()`) |

A plug-in's preset's name is the engine's to read ([engine/plugins.md](engine/plugins.md#preset-names)): the
application asks the bridge for it, or reads a project's saved state with the engine's `presetNameFromPreset()`.

The browser's side of Find Similar is in [BrowserController](../app/src/browser/BrowserController.h)
(`findSimilar()`, `clearSimilar()`, `similarTo`; leaving the similar list cancels its search) and the backend's
`Sort::Score` ([browser.md](browser.md)).

## The index

### Threads

| Thread | Priority | Does |
|---|---|---|
| keeper | background | reads sound-index.bin at start; takes the library from its source after `libraryChanged()` (at most once a second, `refreshSeconds`); saves 10 s after fingerprints change, and on `close()` |
| analysers (a quarter of the cores, 1–4; `threads`), each with its own extractor, made on its thread | background | new files first: stamp, decode (at 44.1 kHz), fingerprint; then the saved ones: their stamp (size, last-write time) compared, analysed again if it changed. Files are checked again whenever the library is taken again, if their last check is more than a minute old (`recheckSeconds`): a sample exported again over itself is analysed again |
| search | normal (the user waits) | the sound's fingerprint (its saved one if a whole library file and up to date; else analysed now, its extractor made the first time, and kept), then every analysed library file compared with it |

Closing stops the analysers in the middle of a file (extractors look at a flag between frames, and while resampling):
the file stays to analyse next time, neither analysed nor failed. Searches are stopped the same way when a newer one replaces them or
`cancelSearch()` drops them (`SoundSimilarity::cancel()`, when the browser leaves its similar list).

Background priority is the browser indexer's: `THREAD_MODE_BACKGROUND_BEGIN` on Windows (CPU, I/O and memory
priority), the lowest nice value and the idle I/O class on Linux. Playback and the UI never wait on it.

The sound tagger runs inside the extractor, on whichever thread makes a fingerprint. Each analysed entry's tags
(`tagging::SoundTags`, read from its fingerprint by `describe()`) are kept beside it, made when it is analysed or read
back from the store; `tags()` copies the library's into a `SoundTagTable` (without the lock while it builds the
table), the same table while nothing changed; `tagsOf(path)` gives one file's, by its key.

Nothing of the module's ever calls into the application but the wake callback, which (as the browser's) is called
once until the next `take()`, and only posts: `SoundSimilarity` takes on its own thread, then emits `found` (a
search's result, only the latest one's) and `progressChanged` (at most four times a second while analysing).

### The library

The application sets a *source*: a function the keeper calls to get the library's paths. `SoundSimilarity` makes it
from the browser's index (`setLibrary(FileIndex*)`): the latest `browser::Snapshot`'s files, folder by folder (which
keeps the analysers on one part of the disk at a time), in the browser's own form (`Snapshot::join`), and not again
while the snapshot's version is the same. So nothing is indexed by hand, and nothing walks the disk twice: what the
browser finds is analysed soon after, what goes is dropped from searches. `setLibrarySource()` waits for a call to the
old source to end: once it returns, the old one is never called again (the browser lets go of it before its index
closes).

Files are told apart by `platform::pathKey()`, as the browser's keys are: on Windows the path with backslashes in
Windows' own lower case, so a clip's `c:/drums/KICK.wav` is the library's `C:\Drums\Kick.wav` (its saved fingerprint is
used, and it isn't among its own best matches); elsewhere the path as it is. A file keeps the library's spelling,
which is what results are looked up by. The keys are made on the keeper's thread before it takes the lock, so a large
library's refresh holds up searches and analysers only for the lookups.

### Searches

`find(SoundQuery{path, start, length})` replaces a waiting search and stops a running one. The result (`SimilarityResult`) holds every
analysed library file's similarity, looked up by path (an open-addressing table of the paths' 64-bit FNV-1a hashes:
no path is copied), the 256 best (not the sound itself), how many were scored, and why the sound couldn't be analysed
if it couldn't. A search right after start waits for the saved fingerprints, and for the first library if it is on
its way (at most 3 s).

The statistics the features are measured by (each one's spread, winsorized) are the library's, as last measured, or
as read with the fingerprints at start. A save measures them again if fingerprints changed since (so they are at most
10 s old); a search, once fingerprints changed (were analysed, or analysed again) for more than a tenth as many files
as they describe, or the library grew or shrank by more than a tenth: so the first run's early searches don't keep the
scale of a few files, and searches measure in the same scale from one run to the next. Until a library is taken (a search right after start), the saved ones stay.
They are measured on at most 20 000 of the library's fingerprints, picked evenly and copied under the lock, and
measured without it (2 ms for 2 000 files, about 25 ms at the cap): the analysers, the application's `take()` and
other searches aren't held up.

### The store

`sound-index.bin` in `localDataDir()` (`%LOCALAPPDATA%\SUBstation` on Windows, `~/.local/share/SUBstation`
elsewhere), or `SUBSTATION_SOUND_INDEX` (the tests set it). Written through `.tmp` and moved over the old one. It
keeps the library's files (analysed, or not analysable); the files that left it in the last 90 days (`keepDays`: each
file records when it was last in the library), so a place on a drive that is unplugged for a while, or removed and
added again, isn't analysed again when it comes back; and the 1 000 sounds outside it searched from most recently.
Files gone for longer are dropped at the next save.
The format is in [SoundStore.h](../intelligence/src/similarity/SoundStore.h). Its header has the key of what made the
fingerprints (`FeatureSchema::key()`, an FNV-1a hash of the extractor, its version, its settings, Essentia's version
among them: the analysis rate, frame and hop, bands..., and its features) and their size. A store of another extractor, version or settings, of another
format, or with a bad checksum, is ignored and everything is analysed again: bump `kFeatureVersion` whenever what
`EssentiaExtractor` computes changes. A file is told by its path and its stamp (size, last-write time): a changed stamp
and it is analysed again; files of the library not in the store are analysed, and only they (incremental indexing). The
library's statistics are saved after the files.

Memory and disk: about 830 bytes a file (181 features: the descriptors, the tagger's embedding and outputs; the path,
the stamp; and its tags, about 100 bytes): 1.7 MB for 2 000 files, 4 MB for 5 000, about 250 MB for 300 000. (Before
the tagger, 400 bytes.)

## The application side

`Session` makes the `SoundSimilarity` before the browser and hands it to the `BrowserController`
(`Options::similarity`), which points it at its index and offers *Find Similar Sounds*. On shutdown the browser lets
go of it first, then the session closes it (saving the fingerprints). `Session::Options::analyseSounds` turns the
background analysis off (the tests' sessions list the user's Music folder): searches then only analyse the sound
searched from.

A `SimilarSounds` keeps its result alive; `scorer()` is what the browser's search sorts by, `best(n)` what the sampler
and the drum rack will step through to swap in similar sounds, and `similarity(path)` takes Qt's paths.

`SoundSimilarity::tags()` is the library's tag table as last taken: at once when the saved fingerprints are read,
then, while fingerprints or the library change, at most every 1.5 s (`kTagsMs`), made on a thread of its own and
handed over with `tagsChanged`; the browser searches and labels with it ([browser.md](browser.md)). `tagsOf(path)`
gives one file's tags however its path is spelt.

### Sound suggestions: the application side

`SoundSuggestions` ([SoundSuggestions.h](../app/src/intelligence/SoundSuggestions.h), `Session.suggestions`) lives
on the application's thread. Its project's sounds are the files its tracks' audio clips play (the file a reversed
clip was made from) and its samplers' samples, each once, as the sound similarity tags them (`tagsOf()`); its key
`Session.harmony`'s (the project's, or inferred). A change to the tracks, clips, devices or their state, the key, or
the tags makes them again 300 ms after the last of a burst (`kSettleMs`), then `changed`: the browser searches a list
under Best again. `fit()` is what the Best sort weighs, each file's fit worked out the first time a search asks for
it and kept until the next change; `summary` is what the browser's footer says the project is ("Techno, Punchy · A
Minor").

### Harmony: the application side

`Harmony` ([Harmony.h](../app/src/intelligence/Harmony.h)) lives on the application's thread. Its notes are those the
song's MIDI tracks play (`songNotes()`): of every MIDI track heard (not muted, nor in a muted group) whose name doesn't
say it plays drums ("Drums", "Kick", "Snare", "Hats", "Perc", "Claps"...: `isDrumTrack()`), what its clips play, where
they play it on the timeline (nothing of a deactivated clip, nor deactivated notes: `Clip::heardNotes()`). Audio tracks are for later. The bars are the project's time signature's; the key its
key, if it has one.

A change to what it hears (the clips of a track it hears; which tracks those are, as tracks come, go, are muted,
renamed or grouped; the time signature, the key; a new project) marks the result stale and, 40 ms later, emits
`changed` once for every change in that time (a drag's edits). A fader, the loop, the tempo or an audio track change
nothing: what they change is compared first (`heardTracks()`, the time signature and the key). The
chords and key are inferred again only when asked for after that, on the application's thread: nothing runs in the
background, and the session asks for the key each time (it hands it to the engine for Hue's Auto: `Engine::setSongKey`). The piano roll asks when it hears `changed` (and when it opens
a clip or shows), maps the chords over the part its clip plays into the clip's own beats, and draws them; Generate asks
too. `shown` (View › Chords and Key, C) is a setting (`pianoroll/show_harmony`), on at first.

## Invariants

- The module includes nothing of Qt, the application, the UI, the engine or the browser (`ctest -R boundaries`); it
  decodes through the `miniaudio` library target, not through the engine.
- Its threads never call into the application but the wake callback, which only posts; the library source is called
  on the keeper's thread and must only read immutable data.
- Only the latest search's result is handed out, once.
- A fingerprint depends only on the sound: not on its level, its sample rate, the silence before it, or the library
  (the library only sets the scale searches measure in).
- Saved fingerprints are used only by the extractor that made them, as it was (its name, version, settings and
  features); nothing else is ever compared.
- Essentia runs only on the index's threads (one set of its algorithms per thread; its factories filled once,
  `essentia::init()` under `std::call_once`), never on the audio thread; its logger is compiled out.
- Harmony is pure functions of the notes and options: the same song always gives the same chords, key and parts.
  It keeps nothing and starts no thread; the application infers it on its own thread, only when asked after a change.
- Humanized velocities are a pure function of the part's notes, the meter and the amount: the model never sees a
  velocity (only the targets' mean, which levels it), and the same part always comes out the same. Only targets
  change. The vendored runtime (`intelligence/third_party/humanbro`, outside the boundary check's folders) includes
  nothing of Qt either.
- Sound tags are pure functions of a fingerprint and the model compiled in: the same sound always has the same tags.
  The model sees only the descriptors the similarity makes; it decodes nothing and starts no thread (it runs where the
  fingerprint is made, on the index's threads). Dirt-Samples, the similarity's and the tagger's benchmark, is never
  trained on.
- Track labels are pure functions of the facts: the same project always gives the same labels, and every label says
  what it was made from (its details). Nothing is read from disk, kept between calls or run in the background; the
  application gathers the facts on its own thread, only when asked after a change.

## Extending it

- **A kind of sound**: its name in `KINDS` (corpora.py, tools/sound_tagger) and labelled sounds of it in a corpus,
  `Kind` and `kindName()` (SoundTagger.cpp, in the model's order), its words in `vocabulary()` (SoundWords.cpp), and a
  sidebar entry in `soundCategories()` if it deserves one; then train ([tools/sound_tagger/README.md](../tools/sound_tagger/README.md)).
- **A word people call sounds**: its tags in `WORDS` (corpora.py) and a blend in `vocabulary()`, then train; it is
  used once it is learned (held out, AUC ≥ 0.7), and its blend's attributes stand in for it until then.
- **A mood or a description** (no data): a line in `vocabulary()`: a `mood()` orders, a `blend()` filters.
- **Retraining**: train.py writes TaggerModel.inc and the tests' TaggerReference.inc together; a new model's
  `modelId()` changes the fingerprints' schema, so libraries are analysed again once.

- **A word for the labels**: a line in `kEntries` (Words.cpp): its kind, family, rank, how it shows, and where it is
  read (a preset's category codes in presets only). Filler goes in `kNoise`.
- **A plug-in the labels misread**: an effect whose name misleads, a line in `kNameRules` (Effects.cpp) before the
  general words; an instrument whose name doesn't say what it plays, or that is a synth, a line in `kInstruments`
  (TrackLabels.cpp).
- **A trait**: a case in `readDevice()` with its weight and group (`traitGroup()` for adjectives that say the same),
  and its parameters' names in `readsParam()`'s lists.

- **A new feature**: add it to `feature::` and `featureInfo()` (its name, aspect and minimum spread: 5–10% of its
  spread over a library), compute it in `EssentiaExtractor` (an Essentia algorithm not vendored yet: add it to
  `vendor.sh`'s list and run it), bump `kFeatureVersion`, and run `sound_similarity_bench --split` on a labelled
  library to see that it helps on both halves.
- **Another extractor** (a learned embedding): a `FeatureExtractor` with a schema of its own, handed to the index as
  `SoundIndexOptions::extractor` (an `ExtractorFactory`: the schema, and a function making one extractor, called on
  each thread that needs one) and made the default in `defaultExtractorFactory()`. Its aspects' weights in its schema;
  `Aspect::Embedding` is there for it. Saved fingerprints of the previous one are made again on their own. It must
  poll the cancel flag on long work, and be safe to run several of at once. Making one may fail (a model missing):
  the index then analyses nothing and says why (`SoundIndexStatus::error`), and searches that need to analyse their
  sound give the reason as their error.
- **Weights the user picks** (as Sononym's aspects): `SoundIndexOptions::weights` (`AspectWeights`); a search could
  take its own.
- **Swapping similar samples** (the sampler, the drum rack, a file manager): `SoundSimilarity::find()` with the
  sample's file, then step through `SimilarSounds::best()`.
- **Another part of the module** (MIDI generation...): a folder of its own under `intelligence/src`
  beside `similarity/` and `harmony/`, sharing `core/`; its Qt side under `app/src/intelligence`. What it needs to know
  of a song's harmony: `Session.harmony` (`chords()`, `key()`), or `inferHarmony()` on notes of its own.
- **A chord quality**: its intervals and suffix in `Quality` and `qualityInfo()` (Chords.cpp), its prior in `kPrior`
  (ChordInference.cpp), and how thin steps take it in `thinPrior()`.
- **Another part to generate**: a function from `ChordSpan`s to `GeneratedNote`s in Accompaniment.h, a
  `Q_INVOKABLE` on `PianoRoll` like `generateBass()`, and an entry in the clip view's Generate menu.
- **The timing model**: a `TimingModel` beside `VelocityModel` in humanize/ (a part of notes in, each target's start
  offset out), its file as `models/timing.hbm` (copied by intelligence/CMakeLists.txt as velocity.hbm is), a
  `timings()` on `Humanizer` beside `velocities()`, and `PianoRoll::humanizeTiming()` asking it in place of
  `notes::humanizedTiming` (the menu and its amount are there already).
- **Another velocity model** (retrained, or HUMANBRO's unquantized one for played-in parts): export it with HUMANBRO's
  `export_cpp_model.py`, replace `models/velocity.hbm` (the build copies it again), and run `intelligence_tests`
  ("humaniz", "velocity", "melody"...) and `test_humanizer`. A model of another target mode (HUMANBRO's residual
  ones) is refused on loading; a newer runtime goes into third_party/humanbro with its local change kept.

## Tests

- [tests/intelligence](../tests/intelligence) (`intelligence_tests`, Qt-free, the engine tests' harness): reading
  (mono mix, parts, 96 kHz to 48 kHz, at a given rate, broken files) and resampling (pitch, length, no delay, flat to
  the top, a tone from common and odd rates, cancelling); Essentia's spectrum (a sine's centroid and roll-off at its frequency at five rates); the fingerprint (level,
  leading silence and the sample rate don't change it; silence has none; clicks of one to three samples, 8–192 kHz,
  NaN and infinity, truncated and junk files handled; pitch of tones down to 41 Hz and of an 808, noise has none;
  envelopes, spectra, peaks, contrast, shape and change of tones, noise, drums and a loop, a click's flux, high
  partials' complexity; a part of a file as long as what is left of it; onsets of loops and one-shots); kicks, snares, hats, claps, synth stabs, plucks and drum loops finding their own kind; level and length
  not outweighing timbre; cancelling; comparing (clipping, weights); the library's statistics (robust to wild files);
  the schema and its key; the store (round trip, statistics, damage, another extractor, version or setting, a
  fingerprint of the wrong size refused); the index
  (analysing a library, saving and checking stamps, new and changed files, files leaving the library, sounds outside
  it and parts of files, undecodable files, a library file searched from before it was analysed, only the latest
  result, its threads and its source, the statistics saved and searched with, kept until a library is taken and
  measured again as it changes, an extractor that can't be made, another extractor's fingerprints made again, closing
  in the middle of a file, a search replaced or cancelled).
- [tests/intelligence/test_sound_tagging.cpp](../tests/intelligence/test_sound_tagging.cpp) (`intelligence_tests`): the
  compiled-in model against what it gave in PyTorch (TaggerReference.inc, written with it); probabilities that sum to
  one; drums told apart (synthesized kicks, hats, snares) and loops from one-shots; a tone's note, noise's none;
  attributes graded among a sound's own kind (a centroid bright for a kick is dark for a hat); an unsure kind not
  said; words read as kinds, plurals, notes, blends, moods (each blend's weights summing to one, only learned words in
  it); a search's words heard in sounds, moods never leaving any out; labels; the tag table's hashes; how sounds fit
  a project (its key, its sounds); the model's speed. [test_sound_index.cpp](../tests/intelligence/test_sound_index.cpp):
  the index's tags of its library, the same table until something changes, read back from the store without
  analysing.
- [tests/intelligence/test_harmony.cpp](../tests/intelligence/test_harmony.cpp) (`intelligence_tests`): naming chords
  and keys, scales and their triads; the key (cadences in major and minor, the bass telling C → G from E minor, too
  little to tell); chords: triads and sevenths, inversions, a progression with its bass on another track, a melody's
  passing notes, arpeggios, a melody alone, a chord pushed off the beat, rests, the key given, a long song's speed;
  the chord part's voicings and voice leading, striking again at bar lines, the bass, the starter progression.
- [tests/app/test_harmony.cpp](../tests/app/test_harmony.cpp): the song's notes (MIDI tracks heard, not drums, what
  clips play where), following the project once edits settle, the key (the project's or inferred), the bars of the
  time signature, the `shown` setting.
- [tests/intelligence/test_humanize.cpp](../tests/intelligence/test_humanize.cpp) (`intelligence_tests`, with the
  model in the repository): loading it, and refusing a missing, junk or truncated file; predictions independent of
  the notes' velocities and of whole bars before the part, but not of where in the bar notes fall or of the time
  signature; the targets' level kept and the context untouched; the amount (none, half); a melody over chords louder
  than the inner voices; a note alone, a chord alone, notes of no length, no notes; a long part's speed.
- [tests/app/test_humanizer.cpp](../tests/app/test_humanizer.cpp): the model next to the tests (`bin/models`); a
  clip's velocities where it plays on the timeline, at the notes' own level; a track's other clips as context, its
  deactivated notes and clips not; each track keeping its own level; targets whose clip is gone.
- [tests/app/test_ui_pianoroll.cpp](../tests/app/test_ui_pianoroll.cpp): the chord lane over the clip (hidden with
  the key, clicks going through it), notes out of the key in red, Generate › Chords and › Bass; Humanize's menu
  (Velocity and Timing, each with its own amount, the notes getting the keyboard back);
  [test_ui_mainwindow.cpp](../tests/app/test_ui_mainwindow.cpp): C.
- [tests/intelligence/test_labels.cpp](../tests/intelligence/test_labels.cpp) (`intelligence_tests`): names split
  into words; what files' and presets' names say (codes in presets only, a genre no sound, an orchestra's); what
  effects are (built-ins, the words of names, names that mislead, categories) and how much they do (a reverb's mix,
  a delay's feedback, OTT's depth, a utility's width, an EQ's cuts; off, dry); parameters' values (text, decibels,
  saved states); what notes play; labels: a pluck washed out by its reverb, instruments, presets and samples, notes
  naming what nothing else does, audio tracks by their files, the user's own name, the mixer and sends, groups,
  returns and the master, shared traits weighing less, tracks alike told apart; the same facts giving the same
  labels, 200 tracks of 500 notes in good time.
- [tests/app/test_track_labels.cpp](../tests/app/test_track_labels.cpp): what the labeller is told (devices and
  their parameters, notes and files where they play, sends, mute; not a deactivated clip's notes); whose name a track
  has; presets and amounts read from plug-ins' saved states; following the project once its edits settle; what
  agents (`describe()`) and tooltips get. [test_ui_mainwindow.cpp](../tests/app/test_ui_mainwindow.cpp): a track's
  name hovered says its label in the info view, and follows its edits. The engine's side of presets' names:
  [engine/plugins.md](engine/plugins.md#tests).
- [tests/app/test_track_search.cpp](../tests/app/test_track_search.cpp): finding tracks: names first (at the start
  of their words), then labels, then names having the words anywhere, then what the labels were made from (a
  preset's `Core - Horns`), every word wherever it is, the arrangement's order within each; stepping through the
  matches, wrapping around, and following the project; the clip in the clip view's area. [test_ui_mainwindow.cpp](../tests/app/test_ui_mainwindow.cpp): Q, the find bar.
- [tests/app/test_sound_similarity.cpp](../tests/app/test_sound_similarity.cpp): the browser's files analysed; Find
  Similar's list, sort, status, filtering by text and places; ending it (another sort, another list, clearing, Ctrl+F);
  a clip's part of a loop; a sound outside the library and `SimilarSounds`; files found later; a sound that can't be
  analysed; the result menu. Samples found by how they sound (a kick named "Untitled" found by "kick", after those
  named so; its row's label; Samples › Kicks; words the tagger doesn't know matching names only); the Best sort's
  project fit (`SoundSuggestions`: what is like the project's sounds first).
- [tests/app/test_browser_native.cpp](../tests/app/test_browser_native.cpp): the backend's `Sort::Score` against the
  reference.
- The UI: the similar bar ([test_ui_browser.cpp](../tests/app/test_ui_browser.cpp)), an audio clip's *Find Similar
  Sounds* ([test_ui_arrangement.cpp](../tests/app/test_ui_arrangement.cpp)), the browser showing for it
  ([test_ui_mainwindow.cpp](../tests/app/test_ui_mainwindow.cpp)).

## Sources

- Lakatos, "A common perceptual space for harmonic and percussive timbres" (2000); McAdams et al. on timbre spaces;
  Peeters, "A large set of audio features for sound description" (CUIDADO, 2004): log attack time, spectral centroid
  and the temporal descriptors.
- de Cheveigné and Kawahara, "YIN, a fundamental frequency estimator for speech and music" (2002).
- Mehrabi, Dixon, Sandler, "Vocal imitation of percussion sounds: similarity measures for vocal-based drum sample
  retrieval using deep convolutional auto-encoders" (ICASSP 2018): learned features predict drum similarity better
  than MFCCs or temporal descriptors alone.
- Ableton Live 12's *Similarity Search* and *Similar Sample Swapping*; Sononym's aspects (overall, spectrum, timbre,
  pitch, amplitude); Freesound's similarity (Essentia descriptors, PCA, nearest neighbours).
- Engel et al., "Neural Audio Synthesis of Musical Notes with WaveNet Autoencoders" (ICML 2017): NSynth. Cella et
  al., "OrchideaSOL: a dataset of extended instrumental techniques for computer-aided orchestration" (ICMC 2020):
  TinySOL. Ramires et al., "Neural Percussive Synthesis Parameterised by High-Level Timbral Features" (ICASSP 2020):
  the Freesound One-Shot Percussive Sounds. Panayotov et al., "LibriSpeech: an ASR corpus based on public domain
  audio books" (ICASSP 2015). Wilkins et al., "VocalSet: A Singing Voice Dataset" (ISMIR 2018).
- Lakshminarayanan, Pritzel, Blundell, "Simple and Scalable Predictive Uncertainty Estimation using Deep Ensembles"
  (NeurIPS 2017); Guo et al., "On Calibration of Modern Neural Networks" (ICML 2017): the tagger's ensemble and
  its temperature scaling.
- Font, Serrà, Serra, "Class-based tag recommendation and user-based evaluation in online audio clip sharing"
  (Knowledge-Based Systems, 2014): Freesound's tags as labels for what sounds are called.
- Krumhansl and Kessler, "Tracing the dynamic changes in perceived tonal organization in a spatial representation of
  musical keys" (1982): the key profiles; Krumhansl, *Cognitive Foundations of Musical Pitch* (1990).
- Pardo and Birmingham, "Algorithms for chordal analysis" (Computer Music Journal, 2002): chord templates scored
  against segments of notes, and segmenting by the best total.
- Fujishima, "Realtime chord recognition of musical sound" (ICMC 1999): pitch-class profiles against chord templates.
- Sheh and Ellis, "Chord segmentation and recognition using EM-trained hidden Markov models" (ISMIR 2003): smoothing
  chord sequences with a Viterbi search.
