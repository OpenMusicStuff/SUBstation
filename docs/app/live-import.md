# Importing Ableton Live Sets

`io/LiveSet.h` reads a Live Set; `io/LiveImport.h` translates it into a project file's JSON;
`Session::importLiveSet` loads that as a new, untitled project. What the user sees:
[../guide/ableton-import.md](../guide/ableton-import.md).

```
Session::importLiveSet(path)                      (session/SessionFiles.cpp)
  ├─ live::readLiveSet(path)                      gzip (puff) → XML (QXmlStreamReader) → Element tree
  ├─ live::importLiveSet(root, path, {plug-ins})  → ImportResult {project JSON, notes, tracks, clips}
  ├─ loadInto(project, json)                      as a project file loads: repairs, defaults, reset
  └─ untitled, named after the set; projectOpened; statusMessage; information(notes)
```

## Files

| File | What |
|---|---|
| [io/LiveSet.h](../../app/src/io/LiveSet.h) / [.cpp](../../app/src/io/LiveSet.cpp) | `Element` (a tag, attributes, trimmed text, children; `at("A/B")`, `value()`, `number()`, `flag()`), `readLiveSet`, `parseLiveSet`, `gunzip`, `isGzip` |
| [io/LiveImport.h](../../app/src/io/LiveImport.h) / [.cpp](../../app/src/io/LiveImport.cpp) | `importLiveSet`, `ImportOptions` (the installed plug-ins), `ImportResult`; `vstPreset`, `vst2CompatibleState` |
| [engine/src/plugins/vst3/Vst3Ids.h](../../engine/src/plugins/vst3/Vst3Ids.h) | VST3 class ids: from the four words Live stores to `PluginDescription::uid` (`classIdFromWords`), to a .vstpreset's spelling (`presetClassId`), and back (`wordsFromClassId`) |
| [app/third_party/puff](../../app/third_party/puff) | Mark Adler's inflate (zlib licence), vendored unmodified |

## Reading

A set is gzip-compressed XML (plain XML is read too). `gunzip` reads gzip's header, inflates
with `puff` into a buffer as long as the trailer says, and checks the trailer's length and CRC-32:
anything else is "damaged". The trailer's length is taken only as far as deflate can go (1032 bytes
from one): a cut file's last bytes would otherwise ask for gigabytes. `ReadLimits` bounds what a
set may take: 512 MB of XML (the file's length and gzip's are checked before anything is read or
inflated) and 5 million elements kept; past them a set is "too large to read", before it takes all
the memory there is. `parseLiveSet` builds the element tree with `QXmlStreamReader`:

- Element and attribute names are interned (a set has about a million elements of some 1500
  names), looked up by the reader's `QStringView` so no string is made for a name seen before.
- Subtrees the importer never reads are skipped as they are read (`skippedElements()`: the Session
  View's clip slots, frozen tracks' sequencers, MIDI controllers' targets, take lanes, views, the
  groove pool, presets' and browsers' records...): about a third of a set.
- Text is kept only where there is some: plug-in states, in hex.

A 55 MB set (a 3 MB file, some 750,000 elements kept) reads in about 2 s, 0.35 s of it inflating,
and takes about 330 MB.

## Translating

`Importer` makes two passes over `LiveSet/Tracks`:

1. `assignIds()`: an id for every track and return, so routing to a track further down
   (outputs, inputs, sidechains) resolves in the second pass.
2. `run()`: each track, return and the main track translated (`translateTrack`,
   `translateReturn`, `translateMaster`), then their automation.

