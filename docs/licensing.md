# Licensing

SUBstation's own code is under the **MIT licence** ([LICENSE](../LICENSE)). It is built together with third-party
code under licences of its own, one of which, **Essentia** (the sound similarity's descriptors), is the **GNU Affero
General Public License v3** (AGPLv3), and another, **Vital** (the built-in Vital synth's engine), the **GNU General
Public License v3** (GPLv3). Every build includes both, so **a SUBstation you distribute (an installer, an
executable, a zip) is, as a whole, distributed under the AGPLv3's terms** (GPLv3 code may be combined with AGPLv3
code: section 13 of each; the GPLv3's own conditions, which the AGPLv3's include, hold for Vital's part). Your own code stays MIT: anyone may take
it under MIT alone; only the combined program carries the AGPL's conditions.

This page says what that asks of you when you ship SUBstation, and which parts need a decision. It is a summary of
the licences, not legal advice: for a commercial release, have it checked.

## What is in a build

| Component | Where | Licence | How it is used | Compatible with distributing the whole under the AGPLv3? |
|---|---|---|---|---|
| SUBstation | everything not listed below | MIT | — | yes (MIT code can be part of an AGPL work) |
| [Essentia](../intelligence/third_party/essentia) 2.1-beta5 | intelligence/third_party/essentia | AGPL-3.0-or-later (or a proprietary licence from the Universitat Pompeu Fabra) | statically linked: the sound similarity's descriptors | — it is what makes the whole AGPL |
| KISS FFT 1.3.0 | inside Essentia's folder | BSD-3-Clause | Essentia's FFT | yes |
| [Vital](../engine/third_party/vital) 1.0.6's source | engine/third_party/vital | GPL-3.0-or-later (or a commercial licence from Vital's author) | statically linked: the built-in Vital's synthesis engine | yes (GPLv3 combines with AGPLv3). Its name is a trademark: see [below](#to-decide) |
| nlohmann JSON 3.1.2, KISS FFT | inside Vital's folder | MIT, BSD-3-Clause | Vital's presets (KISS FFT is vendored with Vital but not used: KFR's FFT is) | yes |
| [KFR](../engine/third_party/kfr) 7.2.0 (its DFT only) | engine/third_party/kfr | GPL-2.0-or-later (or a commercial licence from KFR's authors) | statically linked: the built-in Vital's FFT, Hue's, and the editors' spectra (`app/src/analysis/Fft.cpp`) | yes (GPLv2-or-later is used under the GPLv3, which combines with AGPLv3; ship its [LICENSE.txt](../engine/third_party/kfr/LICENSE.txt)) |
| TNT | inside Essentia's core | public domain | Essentia's matrices | yes |
| [miniaudio](../engine/third_party/miniaudio) | engine/third_party | Unlicense or MIT-0 | audio I/O, decoding | yes |
| [Signalsmith Stretch, Linear](../engine/third_party) | engine/third_party | MIT | time stretching, FFTs | yes |
| [fastpitch](../engine/third_party/fastpitch) 1.0.0 | engine/third_party/fastpitch | MIT | pitch tracking (pYIN): GigaTune | yes |
| [swift-f0](../engine/third_party/swift-f0) 0.3.0's model | engine/third_party/swift-f0 (its weights in engine/src/builtin/SwiftF0Data.h) | MIT | pitch tracking (SwiftF0): GigaTune's High Quality | yes |
| [VST 3 SDK](../engine/third_party/vst3sdk) 3.8.1 | engine/third_party | MIT | plug-in hosting | yes |
| [vst2sdk](../engine/third_party/vst2sdk) (Xaymar's clean-room VST 2.x interface) | engine/third_party | BSD-3-Clause | VST2 plug-in hosting (its header) | yes (ship its [LICENSE](../engine/third_party/vst2sdk/LICENSE)). It is a reimplementation, not Steinberg's VST 2 SDK, which isn't licensed any more; its README notes it grants no trademark rights: see [below](#to-decide) |
| [puff](../app/third_party/puff) 2.3 (zlib 1.3.1's contrib) | app/third_party/puff | zlib | inflate: reading Ableton Live Sets | yes |
| [LAME](../engine/third_party/lame) 3.100 (its encoding library only) | engine/third_party/lame | LGPL-2.0-or-later | statically linked: MP3 export | yes (the LGPL's relinking condition is met by shipping the whole program's source, as the AGPL asks anyway; ship its [COPYING](../engine/third_party/lame/COPYING)). MP3's patents have expired. |
| Qt 6 | not vendored; linked dynamically | LGPL-3.0 (or GPL, or commercial) | the application and its UI | yes (LGPLv3 is compatible; keep Qt dynamically linked and replaceable, and ship its licence) |
| [HUMANBRO](../intelligence/third_party/humanbro) runtime | intelligence/third_party/humanbro | the project's own (no licence file of its own) | Humanize › Velocity | yes, if it is under the MIT licence too: see [below](#to-decide) |
| [velocity.hbm](../intelligence/models) | intelligence/models | none stated; trained on MAESTRO v3, which is CC BY-NC-SA 4.0 | the velocity model | see [below](#to-decide) |
| [TaggerModel.inc](../intelligence/src/tagging/TaggerModel.inc) | intelligence/src/tagging | none stated (SUBstation's own, trained by [tools/sound_tagger](../tools/sound_tagger)); trained on NSynth, TinySOL, LibriSpeech and VocalSet (CC BY 4.0), Freesound sounds (CC0 and CC BY 3.0 only), and tidal-drum-machines (no licence stated) | the sound tagger's model: weights compiled in, no audio of the corpora | see [below](#to-decide) |
| Steinberg's ASIO® SDK | not in the repository; used if present at build time (the releases download 2.3.4: [releasing.md](releasing.md#asio)) | proprietary *or* GPLv3 (Steinberg's dual licence, since 2.3.4, October 2025) | ASIO on Windows | under its **GPLv3** option, as the releases take it: see [below](#to-decide) |
| VST3 and VST2 plug-ins | loaded at run time | their makers' | hosted | not part of SUBstation: they are separate programs loaded through the VST3 and VST 2 interfaces |

## Shipping SUBstation: what the AGPLv3 asks

When you give or sell someone a build (the AGPL's "convey"):

1. **Licence it as AGPLv3**: say so where you distribute it, and ship the licence texts: Essentia's
   [COPYING.txt](../intelligence/third_party/essentia/COPYING.txt) (the AGPLv3), Vital's
   [LICENSE](../engine/third_party/vital/LICENSE) (the GPLv3), [LICENSE](../LICENSE) (MIT), and the
   third-party notices (each `third_party` folder's licence files, and Qt's). The releases' `licenses/` folder holds
   them, with a `README.txt` saying so and linking the build's source
   ([tools/release/package-windows.ps1](../tools/release/package-windows.ps1)).
2. **Offer the complete corresponding source** of the build: SUBstation's source at the commit built, Essentia
   included (it is in the repository, with its local changes in
   [local-changes.patch](../intelligence/third_party/essentia/local-changes.patch)), and the build scripts. A tag of
   this public repository that matches the build, linked from where you ship it, is the simplest way; or ship the
   source archive with it. Keep that source available for as long as you offer the build.
3. **Add no restrictions**: no licence terms forbidding modifying, reverse engineering or passing it on. You may still
   charge for it.
4. **Network use (section 13)** applies to a modified SUBstation that users interact with remotely over a network
   (run as a service). A desktop DAW used on someone's own computer isn't; if you ever offer it as a service, its
   users must be offered its source too.
5. **Interactive notices (section 5d)**: Essentia shows none, so SUBstation needn't add them; an *About* dialog that
   names the licences is still good practice.

You **cannot** ship a closed-source SUBstation with Essentia in it. If you ever want to, either license Essentia
commercially from the Universitat Pompeu Fabra (Essentia's proprietary licence), or replace the extractor (the
similarity module takes any `FeatureExtractor`: [intelligence.md](intelligence.md#extending-it)), which leaves the rest
MIT.

## To decide

- **The sound tagger's training data.** Its model is weights learned from sounds, none of which ship. The corpora
  were chosen to be free to use: NSynth, TinySOL, LibriSpeech and VocalSet are CC BY 4.0 (credit them where the
  model is shipped, as [tools/sound_tagger/README.md](../tools/sound_tagger/README.md) does), Freesound's sounds are
  those under CC0 or CC BY 3.0 only (its CC BY-NC and Sampling+ sounds were left out). tidal-drum-machines states no
  licence (drum machines' sounds, as collected for TidalCycles); whether a model trained on them may ship is the
  question velocity.hbm's MAESTRO raises, milder. To be certain, train without it (`corpora.py` without `tdm`: the
  Freesound drums then teach the kinds alone) and measure the difference.

- **VST2 hosting.** Steinberg stopped licensing its VST 2 SDK in 2018, so SUBstation hosts VST2 plug-ins through
  Xaymar's vst2sdk, a clean-room reimplementation of the interface (BSD-3-Clause) whose README argues it is legal as
  reverse engineering for interoperability and says to have a lawyer confirm that for a commercial product. What
  SUBstation adds to it (`engine/src/plugins/vst2/Vst2Sdk.h`: the time info's and the event list's layouts, a few
  opcodes' numbers) is the same kind of interface fact, written for interoperability. Neither grants a right to
  Steinberg's trademarks: the About box's "VST is a registered trademark of Steinberg Media Technologies GmbH" stays.
  Building without it would mean taking the VST2 files out of `engine/src/plugins` and `pluginFormat()`.

- **Vital's name.** Vital's source is GPLv3, but its name isn't licensed with it: Vital's README asks that "Vital",
  "Vital Audio", "Tytel" and "Matt Tytel" not be used to market or to name a distribution of binaries built from its
  source, that builds not connect to vital.audio's web services (SUBstation's don't), and that Vital's own presets
  (which come with Vital under a licence that doesn't allow passing them on) not be distributed (SUBstation ships
  none: users load their own `.vital` files). SUBstation's device is called "Vital" in its device list and editor (its
  kind, `vital`, is what projects save). For your own builds that is just a name; before you distribute or market a
  build, decide whether to keep it (asking Vital's author, who also offers non-GPL licences:
  licensing@vital.audio) or to rename the device (`VitalProcessor::name()` in
  [Vital.cpp](../engine/src/builtin/devices/Vital.cpp) and the UI's texts; projects keep working, as they save the
  kind), and don't put the name in the product's marketing.

- **ASIO.** Steinberg's ASIO licence agreement (the proprietary one, the only one older SDKs such as 2.3.3 came with)
  adds conditions the AGPL doesn't allow, so a build with ASIO **and** Essentia mustn't be distributed under it.
  Since October 2025 Steinberg offers the ASIO SDK under GPLv3 as well; GPLv3 code may be combined with AGPLv3 code
  (section 13 of each). So for a build you distribute: use an ASIO SDK that Steinberg offers under GPLv3, take it under
  that licence, and follow Steinberg's trademark rules if you use the ASIO name or logo; or ship without ASIO (WASAPI
  only). Builds for your own use are unaffected: the AGPL's conditions apply to distributing. The releases do the
  first: SDK 2.3.4 under GPLv3, its licence text in `licenses/`, and the attribution *"ASIO is a registered trademark
  of Steinberg Media Technologies GmbH."* in Help › About, Preferences › Audio and `licenses/README.txt`. Steinberg's
  usage guidelines also ask for the *ASIO Compatible* logo in the About box and where ASIO is set up; that is still
  to do ([releasing.md](releasing.md#asio)).
- **HUMANBRO's runtime.** The repository calls it "the project's own". If you are its author, the root MIT licence
  covers it; say so in its folder (a `LICENSE` there, or a line in its README) to make it unambiguous.
- **The velocity model.** HUMANBRO's model was trained on MAESTRO v3, which Google publishes under
  [CC BY-NC-SA 4.0](https://magenta.tensorflow.org/datasets/maestro) (non-commercial, share-alike). Whether a model
  trained on a dataset is bound by the dataset's licence is legally unsettled. Shipping SUBstation for free is the
  least exposed; before selling it, either get advice, or ship it without `velocity.hbm` (Humanize › Velocity is then
  greyed out) or with a model trained on data you may use commercially. This is independent of Essentia.

## Building without Essentia

Not offered as a build option: Essentia makes the fingerprints, and the extractor it replaced is gone (its history is
in git). The extractor interface stays, so another extractor (a learned embedding, say) could take its place; a build
with it and without Essentia would then be under the GPLv3 as a whole (Vital's engine), with the permissive and LGPL
components above (LAME: an LGPL library linked statically, so such a build must let its users relink it with a changed
LAME, e.g. by shipping its object files or its source, as the GPL's source offer does anyway). Without Vital too
(the built-in Vital removed: `engine/third_party/vital`, the `vital` target, `devices/Vital.cpp` and `VitalPatch`,
the application layer's Vital files and the editor) it would be MIT plus those.