Every parameter translated that Live can automate is entered in `targets_`: its
`AutomationTarget Id` → (owner track, automation key, a function mapping Live's value to
automation's 0..1). `readAutomation` then puts each `AutomationEnvelope` (by its `PointeeId`) in its
owner's automation; one whose target isn't there is counted in the notes. The tempo's and time
signature's targets are read by `readSettings` instead: their events become the project's tempo map
(`tempo_map`: Live's events are in beats, straight lines between them, as SUBstation's) and time
signature changes (`time_signatures`); tempos past 20 to 999 BPM are held to them, and the notes say so.
Unwarped clips are placed by that tempo map (`song_`: their seconds last what it makes them where they are).

Values Live writes, and what they become:

| Live | Here |
|---|---|
| Mixer and rack chain volume, send level: linear gain (1 is 0 dB, 1.995 is +6 dB) | dB, in the faders' range; automation through `volumeToNormalized` |
| Time signature: `numerator - 1 + 99 * log2(denominator)` | numerator, denominator |
| Colour: an index into Live's 70 colours | `kLiveColors` (its first 13 are `kTrackColors`) |
| Drum Rack pad `ReceivingNote`: `128 - note` | the pad's key (`receive_note`; none: the loader gives it a free one) |
| 32-bit words (class id fields, VST3 `ParameterId`, a VST2's id): signed or unsigned | `liveWord()`: unsigned; a `ParameterId` of -1 is a slot with none |
| Colour: `Color` (Live 11, 12) or `ColorIndex` (Live 10) | `colorOf()` |
| Utility gain, Compressor threshold: linear amplitude | dB |
| Delay times: seconds; feedback, dry/wet: 0..1 | ms; percent |
| VST3 parameters: normalized | the same (their ids are the VST3 `ParamID`s) |
| Built-in devices' parameters (`mapParams`): a `Manual`, or a plain `Value` (a switch, a list), `true`/`false` as 1/0 | each `LiveParam`'s `plain` (fractions to percent, seconds to ms, linear gains to dB, lists by `indexed()` tables), held to the parameter's range |

Clips:

- `clipSpans()` turns a clip's markers into what it plays: from its start marker (`LoopStart +
  StartRelative`) for its length, and round its loop (`LoopStart..LoopEnd`) when `LoopOn`. Warped
  and MIDI clips' markers are in beats, unwarped audio clips' in seconds.
- MIDI notes are written out span by span, cut at a span's end. Clip envelopes are noted once per Live track
  (`noteClipEnvelopes`), not once per pad.
- Samples are looked for once each (`resolvedPaths_`): where the set says, beside it, a folder up.
- Warped audio: `WarpMap` maps content beats to file seconds through the warp markers (straight
  between them, the end stretches' tempo beyond), and back (`beatAt`). When its markers make a clip
  SUBstation can play (`playable()`: seconds rising, every stretch's tempo within
  `kMinSegmentBpm`..`kMaxSegmentBpm`), each span is one clip carrying them (`warp_markers`), held to the
  file. Otherwise each span is cut at the markers in it; each piece is a clip at `60 * beats / seconds`
  BPM, pieces of one tempo that follow each other are joined, held to the file and to that range.
- Fades go on the first and last piece, converted from beats (warped, through the markers) or seconds.

Plug-ins:

- A VST3's class id is `Uid/Fields.0..3`, four 32-bit words (FUID's longs). `classIdFromWords`
  makes the engine's uid of them (the TUID's bytes: a COM GUID's layout on Windows, so the hex
  differs by platform), which finds the installed plug-in (its path, vendor, name). Its settings
  become a .vstpreset (`vstPreset`): Steinberg's header with `presetClassId` (the words as
  `%08X%08X%08X%08X`, the same on every platform), the `ProcessorState` as the `Comp` chunk, the
  `ControllerState` as `Cont`.
- A VST2 (`VstPluginInfo`) becomes the VST2 itself when it is installed (a VST2 in the index whose
  uid is its `UniqueId`, `vst2Uid`), with its chunk as SUBstation keeps a VST2's state: the fxb or
  fxp `CcnK` block `vst2CompatibleState` makes, without its `VstW` header ([engine/plugins.md](../engine/plugins.md#vst2));
  without a chunk, at its defaults. Else it becomes an installed VST3: one whose class id is Steinberg's VST2
  replacement id for it (`"VST"`, the VST2's `UniqueId`, its name: `wordsFromClassId` says) takes
  its settings as `vst2CompatibleState` makes them (`VstW`, then an fxb or fxp `CcnK` block of its
  chunk: what Steinberg's VST2 wrapper and JUCE's VST3s read); else one of the same name
  (`plainPluginName`: lower case, letters and digits, no `x64`) at its defaults.

Live's own devices: Utility, EQ Eight, Compressor, Delay and Simpler/Sampler each have a function of
their own. The others SUBstation has a built-in like are translated by table: a `LiveParam` per
parameter (Live's element under the device, the built-in's parameter id, Live's value in the
built-in's units), which `mapParams` reads, holds to the parameter's range (`builtinSpec`) and enters
in `targets_` with the same mapping, so automation follows. What a table can't say is done beside it
(`setParam`): the legacy Auto Filter's circuit (a list per type), Auto Filter 2's LFO time mode,
Erosion's Mode as Noise Blend and Stereo, Live 10's Redux's switched-off bits, the Arpeggiator's
transposition as octaves. Saturators are told apart by Live 12.1's names (`WsDrive`,
`BassShaperThreshold`): before it the Type list had no Bass Shaper.

| Live (tag) | Built-in | Notes |
|---|---|---|
| `AutoFilter`, `AutoFilter2` | `auto_filter` | the legacy cutoff a MIDI note (`440 × 2^((v − 69)/12)`), ModHub ±127, LfoAmount 0..30; 12.2's types past Morph as Lowpass (noted), its circuits SVF, DFM, MS2, PRD as Clean, OSR, MS2, PRD; the sidechain |
| `Overdrive` | `overdrive` | Drive, Tone and Dry/Wet in percent, PreserveDynamics a fraction |
| `Saturator` | `saturator` | Live 11's types (no Bass Shaper) and 12.1's; PostClip a switch (Soft) or a list |
| `Amp` | `amp` | as it is |
| `Redux2`, `Redux` | `bitcrusher` | Jitter as Dither; Live 10's downsampling as 44.1 kHz over it |
| `Chorus2` | `chorus` | OutputGain linear |
| `Erosion` | `erosion` | BandQ × 4 as octaves, Amplitude ÷ 2; Mode as Noise Blend and Stereo |
| `Gate` | `gate` | Threshold linear; Gain is the Floor; the sidechain, its gain, mix, listen and EQ (not its type) |
| `Limiter` | `limiter` | LinkChannels (before 12.1) as Link 0 or 100 % |
| `MultibandDynamics` | `multiband` | ratios left at 1:1 (noted: Live's encoding of them isn't known) |
| `PhaserNew` | `phaser` | its waveforms reordered; times in seconds |
| `Reverb` | `reverb` | ER spin depth (2..55), diffusion and scale (all-pass gain and size) spread over 0..100 % |
| `Vocoder` | `vocoder` | Live's band counts (4..24, 32, 40) to the list's; an External carrier from `CarrierSource/RoutedInput` as the sidechain (`routedSidechain`) |
| `MidiArpeggiator` | `arpeggiator` | the nearest style, synced rates to the list's, octaves from a 12-semitone transposition |

Some of Live's lists' orders come from its manual rather than a set (Auto Filter 2's waveforms
and circuits, the Arpeggiator's styles and rates): the tables say what they assume.

## The notes

`Notes` collects what didn't come across: plain lines once (`line`), and lists with how many of
each (`listed`: "Left out (SUBstation has no device like them): Saturator (15), Vocoder (7).").
Tracks are named in them as Live shows them ("31-Audio").

## Tests

[test_live_import.cpp](../../tests/app/test_live_import.cpp) writes sets as Live does (gzip or
XML) and checks each part: reading and damage, the song's settings, tracks, groups, returns,
mixers and routing, MIDI loops, audio clips and warping, plug-ins (.vstpreset chunks, VST2
replacement states, a VST2 as itself), Live's own devices (and those translated by table, the
Arpeggiator among them), Drum Racks, automation, and `Session::importLiveSet`.
`test_ui_mainwindow`'s `templatesAndImportingALiveSet` goes through the menu and its dialog.
