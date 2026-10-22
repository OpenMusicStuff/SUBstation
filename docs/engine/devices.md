# Devices and the `Processor` interface

Every device on a track's chain (built-in instruments and effects, VST3 plug-ins, racks) is a `Processor`
([Processor.h](../../engine/src/Processor.h)), so the renderer never needs to know where one came from. Built-in
devices share a base class, `BuiltinProcessor`, and register themselves in `BuiltinRegistry`
([engine/src/builtin/](../../engine/src/builtin)); each one is a single `.cpp` in
[builtin/devices/](../../engine/src/builtin/devices).

How the user works with devices (the device view, racks, presets, folding, cut/copy/paste) is in
[guide/devices.md](../guide/devices.md). Plug-in hosting is in [plugins.md](plugins.md); racks and chains in
[routing.md](routing.md); the device view (QML and its scene-graph items) in [ui/device-view.md](../ui/device-view.md).

## Files

| File | What it holds |
|---|---|
| [Processor.h](../../engine/src/Processor.h) | `ProcessEvent`, `EventList`, `ProcessContext`, `ParamInfo`, `DisplayInfo`, `ParamAutomation`, `ProcessorEvent`, `Processor` |
| [builtin/BuiltinProcessor.h](../../engine/src/builtin/BuiltinProcessor.h) / [.cpp](../../engine/src/builtin/BuiltinProcessor.cpp) | parameters as atomics (`param`, `isOn`, `choice`, `choiceIndex`, `offOnLabels`), automation by splitting blocks, displays, state as named text values, `loadSource()` |
| [builtin/Dsp.h](../../engine/src/builtin/Dsp.h) | DSP the devices share, all inline: `dsp::kPi` and `kTwoPi`, the latency a device has told the engine (`LatencyReport`: its `idle()` returns `changed(latencySamples())`, its `prepare()` calls `reported(...)`), the state-variable filter section (`dsp::Svf`, `dsp::SvfCoefficients`), `hermite`, `followPeak`, `flushTiny`, the instruments' envelopes (`riseStep`, `fallCoefficient`, `kSilent`, `kFadeTo`), `renderBetweenNotes`, `kBendGlideSeconds` |
| [builtin/DspBlocks.h](../../engine/src/builtin/DspBlocks.h) | Building blocks the effects share, beside Dsp.h's, all inline: one-pole and DC filters (`dsp::OnePole`, `onePoleCutoff`, `DcBlocker`), a delay line (`DelayLine`, read linearly or with Hermite interpolation), an envelope follower (`EnvelopeFollower`), white noise (`Noise`), `fastTanh`, an LFO with Ableton's shapes and synced rates (`Lfo`, `LfoShape`, `syncedDivisionLabels`, `syncedCycleBeats`), RBJ biquads (`BiquadCoefficients`, `Biquad`), a Linkwitz-Riley crossover and its all-pass (`Crossover`, `CrossoverAllpass`), a sliding maximum for lookahead (`SlidingMax`), linear-phase oversampling by 2, 4 or 8 (`Oversampler`), a half-band decimator designed to a passband and an attenuation (`HalfbandDecimator`: what is made at twice the rate taken down) |
| [builtin/BuiltinRegistry.h](../../engine/src/builtin/BuiltinRegistry.h) / [.cpp](../../engine/src/builtin/BuiltinRegistry.cpp) | `BuiltinRegistry`, `BuiltinInfo`, `BuiltinCategory`, `SUB_REGISTER_BUILTIN` |
| [builtin/devices/Synth.cpp](../../engine/src/builtin/devices/Synth.cpp) | the Synth instrument |
| [builtin/devices/Supersaw.cpp](../../engine/src/builtin/devices/Supersaw.cpp) | the Supersaw instrument: up to three stacks of 1 to 64 band-limited saws a note, a sub sine, a filter with its envelope, the amplitude envelope, Poly, Mono or Legato with glide, Tilt (each saw at two detunes, joined at a harmonic of the note), Focus (a core of a saw per stack, and the sub, below a crossover); its oscillators at twice the rate, a period at a time on a grid of its own time, a half-band decimator; its latency |
| [builtin/SupersawDesign.h](../../engine/src/builtin/SupersawDesign.h) | The Supersaw's design: a stack's layout (`supersaw::layout`: each saw's cents, pan and gain; `detuneCents`, `curveExponent`, `middleWeight`, `sideWeight`, `panGains`) and the band-limited step (`StepTable`, `stepTable()`), shared with the application layer's `SupersawLayout.h` for the editor |
| [builtin/devices/Vital.cpp](../../engine/src/builtin/devices/Vital.cpp) | the Vital instrument: Vital's engine as a device (engines handed to the rendering thread, notes on their own channels, MIDI) |
| [builtin/VitalPatch.h](../../engine/src/builtin/VitalPatch.h) / [.cpp](../../engine/src/builtin/VitalPatch.cpp) | Vital's patches, with no Vital headers in the interface (the application layer uses it): its parameters (`params()`, `paramIndex()`, `paramText()`), the state values' names, `.vital` presets read and written (`readPreset()`, `writePreset()`), the modulations (`readModulations()`, `writeModulations()`, `modulationSources()`, `modulationDestinations()`, `sourceName()`, `sourceBipolar()`), wavetables from audio files and `.vitaltable` files and of basic shapes, LFO shapes (`wavetableFromAudioFile()`, `wavetableFromVitalTable()`, `wavetableOfShape()`, `lfoOfShape()`, `withWavetable()`, `withLfo()`, `wavetableNames()`, `lfoNames()`), and what the editor draws (`wavetableFrames()`, `lfoShape()`) |
| [builtin/VitalEngine.h](../../engine/src/builtin/VitalEngine.h) | `vitalpatch::Engine`: Vital's `SoundEngine` with its wavetable creators and tuning, built whole from a patch (`buildEngine()`) |
| [third_party/vital](../../engine/third_party/vital) | Vital's synthesis source, vendored (GPLv3; [VERSION.txt](../../engine/third_party/vital/VERSION.txt) says what and how), built without JUCE |
| [builtin/devices/Sampler.cpp](../../engine/src/builtin/devices/Sampler.cpp) | the Sampler instrument |
| [builtin/devices/Groove.cpp](../../engine/src/builtin/devices/Groove.cpp) | the Groove device, a MIDI effect: the notes reaching it moved in time with a groove (`groove::place()`, [music/Groove.h](../../music/src/music/Groove.h); [groove.md](../groove.md)), held and played later, or placed both ways when the renderer hands them over early |
| [builtin/SampleSlicing.h](../../engine/src/builtin/SampleSlicing.h) / [.cpp](../../engine/src/builtin/SampleSlicing.cpp) | The Sampler's slicing (`detectOnsets`, `sliceStarts`) and Snap (`nearestZeroCrossing`), shared with the application layer's `sampleSlices` ([app/src/audio/SampleSlices.h](../../app/src/audio/SampleSlices.h)) for the editor |
| [builtin/devices/Utility.cpp](../../engine/src/builtin/devices/Utility.cpp) | Utility: gain, pan, width |
| [builtin/devices/Ott.cpp](../../engine/src/builtin/devices/Ott.cpp) | Over The Top: multiband upward/downward compression |
| [builtin/devices/Compressor.cpp](../../engine/src/builtin/devices/Compressor.cpp) | Compressor, with sidechain and displays |
| [builtin/devices/Gate.cpp](../../engine/src/builtin/devices/Gate.cpp) | Gate: threshold, return, hold, attack and release, floor, flip, lookahead; keyed by its input or a sidechain through an EQ; listening to the key; displays |
| [builtin/GateDesign.h](../../engine/src/builtin/GateDesign.h) | The Gate's maths (`gate::floorGain`, `closeDb`, `pass`, `gain`, `lookaheadSamples`, `keyFilter`, `keyFilterDb`, the key EQ's types, the parameters' ranges), shared with the application layer's [GateResponse.h](../../app/src/audio/GateResponse.h) for the editor's meter, key EQ curve and drags; and, not shared with it, how a key EQ filter starts warm (`kWarmSeconds`, `kWarmPace`, `warmFrames`: the device's and its tests') |
| [builtin/devices/Limiter.cpp](../../engine/src/builtin/devices/Limiter.cpp) | Limiter: brick-wall lookahead limiting, Soft Clip, True Peak, L/R or M/S with Link, Maximize, its displays |
| [builtin/LimiterDesign.h](../../engine/src/builtin/LimiterDesign.h) | The Limiter's scales, Soft Clip's knee, the M/S ceiling, the true-peak interpolator and its parabola (`limiter::scales`, `knee`, `shape`, `sharedCeiling`, `truePeakPhases`, `refinedPeak`), shared with the application layer's `LimiterResponse.h` for the editor |
| [builtin/devices/Multiband.cpp](../../engine/src/builtin/devices/Multiband.cpp) | Multiband Dynamics: a three-way Linkwitz-Riley split, Above and Below per band, Peak and RMS detectors, split switches, activators and solos, a per-band sidechain and Listen, glides on a 32-sample grid, displays |
| [builtin/MultibandDesign.h](../../engine/src/builtin/MultibandDesign.h) | Multiband Dynamics' gain law (`multiband::aboveGainDb`, `belowGainDb`, `bandGainDb`, `staticGainDb`), ranges, display rate and detector windows (`windowSeconds`), shared with the application layer's `MultibandResponse.h` for the editor |
| [builtin/devices/Spectral.cpp](../../engine/src/builtin/devices/Spectral.cpp) | Spectral Compressor: an STFT (Hann, 4x overlap, each frame's work spread over the hop after it, latency N + H), per-bin levels smoothed across frequency and followed over time, Stereo Link, the gain computer both ways with the Focus band, Delta, sidechain keying, displays |
| [builtin/SpectralDesign.h](../../engine/src/builtin/SpectralDesign.h) | The Spectral Compressor's maths (`spectral::frameSize`, `latencySamples`, `calibrationDb`, `pinkDb`, `thresholdDb`, `belowDb`, `focusWeight`, `smoothingOctaves`, `gainDb`, `displayFrequency`), shared with the application layer's `SpectralResponse.h` for the editor |
| [builtin/devices/Hue.cpp](../../engine/src/builtin/devices/Hue.cpp) | Hue: a spectral pitch quantizer. Polyphase IIR half-band splits (a perfect-reconstruction QMF) down to a 22.05-24 kHz core; Linkwitz-Riley crossovers at Start and End (within 160 Hz-4 kHz); the mid band through three minimum-phase filters (the notes kept, and the notes a semitone off moved up or down by two delay-line pitch shifters; High Quality: a band and a shifter per pitch class moved, its heads a whole number of the note's periods apart, so it lands exactly), designed from masks over pitch by real cepstrum in steps on the rendering thread and crossfaded in; uniformly partitioned convolution in blocks of 256 core samples; notes from buttons, or the song key's scale (Auto, on at first: `keyTonic`, major or natural minor), or from MIDI notes once any come (MIDI Notes, on at first: each chord held until the next, until a reset) |
| [builtin/devices/GigaTune.cpp](../../engine/src/builtin/devices/GigaTune.cpp) | GigaTune: pitch correction for a single voice. Key and Scale (Song: `keyTonic` and `keyMinor`; chromatic without a song key), Retune Speed, Strength, Pitch (whole semitones) and Formant, on `gigatune::Corrector`; its latency fixed for the rate; its editor's displays `pitch`, `corrected` and `note` (a value per 256 samples out, of what plays: `Corrector::heard()`, the frame at that sample's input time) |
| [builtin/GigaTuneDesign.h](../../engine/src/builtin/GigaTuneDesign.h) / [.cpp](../../engine/src/builtin/GigaTuneDesign.cpp) | GigaTune's tracker and corrector, with no fastpitch in the header (its SIMD follows the compiler's flags: only the .cpp, built with the engine's, includes it). `PitchTracker`: fastpitch's YIN stage and observation model a frame at a time (32 ms frames every 2.67 ms, C2 to C7), a Viterbi decoder of its own over pYIN's HMM (librosa's transitions and tie rule) deciding each frame `kLag` frames later (0: as it comes; a longer lag traces back), and with the whole signal (`flush()`) librosa's path; each decided pitch refined by the difference function's minimum between whole lags (a windowed sinc between them, Brent's method) on the input low-passed to 8 kHz, the window ending at the newest input where a low voice's reaches past it. `offlinePyin()`: fastpitch's offline analysis, for tests. `Corrector`: targets with hysteresis (`targetNote()`, `scaleNotes()`), the correction followed by a one-pole, TD-PSOLA with analysis marks a period apart (moved onto the pulse at each voiced start, the grains' centres following it), synthesis marks an output period apart, grains spanning the analysis marks either side, divided by the windows' sum where it passes 1 (bit-exact at a ratio of 1), Formant reading voiced grains faster or slower (windowed-sinc reads, low-passed when faster). Grains are played a sample at a time, reading the input as they need it; the marks are placed as the grains need them, past the frames decided from the last one's pitch (provisional: placed again with each frame until a grain takes them; a grain takes its own when it starts and those after it when the output reaches its centre), and where a grain's read would pass the input in, an earlier period's grain plays. Low Latency's latency is MAutoPitch's (`kLowLatencySeconds`, 1024 samples at 48 kHz): a grain planned before a frame made it wider has its rising half squeezed (`lateSamples()`); High Quality's bound is worked out from the longest period and grain the settings allow, every mark settled |
| [builtin/SwiftF0.h](../../engine/src/builtin/SwiftF0.h) / [.cpp](../../engine/src/builtin/SwiftF0.cpp) | SwiftF0 (a small neural pitch tracker) run by the engine itself, a frame at a time, for GigaTune's High Quality: the host rate resampled to 16 kHz (a Kaiser-windowed sinc), three log-magnitude spectra every 256 samples (512, 1024 and 2048 points, KFR's FFT, 128-band filterbanks), its eighteen convolutions run column by column (each a frame late where it sees time, so a frame is final 176 ms after its centre), the pitch from the best of 95 classes and its neighbours, the confidence from a sigmoid, silence gated; `Detector` (streamed) and `analyse16k()` (a whole signal, for tests) |
| [builtin/devices/Changeling.cpp](../../engine/src/builtin/devices/Changeling.cpp) | Changeling: a pitch and formant shifter for a single voice, Little AlterBoy's controls and Polyverse Manipulator's (see [Changeling](#changeling-builtinchangeling-audioeffect-with-a-midi-input)): Mode, Pitch, Formant, Link, Glide, Key and Scale (`gigatune::scaleNotes()`), Drive, Mix (the dry signal delayed by the latency), Output, Harmonics, Twin, Interval, Detune, Width, Smear and Quality on `changeling::Shifter` and `changeling::Drive`; MIDI notes (`acceptsMidi()`) handed to the shifter at their offsets; its latency the Quality's and the drive's; its editor's displays `pitch`, `out` and `note` (a value per 256 samples out, `Shifter::heard()`) |
| [builtin/ChangelingDesign.h](../../engine/src/builtin/ChangelingDesign.h) / [.cpp](../../engine/src/builtin/ChangelingDesign.cpp) | Changeling's maths. `Shifter`: GigaTune's tracker (two: lag 0 for Live and High Quality, `kStudioLag` for Studio, both rescuing short notes, `PitchTracker::setRescue()`) and GigaTune's corrector's marks and grains in four voices (each channel's main voice and its twin), sharing the analysis marks, each with synthesis marks and grains of its own (a twin runs while Twin is up, starting from its main voice's marks); the output pitch per frame by Mode (Quantize: `gigatune::targetNote()`), MIDI (the note held at the frame's input time) and two glides; Formant and Link as each grain's read speed; Harmonics as a rotation of each voiced grain's analytic signal (the input's `Quadrature` pair); Detune and the twin's interval as the voices' pitch offsets, Width as gains on alternate grains, Smear as earlier grains and jittered marks. `Drive`: LAB's colour, its fitted curve at each of 0..10 and the compression past 6, at twice the rate (`dsp::Oversampler`); 0 a plain delay. `Quadrature`: an allpass pair 90 degrees apart (within 0.2 from 30 Hz to 20 kHz at 48 kHz) |
| [builtin/SwiftF0Data.h](../../engine/src/builtin/SwiftF0Data.h) | SwiftF0's weights (its filterbanks, convolutions and pitch classes), written from the model by [make_data.py](../../engine/third_party/swift-f0/make_data.py) |
| [third_party/swift-f0](../../engine/third_party/swift-f0) | swift-f0 0.3.0 (MIT): its model.onnx and licence, unmodified, and the script that writes SwiftF0Data.h ([VERSION.txt](../../engine/third_party/swift-f0/VERSION.txt)) |
| [third_party/fastpitch](../../engine/third_party/fastpitch) | fastpitch 1.0.0 (MIT), vendored unmodified ([VERSION.txt](../../engine/third_party/fastpitch/VERSION.txt)): a header-only pYIN that matches librosa.pyin frame for frame |
| [builtin/HueData.h](../../engine/src/builtin/HueData.h) | Hue's measured data: each note's gain for every set of enabled notes (4095 x 12), the kept notes' falloff over pitch, and Color past 100 % (the falloff at 125-200 %, and the notes' lift and the floor's drop by how many notes are enabled) |
| [builtin/devices/Saturator.cpp](../../engine/src/builtin/devices/Saturator.cpp) | Saturator: eight curves, Color's emphasis and its inverse, Post Clip on the dry/wet blend, DC, 4x oversampling of all of it |
| [builtin/SaturatorDesign.h](../../engine/src/builtin/SaturatorDesign.h) | The Saturator's curves (`saturator::makeShape`, `params`, `curve`, `transfer`), Post Clip and Color's design (`colorDesign`, `inverse`, `between`, `colorResponseDb`, `colorTimeConstant`), shared with the application layer's `saturatorCurve()` and `saturatorColorDb()` for the editor |
| [builtin/devices/Amp.cpp](../../engine/src/builtin/devices/Amp.cpp) | Amp: seven amp models, the tone stack, sag, Mono and Dual, sleep |
| [builtin/AmpDesign.h](../../engine/src/builtin/AmpDesign.h) | The Amp's design (`amp::kVoicings`, `blend`, the stages' curve and `Adaa`, the tone stack, the dials' mappings, `Transfer`: a tone's steady state by harmonic balance, from tables a rate and a voicing set that can be worked out beforehand (`BasicTransfer`'s `Rate` and `Voice`), the displays' `kDisplaySamples` and `kDisplayFloorDb`), shared with the application layer's `ampToneResponseDb()`, `ampTransfer()` and `AmpTransferCurve` for the editor's curves (and `ampModelNames()`, `ampDisplayFloorDb()`); the device keeps its model morph level-matched with a `BasicTransfer<64>` |
| [builtin/devices/Erosion.cpp](../../engine/src/builtin/devices/Erosion.cpp) | Erosion: a 2 ms delay modulated by a sine or band-passed noise, Noise Blend and Stereo Width, chunked glides and ramps, displays |
| [builtin/ErosionDesign.h](../../engine/src/builtin/ErosionDesign.h) | Erosion's maths (`erosion::band`, `noisePowerGain`, `bandMagnitude`, `bandEdges`, `excursionSamples`, `blendWeights`, `stereoSpread`, the instances' noise salts), shared with the application layer's `ErosionResponse.h` for the editor |
| [builtin/devices/Overdrive.cpp](../../engine/src/builtin/devices/Overdrive.cpp) | Overdrive: a band-pass into an anti-aliased asymmetric soft clipper, Tone, Dynamics, chunked glides, displays |
| [builtin/OverdriveDesign.h](../../engine/src/builtin/OverdriveDesign.h) | The Overdrive's maths (`overdrive::bandEdges`, `driveGain`, `toneHz`, `shape` and its `Adaa`, `bandResponseDb`, `toneResponseDb`, `dynamicsGain`), shared with the application layer's `OverdriveResponse.h` for the editor |
| [builtin/devices/AutoFilter.cpp](../../engine/src/builtin/devices/AutoFilter.cpp) | Auto Filter: a TPT state-variable filter (12 or 24 dB, five types by output weights, five circuits), its cutoff swept by an envelope follower (or the sidechain) and an LFO in chunks, glides, displays |
| [builtin/AutoFilterDesign.h](../../engine/src/builtin/AutoFilterDesign.h) | The Auto Filter's maths (`autofilter::resonanceQ`, `cutoffHz`, `envelopeValue`, `sweepOctaves`, `weights` and `morphWeights`, the circuits' `saturate` and `stateLimit`, `responseDb`), shared with the application layer's `AutoFilterResponse.h` for the editor |
| [builtin/devices/Delay.cpp](../../engine/src/builtin/devices/Delay.cpp) | Delay: synced or free times per side, filter, modes, ping pong, freeze; swing, or the project's groove's (Follow Groove) |
| [builtin/devices/Chorus.cpp](../../engine/src/builtin/devices/Chorus.cpp) | Chorus-Ensemble: modulated delays in three modes (Chorus with Taps and Time, Ensemble, Vibrato), feedback with Invert, Warmth, the high-pass split, Width |
| [builtin/ChorusDesign.h](../../engine/src/builtin/ChorusDesign.h) | The Chorus-Ensemble's voices (`chorus::layout`, `centreMs`, `swingMs`, `voicePhase`, `lfoValue`, `delayMs`, the detune), its warmth's curve and filter and its feedback's limiter, shared with the application layer's `chorusDelayMs()` and neighbours for the editor's graph |
| [builtin/devices/Phaser.cpp](../../engine/src/builtin/devices/Phaser.cpp) | Phaser-Flanger: a phaser, flanger and doubler, two LFOs and an envelope follower, feedback, Warmth, Safe Bass, displays |
| [builtin/PhaserDesign.h](../../engine/src/builtin/PhaserDesign.h) | The Phaser-Flanger's maths (`phaser::` modes and LFO shapes `waveValue`, the modulation's mappings `phaserCenterHz`, `phaserQ`, `delayMs`, the response `transfer`/`responseDb`, `notchFrequencies`, `curve`), shared with the application layer's `PhaserResponse.h` for the editor's graph |
| [builtin/devices/Reverb.cpp](../../engine/src/builtin/devices/Reverb.cpp) | Reverb: input filter, early reflections with Spin, diffusers, a 4/8/16-line feedback delay network with per-band decay, chorus, Freeze/Cut/Flat, the guard, Density's crossfade, sleeping |
| [builtin/ReverbDesign.h](../../engine/src/builtin/ReverbDesign.h) | The Reverb's design: its constants and layouts, the loops' decay per band (`reverb::rates`, `loop`, `decaySecondsAt`, `decaySeconds`), the input filter (`inputFilterDb`), the reflections (`earlyTaps`, `spinPan`), sizes (`sizeFactor`, `onsetMs`, ...), shared with the application layer's `ReverbResponse.h` for the editor |
| [builtin/devices/Disperser.cpp](../../engine/src/builtin/devices/Disperser.cpp) | Disperser: up to 64 all-pass stages, glides and fades, bypass |
| [builtin/DisperserDesign.h](../../engine/src/builtin/DisperserDesign.h) | The Disperser's stages (`disperser::design`, `process`, `groupDelayMs`), shared with the application layer's `disperserGroupDelayMs()` for the editor's graph |
| [builtin/devices/Eq.cpp](../../engine/src/builtin/devices/Eq.cpp) | EQ: 24 bands, placement, output gain, gain scale |
| [builtin/EqDesign.h](../../engine/src/builtin/EqDesign.h) | The EQ's filter design (`eq::design`, `eq::responseDb`), shared with the application layer's `eqResponseDb()` for the editor's curves; `eq::Biquad::tick` (a sample through a section, transposed direct form II: the EQ's and the Sidechain's crossover) |
| [builtin/devices/Sidechain.cpp](../../engine/src/builtin/devices/Sidechain.cpp) | Sidechain: a curve from each hit in the key (or on the beat), lookahead, lows only |
| [builtin/devices/Lfo.cpp](../../engine/src/builtin/devices/Lfo.cpp) | LFO: a modulator; a drawn curve played over and over, in Hz or synced (following the song), Depth, Offset, Phase, Retrigger, Smooth; Random (a new random curve each cycle: `modulator::randomPoints` from a seed it publishes, so the editor draws the one playing); Follow Groove (synced, its time where the project's groove puts it) |
| [builtin/devices/Envelope.cpp](../../engine/src/builtin/devices/Envelope.cpp) | Envelope: a modulator; a drawn curve played from each note or sidechain hit, in ms or synced, Depth, Velocity, Threshold, Smooth; Random (a new random curve from each trigger) |
| [builtin/ModulatorDesign.h](../../engine/src/builtin/ModulatorDesign.h) | What the modulators share (`modulator::`): their curve's parameters (`appendPointParams`, `kPoints`), the curve as they play it (`Curve`, automation's shapes worked out once a segment, `fastmath::exp` a sample), the shapes they start with (`sinePoints`, `pluckPoints`), synced lengths, smoothing, `kDisplaySamples` |
| [Modulation.h](../../engine/src/Modulation.h) | `ModulationDesc` (a modulator moving a parameter of a device on its track: its range and which way up), `modulationTarget`, `kMaxModulators`; see [automation.md](automation.md#modulation) |
| [builtin/devices/Transient.cpp](../../engine/src/builtin/devices/Transient.cpp) | Transient Shaper: onsets and tails turned up or down by followers on the input's envelope, Threshold, Output, Dry/Wet, Clip, lookahead, displays |
| [builtin/TransientDesign.h](../../engine/src/builtin/TransientDesign.h) | The Transient Shaper's maths (`transient::Hilbert`, the envelope; `Detector`, its three followers; `weight`, the threshold's fade; `gain`, the gain law; `softClip`; `lookaheadSamples`; the hit its editor draws, `hitEnvelope` and `hitResponse`), shared with the application layer's [TransientResponse.h](../../app/src/audio/TransientResponse.h) for the editor |
| [builtin/devices/Vocoder.cpp](../../engine/src/builtin/devices/Vocoder.cpp) | Vocoder: Ableton's, a filter bank of up to 40 bands; Noise, External (the sidechain), Modulator and Pitch Tracking carriers, Enhance, Unvoiced, Precise or Retro, Formant, Depth, Gate, Mono, Stereo or L/R, displays |
| [builtin/VocoderDesign.h](../../engine/src/builtin/VocoderDesign.h) | The Vocoder's maths (`vocoder::`): the bands (`bandCount`, `layout`, `sections`, `bandResponseDb`) and the pitch tracker (`PitchTracker`, YIN), shared with the application layer's [VocoderResponse.h](../../app/src/audio/VocoderResponse.h) for the editor |
| [builtin/devices/VocalRider.cpp](../../engine/src/builtin/devices/VocalRider.cpp) | Vocal Rider: a slow gain riding a vocal to a target, holding between phrases, the music on the sidechain moving the target, displays |
| [Rack.h](../../engine/src/Rack.h) | `RackProcessor`: a rack's place in a chain (its chains are run by the renderer) |
| [rt/RtUtils.h](../../engine/src/rt/RtUtils.h) | `SmoothedValue`, `SpscQueue`, `DisplayStream`, `dbToGain`, `gainToDb`, `expDbToGain`, `onePoleCoefficient`, `balanceGains` |
| [EngineChains.cpp](../../engine/src/EngineChains.cpp) | `addBuiltinProcessor()`, and the processor calls the engine bridge makes (`setProcessorParam`, `processorState`, `setProcessorState`, `processorDisplays`, `readProcessorDisplay`, ...) |
| [app/src/model/Devices.h](../../app/src/model/Devices.h) | The application layer's view of the registry: `BuiltinDevice`, `builtinDevices()`, `builtinCategories()`, `builtinParamInfo()` |
| [ui/qml/devices/editors/](../../ui/qml/devices/editors) | Built-in devices' own editors (QML, drawing with items in [ui/src/devices](../../ui/src/devices)), registered by kind |

## The `Processor` interface

### Threads

`process()` and `reset()` run on the rendering thread (the audio thread, a worker, or the thread rendering offline).
Everything else is called from the main (UI) thread, as plug-in formats require, unless noted. `getParam()`,
`requestReset()`, `takeResetRequest()`, `setEnabled()`/`isEnabled()` and `readDisplay()` are thread-safe.

### Members

| Member | Meaning |
|---|---|
| `typeId()` | `"builtin:utility"`, `"vst3:<class id>"`, `"rack"` |
| `name()` | shown name |
| `prepare(sampleRate, maxBlockSize)` | before the processor is first published to the audio thread, and again with audio stopped whenever the sample rate changes. `maxBlockSize` is `Renderer::kMaxBlock` (1024) |
| `reset()` | real-time: silences the processor (notes, tails, filter state). Only the renderer calls it, before a block, when a reset was requested |
| `resetOffline(rendering)` | clears what a real-time reset can't (a plug-in's reverb tail), not being processed; around offline renders: `rendering` before one (plug-ins process in their offline mode until the next call), false after |
| `process(ctx, channels, numChannels, numFrames)` | real-time: planar buffers in place (the renderer passes 2 channels) |
| `latencySamples()` / `tailSamples()` | latency is compensated by delaying the other tracks (and sidechains) |
| `params()`, `getParam()`, `setParam()` | plain (not normalized) values |
| `paramText(index, value)` | the processor's own text for a value; `""` leaves it to the UI |
| `getState()` / `setState()` | everything needed to restore it; empty for processors whose parameters are their whole state |
| `idle()` | main-thread housekeeping, called regularly; true if the latency changed (the engine realigns) |
| `takeEvents(out)` | `ProcessorEvent`s for the UI (edits in a plug-in's editor, closed editors, ...) |
| `hasEditor()`, `openEditor()`, `closeEditor()`, `isEditorOpen()`, `setEditorVisible()`, `setEditorTitle()` | a plug-in's own editor window |
| `setEnabled(bool)` | switching off also requests a reset, so when it comes back on it can't resume notes whose note-offs it missed |
| `requestReset()` / `takeResetRequest()` | asks whichever renderer processes it next to `reset()` it first |
| `automate()`, `clearAutomation()`, `automation()`, `numAutomation()` | automation for one `process()` call; see [automation.md](automation.md) |
| `hasSidechain()`, `setSidechain()`, `setSidechainConnected()`, `sidechain(c)`, `sidechainConnected()` | the sidechain (aux) input; see below |
| `isMidiEffect()`, `endMidiNotes(out)` | a MIDI effect: it plays its chain's notes, before the chain's other devices (see [MIDI effects](#midi-effects)); switched off, it ends the notes it was playing |
| `earlyBeats()` | how far before their time a MIDI effect may play notes (beats; 0, the default: never): the first MIDI effect of a chain gets the clips' notes that much early (`ctx.earlyEvents`; see [MIDI effects](#midi-effects)). Any thread |
| `acceptsMidi()` | it plays notes (an instrument, or an effect with a MIDI input, as a vocoder or a pitch corrector has): what can take another track's notes ([midi.md](midi.md#a-devices-midi-input-from-another-track)). Main thread; false by default |
| `displays()`, `readDisplay()` | streams of values for a device's own editor |

### Events and context

- `ProcessEvent`: `NoteOn`, `NoteOff` (`data[0]` key, 60 = C3; `data[1]` velocity, 0 for note-offs; `data[2]`
  channel), `NoteBend` (a note's pitch from here on, `bend` semitones from its key: MIDI 2.0's per-note pitch bend)
  or raw `Midi` bytes, at a `sampleOffset`. Notes are addressed as MIDI 2.0 addresses them: a note-on carries a
  `noteId`, and its note-off and bends carry the same; -1 (a MIDI 1.0 message) means the note is known by its key and
  channel. The renderer sends each track's notes to every processor on the track (audio effects ignore them; a device
  may take another track's instead), sorted by offset, note-offs before note-ons at the same offset and a note's
  bends after its note-on. A note-on with velocity 0 counts as a note-off (`startsNote()`, `endsNote()` say which an
  event is). Plug-in adapters translate them to their format's events. See [midi.md](midi.md) for where they come
  from.
- `ProcessContext`: sample rate, `samplePos` and `beatPos` of the block's first frame, tempo, time signature, the
  song's key (`keyTonic`, -1 when none is known, and `keyMinor`: `Engine::setSongKey`, which the application sets from
  its harmony, for devices that follow it), `playing`, `looping` with loop start and end beats, `offline`, the song's
  tempo map (`tempoMap`, a `TempoMap` from [music/](../music.md); null when none is known) and the project's default
  groove (`groove`: `Engine::setGroove`, null for none, for devices that play on the beat and follow it), `inEvents`,
  `outEvents` (a MIDI effect's notes, an `EventOutput`; null for every other device) and `earlyEvents` (the clips'
  notes handed early to a chain's first MIDI effect: [MIDI effects](#midi-effects)); `samplesPerBeat()` and
  `beatsPerBar()` from them. The renderer splits blocks where the playhead jumps (a loop wrap), so a block is always
  one continuous stretch.

### Sidechain

A processor with an aux input returns true from `hasSidechain()`. Before each `process()` call the renderer hands it
what goes into that input over the call's frames (two channels, `setSidechain(left, right)`), or null for silence (no
source, or solo leaves it out); after the call, null again. `setSidechainConnected()` says whether a source was chosen
at all, so a device can key from its own input without one, yet hear silence when solo leaves its source out. Routing
and alignment of sidechains are in [routing.md](routing.md).

### MIDI effects

A MIDI effect (`isMidiEffect()`: the Arpeggiator, the Groove device) plays notes instead of audio. The renderer runs
a chain's MIDI effects before the chain's other devices, in their order, wherever they sit in it (as it runs
modulators before their strip: `Renderer::processMidiEffects`, from `processChain`, so a rack's chain runs its own):
each gets the notes the one before it played (the first, the chunk's), slice by slice with its automation and
modulation as any device's, in `ctx.inEvents`, and appends what it plays to `ctx.outEvents` (an `EventOutput`:
offsets relative to its call, in time order; full, the rest is dropped). Those are the notes the chain's devices
hear, kept in the worker's scratch (`midiNotes`, two buffers a depth). Raw MIDI it passes on itself. It never
touches the audio, and in its place in the chain the audio goes past it.

Switched off (or its automation switch off at the chunk's start) it isn't run and the notes go on as they come,
after the note-offs of the notes it was still playing (`endMidiNotes()`, called each such chunk). One taken out
of a chain, or moved to another, asks the chain's devices to reset (`Engine::resetChainLocked`), so an
instrument doesn't hold the notes it was playing for ever.

**Early notes.** A MIDI effect can't play a note before it comes in, but the clips' notes are known ahead. One whose
`earlyBeats()` is more than 0 (the Groove device, with Allow Early on) is handed, if it is the first MIDI effect of
its chain (a rack's chain or a pad counts as its own chain), the clips' notes due within that many beats after the
chunk, and their note-offs: `ctx.earlyEvents`, an `EarlyList` of `EarlyEvent`s (the event, whose `sampleOffset`
from the start of the call is at or past its end: where it would play; and `beat`, where that is on the timeline), in
its first stretch of the chunk only. It plays them into `outEvents` as it plays any note, early or not. Each comes again
in its time with the same note id, for the devices that didn't take it early, and the effect ignores it then. A
handed note ended before its time (a locate, stopping) never sounded: the Groove device drops it. Notes the effects
before it play, and live input, aren't known ahead. How the renderer finds and hands them over is in
[midi.md](midi.md#early-notes).

In the model it is an ordinary device after the instrument (the model keeps an instrument first in its chain);
`BuiltinCategory::MidiEffect`, listed after the instruments.

### Displays

What a device's own editor draws besides its parameters (a meter's readings, a curve, samples for an analyser) is a
stream of floats: `displays()` lists them as `DisplayInfo {id, samplesPerValue}` (1 for samples, more for a meter);
`readDisplay(index, position, out)` appends stream `index`'s values since `position` (0 at first) and returns where
to read from next. Any thread may call it, and any number of readers can follow a stream.

Built-in devices back each display with a `DisplayStream` ([rt/RtUtils.h](../../engine/src/rt/RtUtils.h)): the writer
never waits and never fails (it overwrites the oldest values); each reader keeps its own position, and one that falls
behind skips to the latest 8192 values (`kDisplayCapacity`). A reader overtaken while it reads may get a newer value
in an older one's place, which is harmless for drawing.

The engine's API has them by processor id: `Engine::processorDisplays(id)` and
`Engine::readProcessorDisplay(id, index, position, out)` (appends to a `std::vector<float>` and returns the next
position). The engine bridge wraps them by track and device id (`EngineBridge::processorDisplays()`,
`readProcessorDisplay()`) for the editors.

## `BuiltinProcessor`

What every built-in device shares ([BuiltinProcessor.h](../../engine/src/builtin/BuiltinProcessor.h)):

- **Parameters.** A fixed `ParamInfo` list (a static the device owns: it must outlive the processor) is passed to the
  constructor. Values are atomics initialised to the defaults; the UI sets them (`setParam` clamps to the range),
  the rendering thread reads them with `param(index)`, a switch with `isOn(index)` (on from 0.5, as automation plays
  it: `automationSwitchOn`), a list or stepped value with `choice<Enum>(index)` or `choiceIndex(index)` (rounded).
  A switch's labels are `offOnLabels()`. `params()`, `getParam()`, `setParam()`, `process()`,
  `displays()`, `readDisplay()`, `getState()` and `setState()` are `final`.
- **Automation.** `process()` renders the block in stretches between the points where automation changes a value,
  calling the device's `render()` for each, with that stretch's events (offsets relative to it), position and beat
  position, and sidechain pointers (`sidechain(c)` in a `BuiltinProcessor` already points at the stretch). Up to 8
  channels; up to 2048 events per stretch. Without automation it calls `render()` once. See
  [automation.md](automation.md).
- **Displays.** A device passes `DisplayInfo`s to the constructor and calls `publish(index, value)` from `render()`.
- **State besides parameters** (a sampler's sample) is kept as named text values: the device implements
  `stateValues()` / `setStateValues()`. `getState()` encodes them as lines `name=value` (UTF-8; a backslash escapes a
  backslash or a newline: `\\`, `\n`); empty means none, the defaults. `setStateValues()` is called on the main
  thread or on the thread the UI loads states on; it may take its time (loading files) and throws if it can't do it
  all. The model keeps the state base64-encoded in `Device::state`, as it keeps a plug-in's
  ([app/src/model/DeviceState.h](../../app/src/model/DeviceState.h), namespace `deviceState`, has the same encoding),
  so it saves, loads and undoes as a plug-in's does.
- **Audio files** come through `loadSource(path)` (not real-time). The engine sets a `SourceLoader` on every built-in
  device it creates (`Engine::addBuiltinProcessor`): `Engine::loadResidentSource()`, so files are shared with the
  engine's cache and decoded at the engine's rate, and always into memory, however long (a device reads its sample
  anywhere, note by note; a clip of the same long file may stream: [streaming.md](streaming.md)); without one (a
  device made outside an engine) it decodes the file at its own rate. See [warp.md](warp.md#the-source-cache).

## `BuiltinRegistry`

A device registers itself where it is defined: each `.cpp` in `builtin/devices/` ends with

```cpp
SUB_REGISTER_BUILTIN(MyProcessor, AudioEffect);   // or Instrument, MidiEffect, or Modulator
```

A `Modulator` (the LFO, the Envelope) processes no audio: it moves other devices' parameters
(`Processor::isModulator()`; see [automation.md](automation.md#modulation)). The browser lists
the categories as *Instruments*, *MIDI Effects*, *Audio Effects* and *Modulators*. A `MidiEffect` plays its
chain's notes (see [MIDI effects](#midi-effects)); its `render()` writes them with `emit(ctx, event)`, which puts
the event's offset (relative to the stretch, as its input's are) back in the block's terms.

The macro makes a static `BuiltinRegistrar` that adds a factory (`std::make_shared<MyProcessor>()`) and a
`BuiltinCategory` to `BuiltinRegistry::instance()` (a function-local static, built on first use, since registrars run
in no fixed order).

- `devices()` builds the list once (`std::call_once`): it makes one prototype of each device and records its id
  (its `typeId()` without the `builtin:` prefix), name, category and parameters as a `BuiltinInfo`; instruments come
  first, then MIDI effects, audio effects, then modulators, each by name.
- `create(id)` makes a new device of that id; `std::invalid_argument` for an unknown one.

The engine creates devices by id (`Engine::addBuiltinProcessor(chainId, type, index)`: it sets the source loader,
calls `prepare()` before the audio thread can see the processor, and inserts it). The application's device list,
names, categories and parameter defaults all come from the registry (`BuiltinRegistry::instance().devices()`): the
application layer's `builtinDevices()`, `builtinCategories()` and `isInstrument()`
([app/src/model/Devices.h](../../app/src/model/Devices.h)) and the browser's *Built-in* category are built from it. A
model `Device` of a built-in device has the registry id as its `kind`.

Each device's translation unit only registers itself: nothing refers to it. A linker takes from a static library
only the objects something refers to, so a program linking `sub_engine` would leave the devices out and they would
never register. So [engine/CMakeLists.txt](../../engine/CMakeLists.txt) collects the files with
`file(GLOB BUILTIN_DEVICE_SOURCES CONFIGURE_DEPENDS src/builtin/devices/*.cpp)`, gives each one an empty anchor
function named after it (`SUB_BUILTIN_ANCHOR`, such as `subBuiltinAnchor_Utility`, which `SUB_REGISTER_BUILTIN`
defines), and writes `generated/BuiltinDevices.cpp`, whose `linkBuiltinDevices()` calls them all.
`BuiltinRegistry::instance()` calls `linkBuiltinDevices()`, so every device file is linked in.

## The built-in devices

All parameter tables below are the devices' `ParamInfo`s: id, name, unit, range, default; "log" means `logScale`.
Mixer-like controls (`gain`, `volume`, `width`, `depth`) are smoothed with `SmoothedValue` ramps (20 or 30 ms) so
turning or automating them doesn't zipper.

### Synth (`builtin:synth`, Instrument)

A polyphonic subtractive synth: 16 voices (`kMaxVoices`), one oscillator each, an ADSR amplitude envelope and a
resonant low-pass filter. Mono, on both channels. Velocity sets the level.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `wave` | Wave | | 0..3: Sine, Triangle, Saw, Square (a list) | Saw |
| `attack` | Attack | ms | 1..5000, log | 3 |
| `decay` | Decay | ms | 1..5000, log | 300 |
| `sustain` | Sustain | % | 0..100 | 70 |
| `release` | Release | ms | 1..10000, log | 200 |
| `cutoff` | Cutoff | Hz | 20..20000, log | 4000 |
| `resonance` | Resonance | % | 0..100 | 10 |
| `volume` | Volume | dB | -60..6 | 0 |

- Oscillators: sine, triangle, and band-limited saw and square (PolyBLEP: the step at phase 0 is smoothed over one
  sample on each side, which removes most of the aliasing of a naive saw or square).
- Envelope: attack is a linear ramp; decay and release are exponential, with their times to -60 dB; decay ends at,
  and holds, the sustain level (and follows it if it changes). A releasing voice ends at -80 dB.
- Filter: a TPT state-variable low-pass (Zavalishin/Simper), stable at any cutoff and resonance. The cutoff glides to
  its target (about 5 ms) with coefficients per sample, so turning the knob doesn't step; it is limited to 0.45 of the
  sample rate. Resonance maps to damping from 2 (none) to 0.1 (Q = 10) and applies per block.
- Voices: a note-on takes a free voice, else the quietest releasing one, else the oldest. With the same key held
  twice, the older note ends first (a note-off with a note id ends that note's voice). One voice's peak at full
  velocity is 0.25.
- Bends: a `NoteBend` moves its note's voice (by its note id, else the newest voice on its key) to `bend` semitones
  from its key, held to ±48 (`kMaxBendSemitones`). The voice glides there per sample, a one-pole of about a
  millisecond (`dsp::kBendGlideSeconds`), so the renderer's steps (one every 32 samples at most) don't zipper; the
  oscillator's increment is the key's times 2^(bend/12).
- It *writes* (does not add) its output: it is the first device on a MIDI track. With no events and no active voice it
  writes silence and skips the work. `tailSamples()` is the release time.

### Supersaw (`builtin:supersaw`, Instrument)

An instrument for stacks of detuned saws, made as clean as saws can be: up to three stacks a note, each of 1 to 64 saws,
band-limited to the fraction of a sample, at twice the rate below 80 kHz; a sub sine; a filter with an envelope of its
own; Poly, Mono or Legato with glide; Tilt, a stack's spread below a harmonic of the note against above it; Focus,
which keeps the low end a single saw in the middle. 16 notes. What the user sees is in
[guide/devices.md](../guide/devices.md#supersaw).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `s1_on`, `s2_on`, `s3_on` | Stack 1..3 | | Off, On | On, Off, Off |
| `s<n>_saws` | Stack *n* Saws | # | 1..64 (steps) | 7, 7, 5 |
| `s<n>_transpose` | Stack *n* Transpose | st | -48..48 (steps) | 0, -12, +12 |
| `s<n>_fine` | Stack *n* Fine | ct | -100..100 | 0 |
| `s<n>_detune` | Stack *n* Detune | % | 0..100 | 45, 35, 30 |
| `s<n>_curve` | Stack *n* Curve | % | -100..100 | 35 |
| `s<n>_blend` | Stack *n* Blend | % | 0..100 | 75 |
| `s<n>_width` | Stack *n* Width | % | 0..100 | 100, 70, 100 |
| `s<n>_level` | Stack *n* Level | dB | -60..6 | 0, -3, -6 |
| `phase` | Phase | ° | 0..360 | 0 |
| `random` | Random Phase | % | 0..100 | 100 |
| `drift` | Drift | % | 0..100 | 15 |
| `sub` | Sub | % | 0..100 | 0 |
| `sub_octave` | Sub Octave | | 0, -1, -2 (a list) | -1 |
| `filter_type` | Filter | | Off, Low 12, Low 24, High 12, High 24, Band (a list) | Low 24 |
| `cutoff` | Cutoff | Hz | 20..20000, log | 20000 |
| `resonance` | Resonance | % | 0..100 | 0 |
| `key_track` | Key Track | % | 0..100 | 0 |
| `filter_env` | Filter Env | % | -100..100 | 0 |
| `filter_attack`, `filter_decay`, `filter_release` | Filter Attack, Decay, Release | ms | 1..5000, 1..10000, 1..10000, log | 2, 500, 300 |
| `filter_sustain` | Filter Sustain | % | 0..100 | 50 |
| `attack`, `decay`, `release` | Attack, Decay, Release | ms | 1..5000, 1..10000, 1..10000, log | 2, 600, 300 |
| `sustain` | Sustain | % | 0..100 | 100 |
| `velocity` | Velocity | % | 0..100 | 50 |
| `mode` | Mode | | Poly, Mono, Legato (a list) | Poly |
| `glide` | Glide | ms | 0..2000 | 0 |
| `bend_range` | Bend Range | st | 0..24 (steps) | 2 |
| `volume` | Volume | dB | -60..6 | 0 |
| `focus` | Focus | | Off, On | Off |
| `focus_freq` | Focus Freq | Hz | 40..1000, log | 200 |
| `s<n>_tilt` | Stack *n* Tilt | % | -100..100 | 0 |
| `tilt_at` | Tilt At | # (a harmonic) | 1..16 (steps) | 4 |

**A stack** ([SupersawDesign.h](../../engine/src/builtin/SupersawDesign.h), `supersaw::layout`): N saws around the
stack's pitch (the note, Transpose and Fine). Their places, from the flattest to the sharpest, are N points spread evenly
over -1..1, bent by Curve (x becomes sign(x) |x|^e, e = 2^(1.5 Curve): above 0 the saws gather towards the middle, as
the JP-8000's do, which keeps their beating from lining up into a pattern that repeats; below 0 towards the edges), times
the outermost saws' detune, Detune² × 100 cents (50 % is 25 cents). Blend weighs the middle saw (an even stack's middle
pair shares its weight) against the others: the middle at 1 - Blend / 3, each other at Blend (all alike at 75 %), then
the whole made to unit power, so a stack is as loud whatever its count. Width spreads the saws over the stereo field:
each mirrored pair (the flattest and the sharpest, and so on inwards) to opposite sides, the outer pairs widest, the flat
one of each pair to the left and the right in turn, so each side hears flat and sharp saws alike; equal power, 1 at the
middle (Width 0: both sides the same, to the bit). A note's stack at full velocity has the level of one saw of 0.25 (its
RMS 0.25/√3); Level and Volume scale it.

**Saws without aliasing.** A saw's phase is a 32-bit integer stepped by a 32-bit increment (the nearest to its
frequency: within 3·10^-8 at 440 Hz): exact, wrapping by itself, and its value is the phase read as a signed number, a
rise from -1 to 1. At each jump the device adds the difference between a band-limited step and the plain one (Brandt's
BLEP) from `supersaw::StepTable`: the integral of a Kaiser-windowed sinc (β 13, ±9 samples), tabulated at 1024 fractions
of a sample (linear between them, within 3·10^-7), flat to a quarter of the oscillators' rate and about 125 dB down
from three quarters of it. Below 80 kHz the oscillators run at twice the rate and a half-band decimator
(`dsp::HalfbandDecimator`: flat to 20 kHz, or to 0.4535 of the rate, and 120 dB down where it would fold into that band;
171 taps at 44.1 kHz, 95 at 48 kHz) takes the mix down, filtering away what the step lets fold into the octave above the
rate's Nyquist. So whatever isn't a harmonic is more than 130 dB under each saw's fundamental at any pitch (a plain saw's
aliases are 17 to 55 dB down). At 88.2 kHz and above the oscillators run at the rate, without a decimator (what the step
lets fold then lands above 24 kHz). The sub is a sine of the phase (a polynomial, within 6·10^-8), which has no jumps.

**A voice, a period at a time.** Everything that controls the saws is worked out every 32 oscillator samples
(`kControl`), on a grid of the device's own time: the pitch (the note, its glide, its note's bend and the wheel, each
gliding), the stacks' saws' places (gliding about 12 ms when Detune or Curve moves) and gains (5 ms), each saw's drift,
the filter's envelope and cutoff, the envelopes' settings. Since nothing a voice's oscillators do changes inside a period,
they run a whole period at once at its start (a voice that starts inside one, from its note on): loops over each saw's
samples that the compiler vectorises, each jump's correction scattered into the voice's accumulator, which reaches 9
samples ahead (the step's reach: the voice's output is the accumulator 9 samples behind its oscillators, and its
amplitude envelope runs as far ahead). The events inside a period then only touch the envelope and the filter, which go a
sample at a time; gains and the filter's cutoff ramp linearly between updates, worked out from the period's start. So the
output is the same to the bit however the blocks are cut (the sums add in the same order). Pitches step at the updates, in
phase.

- Drift: each saw's own slow wander, two one-poles (0.8 s each) on noise, scaled to a standard deviation of 4 cents at
  100 %, each saw starting somewhere along it. Phase: where each saw starts; Random Phase: how much of a random phase is
  added to it (100 %: anywhere; 0: every saw together, the note starting with one great jump). The random numbers come
  from the device's own generator, seeded at every reset, so renders come out the same each time.
- The filter, per voice and per side: TPT state-variable sections (Zavalishin/Simper) at the oscillators' rate. Low 12 and
  High 12 one section, Low 24 and High 24 two (a 4-pole Butterworth's: damping 1.848 and 0.765), Band one, normalised to
  unity at its centre. Resonance raises Q from Butterworth's to 12 (in the 24 dB types, the second section's). The cutoff
  (gliding 5 ms, in octaves) moves by Key Track (100 %: the key's pitch from C3, an octave an octave) and by its envelope
  (Filter Env ±100 %: ±10 octaves at its peak), limited to 0.45 of the oscillators' rate; g goes linearly through each
  period. A new type starts its sections from rest.
- Envelopes: as the Synth's (a linear attack; decay and release exponential, their times to -60 dB), the amplitude's a
  sample at a time, the filter's a period at a time. Velocity: how much the velocity sets the level (0: none).
- Notes: a note-on takes a free voice; with 16 sounding, the quietest releasing one (else the oldest) fades out over 4 ms
  while the new note takes another of the 24 voices. Mono and Legato play one voice, remembering the keys held (the newest
  plays; when it ends, the newest still held, gliding back): Mono strikes the envelopes again from where they are, Legato
  only when no key was held; Glide (constant time, linear in pitch) in Mono always, in Legato between held keys, in Poly
  from the last note's pitch. A changed Mode fades out what plays. A `NoteBend` bends its note's voice as the Synth's does
  (±48 semitones, gliding 1 ms); MIDI pitch bend reaches every voice over Bend Range; CC64 (sustain) holds released notes;
  CC120 fades everything out, CC123 releases it.
- Focus: detuned saws have detuned fundamentals, which beat against each other and differ left and right, so a wide
  stack's low end swells, thins and drifts. With Focus on, each voice also makes a *core* (`Voice::core`, mono, an
  accumulator like the stacks'): a saw per stack at the stack's own pitch (Transpose, Fine, the note, its glide and
  bends; no detune, no drift), starting in phase with its middle saw, at the stack's level and switch (a gain gliding
  as the saws' do: the power of the whole stack, which is unit power), and the sub. The core goes through the voice's
  filter (sections of its own, the same coefficients) and envelope into a mix of the cores. After the voices, per
  part of a period, a Linkwitz-Riley crossover (`dsp::Crossover`, 24 dB an octave) at Focus Freq keeps the stacks'
  highs on each side and the cores' lows in the middle: its two sides add up flat (an all-pass), so a single saw in
  the middle comes out as loud as without Focus, at every harmonic, and the sub, in both, is kept whole. At C2 with
  Detune 60 % and Focus at 300 Hz, the fundamental, which swings 13 dB a quarter second at a time without it, holds
  within 0.05 dB, and what of it is to the sides is more than 40 dB down; the 20th harmonic up is as it was. Focus
  Freq glides as the cutoff does (5 ms, in octaves), the crossover's coefficients made again at each update.
  Switching Focus crossfades the stacks as they are with the focused sound over 20 ms (`kFocusSeconds`); the cores are
  made, and the crossovers run, only while it is on or fading (starting from rest when it comes in again), so with
  Focus off the device does what it did before, at the same cost. On, it costs about 4 % more at the most (16 notes of
  three stacks of 64). No latency: the crossover is recursive.
- Tilt: a saw has every harmonic, and a stack's detune, in cents, spreads its k-th harmonics k times as far in hertz:
  one Detune for all of them. Tilt sets a stack's spread below Tilt At (a harmonic of the note: its glide and bends,
  not a stack's Transpose) against above it. Above 0 the lows' detune narrows, to none at 100 % (the first harmonics
  steady, the top blooming); below 0 the highs' does (a wide, beating bottom under a clean top). Detune is the wider
  of the two. While a stack that is on is tilted, every saw of every stack plays twice, from the same phase: at the
  lows' detune (`Stack::lowScale`, of its places, into the accumulator) and at the highs' (`highScale`, `ratioHigh`,
  `StackVoice::phaseHigh` and `incrementHigh`, into `Voice::accHigh`), both at the saw's gains and drift; a stack
  not tilted plays the same twice. Each voice joins them before its filter with Linkwitz-Riley crossovers
  (`Voice::tiltCrossovers`, 24 dB an octave) at Tilt At times its pitch (`tiltSplit`, made at each update, up to
  0.45 of the oscillators' rate): the lows of the one, the highs of the other. So the change is gradual: a harmonic
  an octave from Tilt At hears the other band 24 dB down, two octaves away 48 dB. The scales glide as the places do
  (12 ms), Tilt At as the cutoff (5 ms, in octaves). Tilt coming in crossfades the saws as they are with the tilted
  sound over 20 ms (`tilt_`, as Focus's), the high saws starting where the saws are and the crossovers from rest;
  going, the same back, and then nothing of it runs. With no stack tilted the device does what it did before, at
  the same cost; tilted, the oscillators cost twice as much (16 notes of three stacks of 64 then take nearly half
  a core at 48 kHz, against a quarter). The editor's graph draws each saw at Detune's place (the wider of the two).
- Latency: the step's 9 oscillator samples and the decimator's delay, (9 + delay) / 2 samples (47 at 44.1 kHz, 28 at
  48 kHz); 9 at the rate itself. The engine compensates it like any device's. `tailSamples()` is the release and the
  latency.
- It *writes* its output: it is first on a MIDI track. Silent for 0.25 s with no voice, it sleeps (writes silence, skips
  the work) until an event comes, waking with every glide at rest.
- Its cost: the oscillators are most of it, about 0.3 ns a saw a sample at twice the rate (16 notes of three stacks of 64,
  3072 saws, take about a fifth of a core at 48 kHz; four notes of seven, under 1 %).
- Displays (for its editor): `output`, the output (left and right averaged) a value a sample, and `voices`, how many
  notes sound, every 256 samples.

### Vital (`builtin:vital`, Instrument)

[Vital](https://github.com/mtytel/vital)'s own synthesis engine (vendored in
[engine/third_party/vital](../../engine/third_party/vital), GPLv3: [licensing.md](../licensing.md)) as a built-in
device. What the user sees is in [guide/devices.md](../guide/devices.md#vital).

**Building Vital without JUCE.** Vital's synthesis code (`src/synthesis`, without `effects_engine`, VitalFX's) and
the parts of `src/common` it and its presets need are vendored as they are upstream, and built as Vital builds them,
as two unity files (the `vital` static library, [engine/CMakeLists.txt](../../engine/CMakeLists.txt): the engine's
SIMD flags, no fast math, warnings off). The files include `JuceHeader.h`; [compat/JuceHeader.h](../../engine/third_party/vital/compat/JuceHeader.h)
stands in for it with what they use (the leak detector and non-copyable macros, a `String` over `std::string`,
`StringArray` and `File` for tuning files, `Base64`, `MemoryOutputStream`, switching denormals off) and the standard
headers JUCE's brings in. `load_save` is trimmed to the presets' upgrade and the wave data conversions (the rest is
Vital's application), and the common unity file leaves out the application's files: the changes are in
[local-changes.patch](../../engine/third_party/vital/local-changes.patch). Only `VitalPatch.cpp` and `devices/Vital.cpp`
include Vital's headers (through `VitalEngine.h`); `sub_engine` links `vital` privately.

**Parameters.** Every parameter of Vital's that its engine has (772: `vital::Parameters`, without the few its older
versions had, which its presets' upgrade moves elsewhere), in Vital's order, by Vital's ids (`osc_1_level`,
`filter_1_cutoff`, `modulation_12_amount`), so presets, automation keys and Vital's own names agree. Values are
Vital's raw ones (what its knobs are linear in and its presets hold); no `logScale`. A value Vital names
(`string_lookup`) is a list (`valueLabels`) where its names start at 0 and name every value, else steps. Every one is
`ownText`: `paramText()` gives what Vital shows (`SynthSlider::getAdjustedValue` and `formatValue`: the scale
(quadratic, cubic, quartic, square root, 2^x), the offset, inverted, multiplied, five characters, Vital's units;
"1.000 secs", "50.00%", "-6.020dB"), a cutoff (8..136, a MIDI note) in hertz ("440 Hz", "3.52 kHz"), a list's
names as far as Vital's list has them (a few ranges run past their names: the filters' styles, whose names depend on
the model in Vital's window). Vital's name lists are found by their names among `synth_strings.h`'s, shortest first,
since Vital's `ValueDetails` don't say how long theirs are. The same scale, offset, inversion, multiplier and units
are the parameter's `display` (`ParamDisplay`, in Processor.h), as data, for what reads and sets values as they show
(the MCP server): the parameters Vital shows as they are have none. An oscillator's distortion types name the other
oscillators the FM and RM ones take, as Vital's editor does ("FM <- Osc 2", "FM <- Osc 3" for the first;
`ProducersModule::getFirstModulationIndex` and `getSecondModulationIndex`): Vital's strings name neither, which made
two choices alike. Hidden (the host's, or of Vital's window): `bypass`,
`beats_per_minute` (the song's tempo is set every block), `mpe_enabled`, `view_spectrogram`, `osc_N_view_2d`. Not
automatable: `oversampling` (it rebuilds the engine) and `effect_chain_order`.

**The patch besides its parameters** is the device's state values (`VitalPatch.h`): `modulations` (Vital's list of
`{source, destination[, line_mapping]}`, slot by slot: slot *i*'s amount and switches are `modulation_<i+1>_*`),
`wavetables` (the three oscillators' `WavetableCreator` states), `lfos` (the eight `LineGenerator` states), `sample`,
`tuning`, and as text `preset_name`, `author`, `comments`, `preset_style`, `macro1`..`macro4`: what a `.vital` file
holds in its `settings` beside the parameters, as Vital writes it. A value left out is Vital's init patch's (a saw
wavetable, triangle LFOs, noise as the sample, no modulations, 12-tone equal tuning). `setStateValues()` reads them all
before it builds anything, and throws if one can't be read (the device keeps its patch); unknown names are dropped.

**Engines.** A `vitalpatch::Engine` is Vital's `SoundEngine` with what Vital's `SynthBase` keeps beside it (the
oscillators' `WavetableCreator`s, the `Tuning`), built whole by `buildEngine()` as Vital loads a preset
(`LoadSave::jsonToState`): every control set, the modulations connected as `SynthBase::connectModulation` connects them
(at their slots; one Vital's engine has no source or destination for is left out), the sample, the wavetables rendered,
the LFOs; then the rate, the oversampling and the modulation switches (as Vital's plug-in's `prepareToPlay`). Building
one allocates and takes tens of milliseconds, and Vital's engine isn't made to be changed while it plays, so the device
never changes one the rendering thread has: it builds a new one and hands it over.

- `setStateValues()` (the main thread or the thread loading states) builds an engine for the new patch at the current
  parameters, under the patch's mutex (so the newest build is the one handed over), and puts it in `pending_` (an
  atomic pointer; one not yet taken is freed).
- `idle()` (main thread) frees the engine the rendering thread let go of (`retired_`), and builds a new one when the
  `oversampling` parameter or the rate isn't the newest engine's.
- `prepare()` (audio stopped) takes the pending engine as the live one, builds the first, or sets a new rate on the
  live one directly.
- `render()`, before each block: takes the pending engine once the last one let go of has been freed and the one
  before has faded out. The old one fades out over 480 samples (10 ms at 48 kHz) beside the new, then goes to
  `retired_` for the main thread to free; the notes held play on the new one (struck again: Vital's voices can't move
  across), with the pedals and each channel's bend. Nothing is allocated or freed on the rendering thread.

**Rendering.** Parameters go to the engine's controls between blocks (`Value::set`, only those that changed: a copy
of what each was last set to; all of them on a new engine), as Vital's host sets them; the mod and pitch wheel
parameters reach every channel. The song's tempo is set every block, and while playing its position (`correctToTime`,
in seconds from the beat position) so synced LFOs and delays follow the song, as Vital's plug-in does. A block is
rendered in stretches of up to Vital's largest (128 samples, `vital::kMaxBufferSize`), each ending where a bend or a
MIDI message comes after its start: note-ons and note-offs go in at their sample offsets in the stretch (Vital's
`noteOn(note, velocity, sample, channel)`), bends and messages at the stretch's start. The output is Vital's stereo
(the `poly_float` lanes 0 and 1), written (not added): it is the first device on a MIDI track. After a second under
-120 dBFS with no voice it sleeps (writes silence, skips the work) until a note comes; `reset()` is Vital's
`allSoundsOff()`. `tailSamples()` is 10 seconds (its releases and effects' tails have no bound it can name).

**Displays** (for its editor, [ui/device-view.md](../ui/device-view.md)): `output`, the output (left and right
averaged) a value a sample; and every 256 samples `voices` (how many play), `lfo_1`..`lfo_8` (the newest voice's
LFO phases, 0..1: Vital's status outputs `lfo_<n>_phase`, decoded from their phase-and-voice encoding),
`env_1`..`env_6` (its envelopes' phases: Vital's stage, 1 delay, 2 attack, 3 hold, 4 decay (sustaining at its end),
5 release, plus how far into it) and `env_1_value`..`env_6_value` (their levels, 0..1); an LFO's or envelope's is -1
while no voice plays. The status outputs are found when the engine is built (`Engine::lfoPhases`, `envelopePhases`,
`envelopeValues`), so the rendering thread only reads them. Asleep, it publishes nothing.

**Notes and MIDI.** Each note gets a MIDI channel of its own, 1 to 15 in turn, as an MPE controller would give it (up
to 256 notes held: a table of note id, key, channel and velocity; the oldest gives way). A `NoteBend` sets that
channel's pitch wheel (`setPitchWheel`), which Vital applies to the voices on it as their "local" pitch bend,
±48 semitones (`VoiceHandler::kLocalPitchBendRange`): that note alone, `bend` semitones from its key, as the Synth
does. A bend at its note-on's sample is set before the note starts (Vital takes a voice's bend as it starts it). A
note-off ends the note with its id (else the oldest on its key and channel). MIDI messages apply to every channel:
pitch bend (`setZonedPitchWheel`, over Vital's Pitch Bend Range), CC1 the mod wheel, CC64 sustain, CC66 sostenuto,
CC74 (MPE's slide), channel pressure, polyphonic pressure (the newest note on its key), CC120 all sound off, CC123 all
notes off.

**Presets.** `readPreset()` reads a `.vital` file's JSON: one saved by an older version is upgraded as Vital upgrades it
(`LoadSave::updateFromOldVersion`), one by a newer version read for what it shares; every parameter (those it doesn't
have at their defaults, the rest clamped to their ranges), the state values, its names (named by the file if it has
no name). `writePreset()` builds an engine for the patch and writes what Vital's `LoadSave::stateToJson` writes: every
parameter, the 64 modulation slots, the wavetables, the LFOs and the sample as the engine has them (the init patch's
where the patch leaves them out), the tuning if it isn't the default, the names; Vital opens it. Wavetables come from
audio files as Vital loads one dropped on its wavetable (`WavetableCreator::initFromAudioFile`: spliced into frames,
blending as the file's `clm ` chunk says; vocoded; pitched), from `.vitaltable` files, or as basic shapes (a
`LineGenerator`'s); LFO shapes are `LineGenerator`s. What the editor draws comes from Vital's code too: `wavetableFrames()` renders an
oscillator's wavetable as its engine does (`WavetableCreator::render()`: its frames, as many as it has, each sampled
to as many points as asked) and `lfoShape()` an LFO's values across a cycle and its points. The application layer reads and writes presets and edits the
patch through these ([app/model.md](../app/model.md#vital)).

### Sampler (`builtin:sampler`, Instrument)

Plays one audio file as Ableton's Simpler does, in one of three modes. Up to 32 voices
sound at once (`kMaxVoices`), plus 8 slots for voices cut short (`kVoiceSlots`): a voice
stolen, or a mono note replaced, fades out over 4 ms (`kKillSeconds`) instead of
clicking.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `mode` | Mode | | Classic, 1-Shot, Slice | Classic |
| `root` | Root Key | note | 0..127, 127 steps | 60 |
| `tune` | Transpose | st | -48..48, 96 steps | 0 |
| `fine` | Detune | ct | -100..100 | 0 |
| `start` | Start | % | 0..100 | 0 |
| `end` | End | % | 0..100 | 100 |
| `gain` | Gain | dB | -24..24 | 0 |
| `reverse` | Reverse | | Off, On | Off |
| `snap` | Snap | | Off, On | Off |
| `warp` | Warp | | Off, On | Off |
| `warp_beats` | Warp Length | beats | 1..256, whole beats | 4 |
| `warp_mode` | Warp Mode | | Transients, Standard, Smooth, Formants, Re-Pitch | Standard |
| `loop` | Loop | | Off, On | Off |
| `loop_start` | Loop Start | % | 0..100 | 0 |
| `loop_fade` | Loop Fade | % | 0..100 (of the loop) | 0 |
| `attack` | Attack | ms | 0.1..5000, log | 1 |
| `decay` | Decay | ms | 1..10000, log | 1000 |
| `sustain` | Sustain | % | 0..100 | 100 |
| `release` | Release | ms | 1..10000, log | 50 |
| `voices` | Voices | # | 1..32, whole | 32 |
| `glide` | Glide | ms | 0..2000 | 0 |
| `trigger` | Trigger Mode | | Trigger, Gate | Trigger |
| `fade_in` | Fade In | ms | 0.1..2000, log | 0.1 |
| `fade_out` | Fade Out | ms | 0.1..2000, log | 0.1 |
| `slice_by` | Slice By | | Transient, Beat, Region | Transient |
| `sensitivity` | Sensitivity | % | 0..100 | 50 |
| `slice_beat` | Slice Division | | 1/16, 1/8, 1/4, 1/2, 1 Bar, 2 Bars, 4 Bars | 1/8 |
| `regions` | Regions | # | 2..64, whole | 8 |
| `playback` | Playback | | Mono, Poly, Thru | Mono |
| `filter` | Filter | | Off, On | Off |
| `filter_type` | Filter Type | | Low-pass, High-pass, Band-pass, Notch | Low-pass |
| `filter_slope` | Filter Slope | | 12 dB, 24 dB | 24 dB |
| `filter_freq` | Filter Freq | Hz | 20..22000, log | 22000 |
| `filter_res` | Resonance | % | 0..100 | 0 |
| `lfo` | LFO | | Off, On | Off |
| `lfo_wave` | LFO Wave | | Sine, Triangle, Saw Up, Saw Down, Square, Random | Sine |
| `lfo_sync` | LFO Sync | | Off, On | Off |
| `lfo_rate` | LFO Rate | Hz | 0.01..30, log | 1 |
| `lfo_beats` | LFO Synced Rate | | 1/32, 1/16, 1/8, 1/4, 1/2, 1 Bar, 2 Bars, 4 Bars, 8 Bars | 1/4 |
| `lfo_retrig` | LFO Retrigger | | Off, On | Off |
| `lfo_volume` | LFO > Volume | % | 0..100 | 0 |
| `lfo_pitch` | LFO > Pitch | ct | 0..1200 | 0 |
| `lfo_filter` | LFO > Filter | % | 0..100 | 0 |
| `lfo_pan` | LFO > Pan | % | 0..100 | 0 |
| `pan` | Pan | | -1..1 | 0 |
| `velocity` | Vol < Vel | % | 0..100 | 50 |
| `volume` | Volume | dB | -60..6 | 0 |

The defaults are the Sampler before it had modes (Classic, everything else off), so a
project saved then plays as it did: a parameter it doesn't have takes its default.

A `NoteBend` moves the voice playing its note (by note id, else a voice on its key) to `bend` semitones from its
pitch, in every mode (a slice too), held to ±48. The voice glides there a chunk at a time with the same
millisecond one-pole as the Synth (`dsp::kBendGlideSeconds`), and the bend adds to its pitch as Transpose and the LFO
do. A legato note that takes over a mono voice takes it over at its own pitch (its bend back to 0); letting go of it
glides back to the newest key still held, whose bends then find the voice by its key (it drops the note id).

- **The sample as it plays** (`Reader`, built per block): frames are counted in the
  order the sample plays, so Reverse reads it backwards (`frames - 1 - i`) and every
  marker (Start, End, Loop Start, slices) is a place in it as it plays. Outside the file
  it is silent. Start and End are `percent / 100 * frames` (truncated); with Snap each
  moves to the nearest zero crossing of the channels summed within 10 ms
  (`slicing::nearestZeroCrossing`, the earlier of two as near; never off either end).
- **Modes**:
  - *Classic*: a voice plays from Start, pitched by `key - root`; with Loop (and End
    after Start) it wraps from End to `max(Loop Start, Start)` (snapped), else it ends
    at End. Loop Fade crossfades the loop's last `fade` frames (`loop_fade` % of the
    loop, at most as many as come before Loop Start) into the frames before Loop Start,
    equal power, so the frame after End is the one at Loop Start. ADSR as the Synth's.
    Voices limits the notes: the quietest releasing one, else the oldest, is cut short
    to make room. One voice: a new note cuts the last; with Glide too, notes overlapping
    are legato: the voice goes on, its pitch moving linearly (in semitones) to the new
    key over the glide time, and back to the newest key still held when the one playing
    is let go (the keys held are kept in order, even while there is no sample).
  - *1-Shot*: one voice at a time, pitched; the envelope is Fade In (linear from 0),
    then held. Trigger ignores the note-off; Gate fades out over Fade Out (linear) from
    it. Either way it fades out linearly over the Fade Out before End, in played time.
  - *Slice*: at a note-on the slices of Start..End are worked out
    (`slicing::sliceStarts`): every Onset with a strength of at least `1 - sensitivity`
    and at least 10 ms from the slice before and from End (Transient); the region's
    beats (`warp_beats * (End - Start) / frames`) divided by the division (Beat); or
    `regions` equal parts (Region); always Start first, at most 128. Key 36 (C1) plays
    the first, each key up the next; keys below or past the last play nothing. A slice
    plays from its start to the next's (Thru: to End), at the root's pitch plus
    Transpose and Detune. Mono and Thru cut the slice before; Poly limits notes as
    Classic does. Fades, Trigger and Gate as 1-Shot's; with Snap, slices snap too.
- **Transients** (`slicing::detectOnsets`, at load, both ways): the sample summed to
  mono; per hop of 128 samples at 48 kHz (`sampleRate / 375`), the energy over two hops,
  of the whole band and of the first difference (the highs), in dB floored 60 dB below
  the loudest; a hop's rise is how far either band climbs above the loudest of the
  three hops before. Rises of 3 dB or more that are the biggest within 30 ms either side
  are transients; each starts at the first sample around it reaching a quarter of its
  peak, moved back to the zero crossing before it (within 2 ms). Strength: its rise
  over the biggest rise.
- **Pitch and speed**: unwarped, a voice steps through the sample at `(file rate / engine
  rate) * 2^(semitones / 12)` (4-point Hermite interpolation; the loop's wrap and its
  crossfade read through `Reader::at`, so the interpolation is continuous across it),
  `semitones` being its pitch plus `round(tune) + fine / 100` plus the LFO's. Warped,
  the whole sample lasts `warp_beats` beats at the block's tempo: `rate = frames /
  (beats * 60 / tempo * sampleRate)`. Re-Pitch resamples at `rate * 2^(semitones / 12)`;
  the other modes stretch: each voice gets a Signalsmith stretcher of its own, configured
  with the clips' block sizes for that mode and split computation (each block's work
  spread over the interval after it; [warp.md](warp.md); Formants keeps the formants),
  fed `rate` frames a frame from where it plays (wrapping a Classic voice's loop; silent
  past its end), transposed by `semitones`. A note-on seeks it (`outputSeek`, its
  latency computed ahead), so its first frame is the slice's or Start's at once. Faster
  than 4 times (`kMaxStretchRate`: a long sample in few beats) notes are resampled, as
  a stretcher's work grows with the rate; a stretched note that gets there as it plays
  (Warp Length or the tempo changing) gives its stretcher up and goes on resampled.
- **Stretchers** are made on the main side (`updatePool()`: `idle()`, `prepare()`,
  `resetOffline()`), only while Warp is on with a stretching mode: one for a mono mode,
  else as many as Voices allows up to 8 (`kMaxStretched`, which then limits the notes
  too), plus 2 for notes fading out; each is about 0.9 MB. They are handed to the
  rendering thread as samples are (`pendingPool_`, `retiredPools_`); voices of the pool
  it replaces fade out (resampled). `resetOffline()` makes fresh ones (with the fixed
  seed), and `reset()` counts notes from 0 again (a Random LFO restarted by notes takes
  its values from them), so offline renders come out the same every time. Until they come (or with none free) a warped note
  is resampled as Re-Pitch would.
- **Filter**, on the voices' mix: TPT state-variable sections (Zavalishin/Simper);
  12 dB one, 24 dB two (low- and high-pass a Butterworth pair, Q 0.54 and 1.31; band-pass
  and notch two of Q 0.71). Resonance raises the resonant sections' Q towards 10
  geometrically (the last of a low- or high-pass; both of a band-pass or notch). The band-pass is normalized to unity at its centre. The cutoff glides to
  its target (5 ms, in log) a chunk of 32 samples at a time, limited to 0.45 of the
  sample rate; switched on, it starts at its cutoff from silence. After the last note
  the device goes on rendering until the filter has rung out.
- **LFO**, one for the device, evaluated at each chunk's ends: the gains glide between
  them, the pitch and cutoff take the middle. Sine, triangle, saw up and down, square,
  and random (a new value each cycle, from a hash of the cycle's number). Synced while
  the song plays and Retrigger is off, its cycles are the song's (`beatPos` over the
  rate in beats), so a note starting off the beat finds it where the song is; otherwise
  it runs at its rate (synced: from the tempo), and with Retrigger each new note starts
  it at the beginning of a cycle. Volume: `1 - amount * (1 - lfo) / 2`;
  Pitch: `amount * lfo` cents; Filter: the cutoff times `2^(4 * amount * lfo)`; Pan: added to the pan.
- **Output**: voices mix into the output (both sides into one channel if that's all
  there is, at half gain); then the filter; then Gain, the LFO's tremolo, the pan
  (balance, as Utility's) and Volume, all ramped over 20 ms. It writes, not adds.
  Velocity: gain = `1 - sensitivity * (1 - velocity / 127)`. `tailSamples()` is the
  longer of Release and Fade Out.
- **State**: `"sample"`: the file's path. `setStateValues()` stores the path, loads the
  file through `loadSource()` and finds its transients both ways, *without holding its
  mutex* (it may take a while), and publishes it, unless another state came meanwhile or
  it is the file already playing (notes go on). If the file can't be loaded it publishes
  nothing (silent until it can be loaded) and rethrows, but keeps the path in its state,
  so the project keeps it. Other state values it keeps as they came (`others_`), and gives
  back with its state: the application keeps the similar sounds' reference there
  (`"similar_reference"`, `"similar_reference_saved"`: app/src/model/SamplerState.h).
- **Handing the sample to the audio thread without a lock**: the main side puts a new
  `Sample` (the source and its transients) into `pending_` (an atomic pointer; a sample
  still pending and never taken is freed there). At the start of each `render()` the
  rendering thread takes it, pushes the one it replaces into `retired_` (an `SpscQueue` of
  8), and silences its voices (they played the old one). If `retired_` is full it waits
  for the next block. The main side frees retired samples (and stretchers) in `idle()`
  and whenever it publishes.
- **Display**: `position`, one value per 256 samples: where the newest note plays in the
  sample (0..1 of its length, as it plays), or -1 while none plays. The editor draws the
  playhead from it, and lights the slice it is in.

The engine bridge restores a built-in device's state in the background (`EngineBridge::setBuiltinState()`, on a pool
of one thread, so states are set in the order they came and the last one wins), since it may load files, and waits
for all of them before rendering offline (`waitForDeviceStates()`; `devicesReady()` says whether any are pending).
See [app/engine-bridge.md](../app/engine-bridge.md).

### Arpeggiator (`builtin:arpeggiator`, MidiEffect)

Plays the notes held one after another (see [MIDI effects](#midi-effects) for how its notes reach its chain).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `style` | Style | | Up, Down, Up-Down, Down-Up, Converge, Diverge, Played, Random, Chord | Up |
| `sync` | Sync | | Off, On | On |
| `rate` | Rate | Hz | 0.5..40, log | 8 |
| `synced` | Rate (Synced) | | 1/1, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 |
| `gate` | Gate | % | 5..200 | 50 |
| `octaves` | Octaves | | 1..4 (whole) | 1 |
| `swing` | Swing | % | 0..100 | 0 |
| `velocity` | Velocity | | Played, Fixed | Played |
| `fixed_velocity` | Fixed Velocity | | 1..127 (whole) | 100 |
| `hold` | Hold | | Off, On | Off |
| `follow_groove` | Follow Groove | | Off, On | Off |

- **Held notes** (up to 32, by key and channel, with their velocity and the order they
  came): a note-on with none held down starts a new chord (with Hold, in place of the
  held one) and the pattern from its start. A note-off lets its key go; without Hold it
  leaves the set. Raw MIDI passes on at its offset; per-note bends are dropped.
- **The clock**: synced while the song plays, the song's grid: step k at k note values
  from the song's start (`beatPos / note beats`), so it locks to the beat wherever play
  starts or loops (a jump realigns it). Otherwise (stopped, or free in Hz) a clock of its
  own, from 0 at the chord's first note. Step k plays when the clock reaches
  k + (k odd ? 0.5 × Swing / 100 : 0). A chord starting no more than a tenth of a step
  after a step (`kLate`) plays that step at once; otherwise the next.
- **Follow Groove**: synced while the song plays, with the project's groove (`ctx.groove`)
  and the tempo map, step k plays where `groove::place()` puts its beat (k note values,
  as key 60 plays it, at the default `Amounts`: Timing and Velocity 100 %, no Random or
  Quantize), in place of Swing, and each step's notes are as loud as the groove makes
  them on their key at that beat. Without a project groove, stopped or free, it plays as
  it would.
- **A step**: the pattern is the held notes low to high (Played: in the order they
  came), repeated an octave up for each octave (keys over 127 left out); `indexFor`
  picks one by the style and the steps played since the chord started (Up-Down and
  Down-Up don't repeat their ends; Converge: lowest, highest, second lowest…; Diverge:
  the same from the middle out; Random: a hash of the step, so renders repeat). Chord
  plays every held note an octave further each step. A note still sounding on the same
  key and channel ends first; each note gets an id of its own (`noteId`) and lasts
  Gate × a step (at the step's nominal length), at its key's velocity or Fixed's.
- **Switched off**: `endMidiNotes()` ends what it plays and forgets the held notes (it
  won't hear their note-offs). `reset()` forgets everything.
- **Display**: `step`, one value per 64 samples: the step (since the chord started, 0..15
  round and round) whose notes sound, or -1 while none does. Its editor lights that step.

### Groove (`builtin:groove`, MidiEffect)

Moves the notes reaching it in time with a groove (`groove::place()`: [music.md](../music.md#groove)), with their
velocities and lengths as the groove has them; the feature as a whole is in [groove.md](../groove.md). See
[MIDI effects](#midi-effects) for how its notes reach its chain.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `swing` | Swing | % | 50..75 | 50 |
| `step` | Step | | 1/8, 1/8T, 1/16, 1/16T, 1/32 (`groove::kSteps`) | 1/16 |
| `timing` | Timing | % | 0..200 | 100 |
| `velocity` | Velocity | % | -100..100 | 100 |
| `length` | Length | % | 0..100 | 0 |
| `random` | Random | % | 0..100 | 0 |
| `quantize` | Quantize | % | 0..100 | 0 |
| `early` | Allow Early | | Off, On | On |
| `latency` | Latency | | Off, On | Off |

- **The groove** it plays is its source, a state value (`stateValues()`): `source` is `swing`
  (Swing and Step: `groove::swing()`, made in place on the rendering thread, nothing
  allocated), `project` (the project's default groove, `ctx.groove`; none: the notes as they
  come) or `groove` (its own: `groove`, a groove's text, `groove::toText()`, read on the main
  thread and handed to the rendering thread without locks). `seed` is Random's seed. Other
  values (the application's `groove_id`: the library groove it is a copy of) it keeps and
  gives back unread. Timing, Velocity, Length, Random and Quantize are the amounts it plays the
  groove at (`groove::Amounts`); a note takes their values at the moment it comes in.
- **Later**: a note-on is held (up to 64 notes, `kMaxNotes`; one more passes at once) and
  played where the groove puts it; its note-off is moved by as much and by Length (the
  note's length times its step's, as far as Length says), at least a sample after its
  note-on, and its bends follow it by its note id. Raw MIDI passes at its time (with Latency, that much
  later, as the notes are). What waits
  is played in time order (4096 events at most), at one time note-offs first, then note-ons
  and raw MIDI, then bends. A note on a key and channel one of its notes still sounds on
  ends that one first. A note without an id finds its note-off by key and channel (the
  oldest held).
- **Early**: `earlyBeats()` is half a step (of the groove it plays) while Allow Early is on
  and Latency off, else 0. The notes handed early (`ctx.earlyEvents`) it places both ways,
  no earlier than half a step before their time nor before now, and remembers their ids
  (256 at most), so their note-ons and note-offs coming again in their time are ignored. A
  note handed early whose note-off comes before it started (a locate, stopping) is dropped:
  it never sounded. Notes not handed early (live input, the notes of a MIDI effect before
  it) move later only.
- **Latency**: on, it reports half a step at the last tempo it played at as its latency
  (`dsp::LatencyReport`), and plays every note (and raw MIDI) that much later, so delay
  compensation lets every note, live input too, move both ways.
- **Stopped** (`ctx.playing` false) notes pass as they come; the notes held that haven't
  sounded when it stops are dropped. **Switched off**, `endMidiNotes()` plays the sounding
  notes' note-offs and drops the held ones.
- **Display**: `step`, one value per 64 samples: the step of the cycle the newest sounding
  note fell on, or -1 while none sounds. Its editor lights that step.

### Utility (`builtin:utility`, AudioEffect)

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `gain` | Gain | dB | -60..24 | 0 |
| `pan` | Pan | | -1..1 | 0 |
| `width` | Width | % | 0..200 | 100 |

Mid/side width (`side * width`), then gain times the balance pan's left and right gains (sine taper, unity at
centre), all ramped over 20 ms. On one channel it applies only the left gain.

### Over The Top (`builtin:ott`, AudioEffect)

A multiband upward and downward compressor in the style of the one every drop uses, with a single big *Soundgoodize*
knob (depth) and an output trim. At 0 % it passes the audio through untouched (apart from the crossovers' all-pass
phase, see below).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `depth` | Soundgoodize | % | 0..100 | 100 |
| `output` | Output | dB | -24..24 | 0 |

- Three bands split by Linkwitz-Riley crossovers at 88.3 Hz and 2.5 kHz, each half two Butterworth TPT state-variable
  filters in a row. The low band also goes through an all-pass equal to the upper crossover's LP + HP sum, so every
  band has the same phase shift. The dry signal is the bands summed unprocessed, so in-between settings don't
  comb-filter against the crossover's phase shift.
- Each band's envelope is followed on the louder channel (linked stereo) with its own attack and release (one-poles
  on the peak level). Above its downward threshold it is squashed at 66.7:1 (effectively a limiter); below its upward
  threshold it is pulled up at 1:4.17, Multiband Dynamics' law (`multiband::aboveGainDb`, `belowGainDb`: at most
  30 dB up, faded out between -72 and -96 dB so silence stays silent). Then each band's make-up.

| Band | Down threshold | Up threshold | Make-up | Attack | Release |
|---|---|---|---|---|---|
| Low | -36.1 dB | -42.0 dB | 12.7 dB | 0.77 ms | 84 ms |
| Mid | -29.1 dB | -41.8 dB | 5.4 dB | 0.66 ms | 59 ms |
| High | -33.4 dB | -36.1 dB | 8.3 dB | 0.55 ms | 25 ms |

- Depth scales each band's whole gain in dB (its make-up too), as Live's Amount does: 50 % is half the change in dB,
  not half the processed signal, and 0 % is 0 dB. The output trim applies last.
- The settings are fitted to renders of Live's OTT preset (Multiband Dynamics) on drums, a dubstep growl and a piano
  at Amount 25, 50 and 100 %: each band's level, 10 ms at a time, is within 0.7 dB of Live's on average (the earlier
  settings, the preset's panel values with slow detectors and a linear blend, were 2.8 dB off). Live's ratios fit
  as well as free ones and its thresholds land within a few dB of the panel's, but its detectors react far quicker
  than the panel's attack and release suggest: with them, a transient after a quiet stretch came through with the
  quiet stretch's lift. The renders were peak-normalized on export, which hides the overall level, so the make-ups
  are placed to average the preset's band outputs (10.3, 5.7 and 10.3 dB); the renders hold too little quiet mid to
  place its upward threshold, so it is the preset's. Loud material comes out quieter than it went in, as Live's does.

### Compressor (`builtin:compressor`, AudioEffect, sidechain)

A feed-forward soft-knee compressor, keyed by its own input or by its sidechain (a ducker, with another track there).
Linked stereo: both channels get the gain the louder one calls for.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `threshold` | Threshold | dB | -60..0 | -18 |
| `ratio` | Ratio | :1 | 1..20, log | 4 |
| `attack` | Attack | ms | 0.1..200, log | 10 |
| `release` | Release | ms | 5..2000, log | 150 |
| `knee` | Knee | dB | 0..24 | 6 |
| `makeup` | Makeup | dB | -12..24 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 100 |

- The key's level follows its peaks at once and falls at the release time, so it holds across a low note's cycles.
- The gain computer works in dB on that level: unchanged below the knee, the ratio's slope above it, and a quadratic
  blend across it. The gain reduction it asks for is smoothed with the attack as it grows (and follows at once as it
  falls).
- With a sidechain connected the key is what the sidechain hears (silence while solo leaves its source out); without
  one, the device's own input.
- Gain = `1 + mix * (10^((makeup - reduction) / 20) - 1)`, so Dry/Wet blends towards the untouched signal.
- **Displays**, one value per 256 samples (`kMeterSamples`): `input` (the key's peak in dB, floor -90), `reduction`
  (the most gain reduction in dB, positive) and `output` (the output's peak in dB). Its editor shows every knob at
  once and, beside them, the gain reduction over the last second and meters of what keys it (the threshold marked)
  and of its output.

### Gate (`builtin:gate`, AudioEffect, sidechain)

After Ableton's Gate: it passes only what is louder than the threshold and turns everything else down to the floor
(silence at the bottom of its range); flipped, only what is quieter passes. It is keyed by its own input or by its
sidechain, optionally through an EQ, and can play the key instead of its output, to hear what opens it. Its maths
the editor shares is in [GateDesign.h](../../engine/src/builtin/GateDesign.h) (namespace `gate`).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `threshold` | Threshold | dB | -70..6 | -12 |
| `return` | Return | dB | 0..24 | 3 |
| `attack` | Attack | ms | 0.02..150, log | 3.5 |
| `hold` | Hold | ms | 1..1500, log | 10 |
| `release` | Release | ms | 0.1..3000, log | 15 |
| `floor` | Floor | dB | -75..0 (-75: silence, shown as -inf) | -40 |
| `lookahead` | Lookahead | | 0 ms, 1 ms, 10 ms (not automatable: it is the latency) | 1 ms |
| `flip` | Flip | | Off, On | Off |
| `sc_gain` | S/C Gain | dB | -70..24 | 0 |
| `sc_mix` | S/C Mix | % | 0..100 | 100 |
| `sc_listen` | S/C Listen | | Off, On | Off |
| `sc_eq` | S/C EQ On | | Off, On | Off |
| `sc_eq_type` | S/C EQ Type | | Low Shelf, Bell, High Shelf, Low-pass, Band-pass, High-pass | High-pass |
| `sc_eq_freq` | S/C EQ Freq | Hz | 30..15000, log | 80 |
| `sc_eq_q` | S/C EQ Q | | 0.1..12, log | 0.71 |
| `sc_eq_gain` | S/C EQ Gain | dB | -15..15 | 0 |

- **The key** (what opens it): without a sidechain, its own input. With one, the sidechain times S/C Gain blended
  with the input by S/C Mix (100 %: the sidechain alone; 0 %: the input alone); silence while solo leaves its source
  out. The blend glides over 20 ms when a sidechain is chosen and back to the input when it goes (once removed, the
  sidechain's sound is gone at once, as when its source stops). S/C Gain and Mix ramp over 20 ms. A sidechain sample
  that isn't audio (NaN, infinity, beyond ±1e30: what a broken source can leave in the engine's buffer, which
  `BuiltinProcessor` cleans only for the device's own input) is taken as silence where the key reads it, so it never
  reaches the EQ's state or the output.
- **The key EQ** (S/C EQ On): one RBJ biquad per channel (`gate::keyFilter`, `dsp::BiquadCoefficients`), the six
  types in Live's order; the shelves have the cookbook's plain slope (Q 0.7071: Q is the bell's width and the pass
  filters' resonance); the frequency is held below 0.45 of the rate. Freq and Q glide in log, Gain in dB (a 15 ms
  one-pole, the filter designed again every 32 frames while they move, counted per frame so automation splitting
  the block doesn't change its pace). Switched on, it fades in over 10 ms (and out likewise); a change of type
  crossfades over 10 ms from the old filter (its state kept) to the new. A filter that starts (switched on, or a
  new type) starts warm: the device keeps the key before the EQ for its last 100 ms (`gate::kWarmSeconds`), and the
  new filter is run from rest over it, for as long as its slowest pole takes to fall 60 dB (`gate::warmFrames`, at
  most those 100 ms), so it goes on as if it had been running all along and the fade is only ever between two
  settled outputs. Carried on from the old filter's state instead, a low shelf at 30 Hz overshot the key by about
  5 dB for tens of milliseconds, enough to open the gate. It runs over that history 16 frames a frame
  (`gate::kWarmPace`) until it has caught up with the key, so no one block pays for it all (100 ms is 19 200 frames
  a channel at 192 kHz: run at once, a 32-frame block cost 200 µs where it lasts 167 µs), and is heard only from
  then: the crossfade (or the fade in) starts that much later (it gains 15 frames on the key a frame: about 1.3 ms
  for the default high-pass, 6.7 ms at most), with the old filter (or the key unfiltered) heard until then. Nothing
  of it but that short history is kept while it is off.
- **Detection**: peak, linked stereo (the louder key channel's level opens the gate for both), with no smoothing
  besides the lookahead window; chatter on low notes is what Return and Hold are for, as in Live (the default 10 ms
  hold holds across a 50 Hz note's zero crossings).
- **Opening and closing**: it opens on the first sample whose level is at or above the threshold, stays open while
  the level is at or above the threshold less Return (hysteresis), then for Hold more, then closes. How far open
  it is moves linearly over Attack while open and over Release while closed, from wherever it is (opening again
  during a release ramps up from there). The hold counts up from the fall, so a Hold changed while it counts
  applies at once (shortened below what has passed, the gate closes there). Threshold, Return, Attack, Hold and
  Release need no smoothing: they move a decision or a ramp's speed, never the gain itself. Ableton's own ramp
  curves are unpublished; these are designed.
- **The gain**: the openness eased in and out (smoothstep, so the ramps start and end without a corner), flipped,
  then between the floor and unity, `1 - (1 - floor)(1 - pass)`: exactly 1 fully open, so an open gate without
  lookahead passes its input bit for bit. The ramps are linear in amplitude: at the default -40 dB floor a release
  spends most of its time in the top 20 dB, heard as a fade of about its length.
- **Flip** inverts what passes, not the detector (the state, Return and Hold work as unflipped): flipped, the attack
  is the sound being turned down as the key rises above the threshold, the release the sound coming back. Switching
  it fades over 10 ms. **Floor** is ramped over 20 ms; at its bottom (-75 dB) the gain is 0.
- **Lookahead** (0, 1 or 10 ms, in whole samples: 0, 48, 480 at 48 kHz) delays the audio, and the level is the key's
  largest over the lookahead window plus the current sample: the gate opens that much before a transient reaches
  the output, and the hold counts from when the fall reaches it. Each choice with a window has its own
  `dsp::SlidingMax`, exactly as long as it (at 0 ms the level is the key's peak as it is); a change refills the new
  one from a short delay line of the key's peaks, so its window holds what the key did before it was chosen, and
  crossfades (an S-curve, 10 ms) from the old delay tap to the new. `latencySamples()` is the lookahead (48 at the
  default, as Live's); `idle()` returns true once when it changes, so the engine realigns the tracks.
  `tailSamples()` is the lookahead too (what is still in the delay).
- **Listen** puts out the key (after S/C Gain, Mix and the EQ, delayed by the lookahead as the audio is) instead
  of the gated audio, crossfading over 10 ms; on one channel, the mean of the key's two channels. At either end of
  the crossfade the output is the one alone, so nothing of the key is in it while nobody listens.
- A change of lookahead or EQ type that comes during its crossfade (or while the new type's filter catches up)
  waits until it is done.
- **Channels**: two as described; on one, the key's right channel is the input itself (a sidechain still brings
  two). A channel that comes back after one-channel processing starts its delays from silence.
- **reset()** clears the delays, the detector and the EQ, and closes the gate; the next `render()` starts every
  ramp, the lookahead and the EQ at the parameters as they are then (whether a sidechain is connected is only known
  there), so an offline render's first transient sees the key as set. `prepare()` (a new rate) sizes the delays for
  10 ms and works out the ramps again.
- **Denormals**: the only recursive state is the key EQ's, and `dsp::Biquad` zeroes a section's two states together
  once both are below 1e-20 (low down or narrow, at 30 Hz and Q 0.1 or 12, the two nearly cancel: zeroing one alone
  would kick the other back up, and it would go on at about 1e-19 for minutes). So silence rings out to exact zeros
  as soon as the filter's slowest pole takes it there: 0.11 s for the default high-pass after loud noise, 5.5 s for a
  bell 15 dB down at 30 Hz and Q 0.1 (a real pole near 1 Hz), 12 s at most (a bell 15 dB up at 30 Hz and Q 12, the
  key 24 dB up), all of it below -300 dB after the first moments.
- **Displays**, one value per 256 samples (`gate::kDisplaySamples`), pushed together so they stay in step: `input`
  (the input's peak in dB as it reaches the gain, after the lookahead), `output` (the output's peak in dB, what is
  heard while listening too), `key` (the key's peak in dB, before the lookahead: what the threshold is compared
  with, so it leads `input` by the lookahead), all floored at -90 dB; and `open` (the mean of how much passed, 0 at
  the floor .. 1 fully through, after Flip). The editor draws the levels' history with the threshold and return
  lines, how far the gate turns the sound down, and the key EQ's curve (the response of `gate::keyFilter`, the
  engine's own biquad, through the application layer's [GateResponse.h](../../app/src/audio/GateResponse.h)).
- It costs about 0.13 % of one core at 48 kHz stereo at its defaults
  ([builtin_devices_bench](../../benchmarks/builtin_devices_bench.cpp)), 0.07 % without lookahead, and about 0.18 %
  with the key EQ (a Q 12 bell), 10 ms of lookahead, listening and flipped. A key EQ filter catching up costs 16
  biquad steps a channel a frame while it does (for at most 6.7 ms): the 32-frame block where it starts, about 7 µs
  against 1.2 µs, at any rate (run at once, it cost 30-55 µs at 48 kHz and 100-400 µs at 192 and 384 kHz).

### Limiter (`builtin:limiter`, AudioEffect)

A brick-wall lookahead limiter after Ableton Live 12.1's: no sample comes out above the ceiling, and in True Peak mode
no peak between samples does either. Gain pushes the input into it; Soft Clip rounds peaks off as they near the ceiling
instead of turning them all down; Routing limits left and right, or mid and side, with Link sharing the gain reduction
between the two; Maximize turns the ceiling into a threshold. Its latency is the lookahead.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `gain` | Gain | dB | -24..24 | 0 |
| `ceiling` | Ceiling | dB | -24..0 | -0.3 |
| `release` | Release | ms | 0.1..3000, log | 300 |
| `auto_release` | Auto Release | | Off, On | On |
| `lookahead` | Lookahead | | 1.5 ms, 3 ms, 6 ms (not automatable: it is the latency) | 3 ms |
| `mode` | Mode | | Standard, Soft Clip, True Peak | Standard |
| `routing` | Routing | | L/R, M/S | L/R |
| `link` | Link | % | 0..100 | 100 |
| `maximize` | Maximize | | Off, On | Off |
| `threshold` | Threshold | dB | -24..0 | -0.3 |
| `output` | Output | dB | -24..0 | -0.3 |

- **A normalized domain.** Everything runs where the line (the ceiling, or with Maximize the threshold) is 1
  (`limiter::scales()`, [LimiterDesign.h](../../engine/src/builtin/LimiterDesign.h)): `pre` multiplies the input before
  the lookahead and the detector, `post` the output. Without Maximize `pre` = gain / ceiling and `post` = ceiling; with
  it `pre` = 1 / threshold and `post` = output, so the gain is Output - Threshold, the Output is the ceiling, and Gain
  doesn't apply. At the defaults (Threshold and Output at the Ceiling's -0.3 dB, Gain 0 dB) switching Maximize on
  changes nothing. `pre` and `post` glide over 20 ms along a cubic in dB (`GainGlide`: from rest a smoothstep; a new
  target mid-glide carries the slope on, within Fritsch and Carlson's bound, so it never overshoots), so a level change
  has no slope kink where it starts or stops for limiting to turn into a click; and the two glide along the same curve,
  so their product stays the gain and moving the ceiling over material that doesn't reach it changes nothing (linear
  ramps would bulge by about 1 dB halfway). A cubic's second difference is constant: three products a sample, a log and
  three `exp` per new target. `post` and Soft Clip's amount are written into rings as long as the lookahead when their
  sample comes in and read back when it comes out, so each sample is detected and scaled with the same values: a change
  is a smooth level change and never breaks the brick wall. Input that isn't finite (NaN, ±inf, beyond 1e30) never
  reaches it: `BuiltinProcessor::process()` takes it as 0, as for every built-in device, so one bad sample can't
  silence the rest.
- **Detection**, 12 samples behind the input (`kDetectorDelay`), of left, right, mid and side. Standard and Soft Clip
  take each sample's magnitude. True Peak interpolates seven points between each sample and the next (24 taps each, a
  Kaiser-windowed sinc with β = 6, `dsp::besselI0` for its window, each phase passing DC exactly: flat within ±0.01 dB
  to 0.42 of the rate), and refines the largest of the eight by a parabola through it and its neighbours
  (`limiter::refinedPeak`, never below the point, within 0.002 dB of a sine's crest at 0.4 of the rate), aiming 0.02 dB
  under the ceiling (`kTruePeakMargin`). Its output, by a 32x reference meter, stays at or under the ceiling for content
  up to about 0.42 of the rate (20 kHz at 48 kHz). The seven dot products run as one vector of eight points per tap (AVX
  intrinsics, as compilers vectorize the plain loop across the taps with a shuffle per value; the plain loop elsewhere),
  and the parabola is skipped where 1.25 times the largest of the ten points around the sample is under the target (the
  most it can add).
- **Required gains.** L/R: each channel's own (target / peak, for peaks over it) blended by Link with the pair's (the
  lower). M/S: |left| and |right| are at most |mid| + |side|, so their sum must fit: own gains share the room out
  (`limiter::sharedCeiling`: the quieter keeps its level while the louder takes what is left; both meet at half when
  both are loud), the linked gain turns both down alike, by what left and right need: |mid| + |side| is the louder of
  |left| and |right| at every moment, between samples too, so at Link 100 % M/S is exactly L/R linked, in True Peak as
  well (the sum of the mid's and the side's own true peaks, found at different moments, would limit 0.4 to 1.2 dB more
  than needed). Any blend of feasible pairs is feasible, so every Link and Routing obeys the ceiling. What M/S costs: on
  wide, uncorrelated material each M/S pipeline holds its own worst sample, and their peaks rarely coincide, so the
  peaks that come out fall short of the ceiling: measured on uncorrelated noise driven 12 to 20 dB into it, 4.3 to 5 dB
  short at Link 0 % and 1.7 to 2 dB at 50 % for uniform noise (the densest peaks), about 1.4 dB at 0 % for Gaussian
  noise, none at 100 % or in L/R. On a loud centre with quieter sides the side keeps its level, which is what M/S is
  for.
- **Four pipelines** (L, R, M, S), each on the required depth (1 - gain): a hold over the attack's length S = L + 1 - 12
  samples (`dsp::SlidingMax`, one per lookahead, each exactly its window), a release (Release's one-pole; Auto: a
  one-pole whose time constant follows how dense the limiting has lately been, a 200 ms one-pole of whether it was
  limiting: 50 ms after a lone peak, slowing with that density's square to 600 ms under sustained limiting
  (`limiter::autoReleaseMs`; the coefficient from a table of 33, read in between linearly, no `exp` per sample). So the
  gain doesn't follow each cycle of a bass note's limited crests, which distorts it: a 30 Hz sine 6 dB over comes out
  with 0.40 % THD (a manual 300 ms: 0.56 %, 50 ms: 2.6 %), 50 Hz with 0.11 %; and once the limiting stops it speeds up
  again (a lone 5 ms peak 6 dB over is back within 0.25 dB 150 ms after; after 2 s of limiting by 6 dB, 3 dB are left
  150 ms after and none 600 ms after). Auto's switch is a 20 ms blend, and the release is at least the hold whatever the
  blend), then two box filters whose lengths add up to S. The gain each sample gets is an average of values that are
  each at least what that sample needs, so it is never above it, and reaches it exactly when a lone peak comes out,
  after an S-curve S - 1 samples long (nothing earlier). The boxes sum in fixed point (depths rounded up to multiples of
  2^-30, summed in 64 bits, the first's average divided out by integer ceiling division): exact, so they never drift,
  and silence brings the gain back to exactly 1. Both routings' pipelines always run.
- **The output** is the delayed input times the routing's gains (Routing crossfades between the two routings' outputs,
  each obeying the ceiling), through Soft Clip's knee, clamped to the line, times `post`.
- **Soft Clip**: a knee (`limiter::knee`, `shape`) linear up to 6.02 dB under the ceiling and a quadratic from there to
  the ceiling at 3.52 dB over it (slope 1 to 0: continuous in value and slope, never over); the limiter aims at that top
  instead of the ceiling, so peaks up to 3.52 dB over are rounded off (louder, with some crunch: a sine at the ceiling
  comes out at -1.16 dB with its 3rd harmonic at -27 dB) and only what goes further is turned down. Not oversampled, so
  every mode has the same latency and attack; what that costs: content above a sixth of the rate (8 kHz at 48 kHz) that
  itself reaches the knee folds its 3rd harmonic back as an inharmonic tone, 27 dB under it at the ceiling and 19 dB
  pushed 3 dB in (11 kHz → 15 kHz, 9 kHz → 21 kHz); below that only the 5th folds, 44 dB under or less. On a mix, whose
  bass carries the peaks, bright content riding through the knee up to about 13 kHz folds back 54 dB or more under
  itself; above that its sums fold into the top octave (about 20 kHz), 24 to 36 dB under it. (A 2x oversampled knee
  would take 5 to 31 samples from the attack or add them to the latency, and still leave about -29 dB pushed 3 dB in:
  the brick wall's clamp at the base rate clips what the decimation filter overshoots.)
- **Smoothing.** Gain, Ceiling, Threshold, Output and Maximize glide `pre` and `post` (20 ms in dB); Release sets its
  coefficient per stretch; Link glides linearly (20 ms, before the pipelines, which smooth it further); Mode's Soft Clip
  amount, Routing and Auto glide over 20 ms eased in and out (`dsp::sCurve`, a smoothstep: a crossfade has no kink
  where it starts or stops); switching to or from True Peak only changes what is measured, which the pipelines
  smooth.
- **Lookahead changes** fade the output out (2 ms, a smootherstep), start again from the new length (the delay line,
  rings, holds, boxes and releases cleared: fills of a few thousand values at most), wait for the line to refill and
  fade back in: a dip of about L + 4 ms, never a click. `latencySamples()` and `tailSamples()` are the lookahead (72,
  144 or 288 samples at 48 kHz); `idle()` reports a change once, so the engine realigns the tracks. `reset()` starts
  from the new length at once.
- **One channel**: only the left pipeline; Routing and Link don't apply. Channels past two are left untouched.
- **Denormals**: the release stages and Auto's density are flushed below 1e-9 and the boxes are integers, so silence
  comes out as exact zeros once the delay line has emptied, and the gain reduction returns to exactly 0 (with Auto about
  1.1 s after the input stops; manual, about 21 Release times).
- **Displays**, seven, one value per 128 samples (`kMeterSamples`), published together, all measured where the sample
  comes out (so a peak's input, reduction and output line up): `input_l`, `input_r` (the input's peak in the line's
  domain: dBFS after Gain, or the raw input with Maximize; floor -90), `output_l`, `output_r` (the output's peak in
  dBFS), `reduction_a`, `reduction_b` (the most gain reduction of the routing's two channels, L and R or M and S, in
  dB, positive, as the Compressor's `reduction`; exactly 0 when the gain stayed 1) and `clip` (what Soft Clip's knee
  took off the loudest peak, dB; exactly 0 in Standard and True Peak). One channel: the right's are the left's.
- **Shared with the editor** ([LimiterDesign.h](../../engine/src/builtin/LimiterDesign.h), through the application
  layer's [LimiterResponse.h](../../app/src/audio/LimiterResponse.h)): `limiter::scales` (the line, and Maximize's shift
  of the output), `softKneeDb` and `softTopDb` (Soft Clip's band), `kMeterSamples` (the history's time axis) and
  `kFloorDb` (the displays' silence), so the line and Soft Clip's band drawn are where the device limits and rounds
  off. `lookaheadSamples`, `autoReleaseMs`, `sharedCeiling`, the true-peak phases, `refinedPeak` and `reductionDb` are
  the device's own (and its tests'); `interpolate` (the phases applied one point at a time) is the tests'.
- On the machine it was written on (a 2.1 GHz Xeon, AVX2, `builtin_devices_bench`, 48 kHz stereo) it took 0.27 % of one
  core at its defaults (Standard), 0.32 % in Soft Clip with 12 dB of gain, 0.49 % in True Peak (0.65 % limiting 12 dB),
  and 0.91 % at its heaviest (True Peak, M/S at Link 50 %, 6 ms, 24 dB of gain).

### Multiband Dynamics (`builtin:multiband`, AudioEffect, sidechain)

After Live's Multiband Dynamics: up to three bands, each with two thresholds, *Above* and *Below*, and a ratio for
each, so one band can be compressed and expanded, downwards and upwards, at once
([MultibandDesign.h](../../engine/src/builtin/MultibandDesign.h)). Over The Top is one setting of it. A sidechain
keys each band by the same band of the key. Its fresh state (every ratio 1:1) does nothing: the bands summed, an
all-pass of the input. Ranges are Live's (its set files' parameters).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `xover_low` | Low-Mid Crossover | Hz | 30..3000, log | 120 |
| `xover_high` | Mid-High Crossover | Hz | 300..15000, log | 2500 |
| `low_on` | Low Band On | | Off, On (not automatable, as Live's split switches) | On |
| `high_on` | High Band On | | Off, On (not automatable) | On |
| `<band>_active` | `<Band>` Activator | | Off, On | On |
| `<band>_in` | `<Band>` Input | dB | -24..24 | 0 |
| `<band>_out` | `<Band>` Output | dB | -24..24 | 0 |
| `<band>_above` | `<Band>` Above Threshold | dB | -80..0 | -20 |
| `<band>_above_ratio` | `<Band>` Above Ratio | ratio | 0.25..100, log | 1 |
| `<band>_below` | `<Band>` Below Threshold | dB | -80..0 | -40 |
| `<band>_below_ratio` | `<Band>` Below Ratio | ratio | 0.25..100, log | 1 |
| `<band>_attack` | `<Band>` Attack | ms | 0.1..5000, log | 10 |
| `<band>_release` | `<Band>` Release | ms | 0.1..5000, log | 100 |
| `<band>_solo` | `<Band>` Solo | | Off, On (not automatable, as Live's) | Off |
| `amount` | Amount | % | 0..100 | 100 |
| `time` | Time | % | 10..1000, log | 100 |
| `output` | Output | dB | -24..24 | 0 |
| `soft_knee` | Soft Knee | | Off, On | Off |
| `mode` | Peak/RMS | | Peak, RMS | RMS |
| `sc_gain` | S/C Gain | dB | -70..24 | 0 |
| `sc_mix` | S/C Mix | % | 0..100 | 100 |
| `sc_listen` | S/C Listen | | Off, On (not automatable, as Live's) | Off |

The ten `<band>_...` rows come three times, for `low`, `mid` and `high` (named "Low Activator", "Mid Input", "High
Input"...), in that order after `high_on`: band b's field f is parameter 4 + 10b + f. A ratio R is stored as the
distance past the threshold the level change divides (at 4, a level 4 dB past it comes out 1 dB past it), and the
`ratio` unit prints it as Live does, "1:R" either side of 1:1, to three significant digits (`formatValue`:
"1:4.00", "1:66.7", "1:100"; under 1 "1:0.500", "1:0.250"). So Live's numbers mean the same here: its "1 : 66.7"
(Over The Top's Above) is 66.7.

- **The split**: two Linkwitz-Riley crossovers (`dsp::Crossover`, as Over The Top's) at Low-Mid and Mid-High, the
  low band through the upper crossover's all-pass (`dsp::CrossoverAllpass`), so the three add up to an all-pass:
  flat (within 0.01 dB) whatever their gains are when they are equal. The dry signal is never mixed in, so nothing
  comb-filters against the crossovers' phase. A Low-Mid at or above the Mid-High splits both there: the mid band
  narrows to a band around it (peaking at −12 dB), and the sum stays flat.
- **Low and High, the split switches, switch the outer bands off as Live's do**: the split doesn't change (nor the
  phase), but a band switched off takes the mid band's whole chain (Input, dynamics, Output) and its signal feeds the
  mid band's detector, so with both off the mid band shapes the whole spectrum as one band. The switches crossfade
  the gains; every band's detector, gain computer and smoothing keep running, switched off or not, so a band
  switched back on picks up where its own level is. Not automatable, as Live's. Solo: only the soloed bands are
  heard; a band switched off can't be soloed and follows the mid band's solo.
- **Each band's activator** (Live's band buttons, the Mid band's too) bypasses it: its Input, dynamics and Output
  glide to unity (its whole gain in dB scaled by the activator's glide), while its split stays, so its frequencies
  are its own, not another band's to shape; the device's Output still applies. A band switched off takes the mid
  band's gains, so with the Mid band bypassed nothing shapes it either. Its detector runs on.
- **Detectors**, linked stereo (the louder channel; one channel: that one), per band, in dB after the band's Input
  (Input drives the detector as it drives the audio, so the thresholds stay where the level is drawn):
  - Peak: the largest |x| over a window of the band's lowest period (`multiband::windowSeconds`: 25 ms for the low
    band, 1 / Low-Mid for the mid band (25 ms while the low band is merged into it), 1 / Mid-High for the high band,
    each within 1..25 ms; a `dsp::SlidingMax`). A rectified sine peaks every half period, so a steady tone holds its
    peak and isn't modulated (distorted); the level falls once a peak leaves the window, then the release takes over.
  - RMS: each channel's mean square through a one-pole of that window or 10 ms, whichever is longer, the larger of
    the two taken: the louder channel's power (the mean of the larger square each sample would read up to 2.1 dB
    hot on stereo whose sides differ). A sine reads 3.01 dB under its peak.
  - Both run all the time; Peak/RMS crossfades their levels (a glide), so switching never clicks.
- **The gain law** (`multiband::aboveGainDb`, `belowGainDb`, `bandGainDb`): past either threshold the distance from
  it is divided by the ratio, `out = T + (L − T) / R`; so over 1:1 a side narrows its region's range (Above:
  downward compression; Below: upward compression), under 1:1 it widens it (Above: upward expansion; Below: downward
  expansion). Soft Knee bends each side in over 6 dB around its threshold, quadratically (the Compressor's shape).
  Upward compression fades out between −72 and −96 dB, so silence, and whatever lies under −96 dB, isn't lifted; a
  noise floor above that is, as Over The Top's is (−80 dB under a Below of −40 at 1:4 comes up 20 dB). Each side's
  change is held to −96..+30 dB, and so is the band's (Amount times the sum of its two sides), so a gate-like
  expansion lets go from at most 96 dB down. Both sides apply as their formulas say even with Above set under Below.
- **Attack and release** as Live defines them, for each side on its own: a side's gain change (dB) moves with the
  attack while it grows and with the release while it shrinks (one-poles). So Above's attack is how fast it acts as
  the level rises past the threshold, Below's as the level drops under it, and each release how fast it lets go.
  Time scales every band's attack and release (10..1000 %).
- **Every control glides**: two one-poles of 5 ms in a row, so a jump eases in and out (a linear ramp's kinks show
  on a band's share of a tone), within 1 % after about 33 ms: thresholds, ratios (as their slopes), gains, Amount,
  Output, S/C Gain and Mix, Listen, Soft Knee (the knee's width), Peak/RMS, the band switches and activators, solos
  (each band's audibility) and the sidechain's connection (`dsp::Glide`, in double, a thin `Control` holding each
  one's target and its value as a float for the loop). Crossovers glide in log, a step per 32 samples, each crossover's
  `g = tan(πf/sr)` interpolated sample by sample between the steps and its coefficients remade while it moves; the
  windows follow per step. Attack, release and Time make new coefficients per stretch (a time constant changing
  can't click). Only glides still moving are stepped (in groups: the device's controls, and each band's), so a
  device left alone does no smoothing work. The 32-sample grid runs across `render()` calls, so the output and the
  displays don't depend on where automation cuts a block (bit-identical in blocks of 1, 13, 256 and 1024).
- **The sidechain** (`hasSidechain()`; Live 9's): the key, times S/C Gain, goes through a split of its own, so each
  band is keyed by the same band of the key (a kick keys the low band, a vocal the mid). S/C Mix blends what each
  detector hears from the device's own band (0 %) to the key's (100 %); at 0 % the output is exactly the unkeyed
  one. One main channel: the key is the mean of its two. Connected but silent (solo leaving its source out): the key
  is silence. A key sample that isn't audio (NaN, infinity, beyond `BuiltinProcessor::kMaxInput`) is taken as
  silence where it is read: the input is cleaned by `process()`, the key isn't (it is the engine's buffer). The
  key's split runs only while some of the key is heard (the sidechain connected, or fading in or out, and S/C Mix
  over 0 %: at 0 % it costs nothing), and is cleared once none is, so a key heard again starts from silence. Its
  connection glides, but after a `reset()` the first `render()` takes it as it is (the renderer sets the flag after
  resetting, and a reset has cleared every state, so nothing can click): an offline render after a sidechain was
  removed starts unkeyed.
- **Listen** (S/C Listen, as Live's headphones): the output crossfades (a glide) to what the detectors hear, the
  three bands of the trigger summed (an all-pass of it): the key after S/C Gain, blended with the device's own input
  by S/C Mix; without a sidechain, the input.
- **Denormals and the loudest input**: on the 32-sample grid the crossovers' states (and the key's), the mean
  squares and the gain changes are flushed to 0 below 1e-20 (`Crossover::flush()`, `dsp::flushTiny`), so silence
  rings out to exact zeros. The detectors' squares are held to 1e10 (+100 dB, far over any level the bands carry):
  the loudest input `process()` lets in, 1e30, squared would be infinite, and an infinite mean square would never
  come down; held there, the RMS detector is back from it within about a second.
- `reset()` clears every state and snaps every glide to the parameters; `prepare()` (a new rate) sizes the sliding
  maxima for 25 ms, works out the glides again and resets. `latencySamples()` is 0: no lookahead, as Live's (its
  latency article lists lookahead only for Compressor, Gate and Limiter). `tailSamples()`: the crossovers' ringing,
  14 time constants of the slowest Butterworth pole, `ceil(14 √2 / (2π f) · sr)` with f the lower crossover (105 ms
  at 30 Hz, 26 ms at 120 Hz); the dynamics add none.
- **Displays**, one value per `multiband::kDisplaySamples` (256) each, all nine published together:

  | id | Each value |
  |---|---|
  | `low_in`, `mid_in`, `high_in` | the band's detector level (dB, after Input; Peak or RMS as set; the trigger's when keyed), the largest over the 256 samples, −90 floor |
  | `low_out`, `mid_out`, `high_out` | that level plus the gain change the dynamics are applying (before the Outputs): the static curve's output for it once attack and release have settled, the largest, −90 floor |
  | `low_gain`, `mid_gain`, `high_gain` | the dynamics' gain change (dB, signed: negative a cut, positive a boost; Amount and the limits applied), the one of largest magnitude |

  A band switched off (done fading) publishes −90, −90, 0 (the mid band shows what it does). A band bypassed shows
  its level as it comes (no Input) and no change. With every ratio 1:1, in and out are the same.
- **The shared design**: [MultibandDesign.h](../../engine/src/builtin/MultibandDesign.h) (`sub::multiband`, inline, no
  Qt) holds the ranges, the display rate (`kDisplaySamples`), `slope`, `upwardFade`, `aboveGainDb`, `belowGainDb`,
  `bandGainDb`, `staticGainDb` (the curve a steady level settles on) and `windowSeconds`; the device runs these very
  functions per sample. The application layer's [MultibandResponse.h](../../app/src/audio/MultibandResponse.h)
  (`multibandGainDb`; the ranges, `multibandMinThresholdDb()`... `multibandMaxRatio()`; and `multibandParseRatio`,
  `multibandParseMs` for typed ratios and times) hands them to the editor, so its readouts and handles are the sound.
  (The displays' rate reaches the editor with them: `DeviceCanvas::readRecent` takes it from the device's `displays()`.)
- About 0.66-0.69 % of one core at 48 kHz stereo at the defaults, and 0.70-0.78 % with every band's ratios working, Soft
  Knee and Peak (`builtin_devices_bench`, best of three, on a 2.1 GHz Xeon; Over The Top 0.40 % there). A sidechain
  keying it brings it to about 1 % (none at S/C Mix 0 %, where the key's split doesn't run), and everything at once (a
  sidechain, both crossovers swept all the time, Peak/RMS switched every 50 ms so it is nearly always crossfading) to
  about 1.05 %.

### Spectral Compressor (`builtin:spectral`, AudioEffect, sidechain)

A compressor per frequency, after the best-known spectral dynamics processors (nih-plug's Spectral Compressor,
soothe2, M-Compressor), in Live's terms: it splits the sound into about a thousand bins (a short-time Fourier
transform), measures each bin's level, and compares it with a threshold curve that follows a pink spectrum
([SpectralDesign.h](../../engine/src/builtin/SpectralDesign.h)). Each bin is turned down above the curve (Ratio)
and, if Upward is past 1:1, brought up below a second curve (Below), so resonances and harsh regions are tamed where
they occur and the rest is left alone; with Upward too the spectrum is pulled towards the curve. Keyed by a
sidechain, the key's spectrum sets the gains: one track's frequencies duck where another's clash with them.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `threshold` | Threshold | dB | -72..12 | -18 |
| `ratio` | Ratio | :1 | 1..20, log | 2 |
| `below` | Below | dB | -72..12 | -48 |
| `upward` | Upward | :1 | 1..10, log | 1 |
| `tilt` | Tilt | dB/oct | -6..6 | 0 |
| `knee` | Knee | dB | 0..24 | 6 |
| `range` | Range | dB | 0..48 | 24 |
| `smooth` | Smoothing | % | 0..100 | 40 |
| `focus_lo` | Focus Low | Hz | 20..20000, log | 20 |
| `focus_hi` | Focus High | Hz | 20..20000, log | 20000 |
| `attack` | Attack | ms | 1..1000, log | 20 |
| `release` | Release | ms | 10..5000, log | 150 |
| `link` | Stereo Link | % | 0..100 | 100 |
| `mix` | Dry/Wet | % | 0..100 | 100 |
| `output` | Output | dB | -24..24 | 0 |
| `delta` | Delta | | Off, On | Off |

- **The STFT**: a periodic Hann window of N samples, `spectral::frameSize()`: 2048 at 44.1 and 48 kHz, doubling per
  octave of rate (4096 at 88.2/96, 8192 at 176.4/192, 1024 at 22.05..32 kHz, down to 256 at 8 kHz), so a bin is
  about 23 Hz wide at any rate. The input goes into a ring of 2N samples; every N/4 samples (a hop, H: 10.7 ms at
  48 kHz) the last N are a frame, windowed and transformed (signalsmith-linear's `RealFFT<float>`), gained,
  transformed back and added through the window again (scaled by 1 / 1.5N: Hann² at 4x overlap sums to 1.5) into
  an accumulator that is read, and cleared, as the output.
- **The work is spread over the hop**: a frame's work (its transforms, the per-bin loops, the displays) is cut into
  steps, a transform in ten (signalsmith-linear's split computation, `RealFFT<float, true>`) and a loop over the
  bins in parts of about 256, and the steps run evenly through the hop after the frame went in, so an audio
  callback carries its share of a frame, not the whole of it (at 192 kHz a frame takes about 250 µs, longer than a
  32-sample callback lasts; spread, the largest step is about 4.5 µs). The cost is that hop: a frame's output is
  added a hop after it went in, so input sample t comes out at t + N + H exactly: `latencySamples()` is N + H
  (`spectral::latencySamples()`: 2560, 53.3 ms at 48 kHz and 58.0 ms at 44.1 kHz) and `tailSamples()` N; with every
  gain at 0 dB it is transparent to float rounding (about 1e-6). The latency changes only with the rate, in
  `prepare()`.
- **Levels** are pink-referenced: each bin's power |X|², plus `calibrationDb()` (-34.26 dB at 48 kHz and 2048) and
  `pinkDb()` (3.01 dB per octave about 1 kHz, held below 20 Hz), so pink noise of RMS L dBFS reads L dB at every
  frequency and a threshold means about what a compressor's does on a full mix. A pure tone reads about 20 dB above
  its peak in dBFS (a 0 dBFS sine at 1 kHz reads +19.9 dB): its power is all in one bin.
- **Smoothing** measures each bin over a band constant in octaves, centred on it in log frequency: the mean power
  over bins `k / 2^(w/2) .. k · 2^(w/2)` (prefix sums in double), w from 1/48 octave just above 0 % growing
  exponentially to 2 octaves at 100 % (`smoothingOctaves()`; 40 %: about 1/8 octave); at 0 % each bin alone. A mean,
  not a sum: noise reads the same at any Smoothing, while a tone reads lower the wider the band, which is what makes
  wide Smoothing gentle with tones. (At 100 % the 2-octave box reads pink noise 0.34 dB low.)
- **Envelopes**: per bin and hop, on the magnitude (the square root of Smoothing's mean power), Attack and Release
  as one-poles at the hop rate (`env = x + a (env - x)`, the attack's coefficient while rising), floored at a
  magnitude of 1e-6 (and NaN lands there too: a bin's power overflowed by an absurd sample, one still under
  `BuiltinProcessor::kMaxInput`, can't stick). Magnitude, as the Compressor's follower
  and nih-plug's, so a time means the same as there: a rise from silence reaches 63 % of the new magnitude in an
  attack time, and a fall far below decays at 8.69 dB per release time constant (a follower of power would take
  half as much). Times under a hop act at once, and the frame itself smears an onset over about N/2 (21 ms at
  48 kHz). They read noise about 1 dB under its mean power at Attack = Release where Smoothing measures a bin alone
  (the mean of a magnitude; less the more bins), and 1.5-2 dB over it with an attack faster than the release (the
  defaults) where few bins are measured (low frequencies, low Smoothing), as any peak-leaning detector does. Every
  channel's envelopes run whatever the link, so turning it finds them current.
- **Stereo Link** blends each channel's level, after the envelopes, towards the louder channel's in dB (0: each its
  own; 100: both the louder's, so both get the same gains; 50: halfway in dB, so the knob is even to the ear). From
  99.99 % one set of gains is worked out, from the louder envelope, and copied (as cheap as mono). Keyed by a
  sidechain, each main channel is compared with the key's channel of its side (one main channel: the louder key's).
- **Gains**: the shared gain computer `spectral::gainDb()` (in float): the Compressor's quadratic knee, both ways;
  down by 1 - 1/Ratio dB per dB above the threshold `Threshold + Tilt · octaves from 1 kHz`; up by 1 - 1/Upward per
  dB under Below (held at or under the threshold: `min(Below, Threshold)`, tilted the same), the lift fading in from
  -90 dB over 20 dB so silence and dither stay put; the sum held within ±Range. Then times the **Focus** weight
  (`focusWeight()`: all of it from Focus Low to Focus High, none a third of an octave outside each, linear in octaves
  between; crossed edges: nothing; at the defaults everything from 20 Hz up is processed and nothing under 16 Hz,
  so DC and subsonics pass untouched), and smoothed once across bins, [1 2 1] / 4 (the ends taking themselves for
  the missing neighbour): a raised cosine over the gain's impulse response, so a jagged gain smears less in time.
  Each bin is multiplied by its gain (DC and Nyquist, packed in the first bin, each by its own).
- **Changes don't click**: each frame's gains are faded in and out by the synthesis window and overlap four ways, so
  a jump in the gains becomes a crossfade over about N samples. Threshold, Below, Tilt, Knee, Range, the ratios (as
  slopes), Stereo Link and the Focus edges (in octaves) also glide per hop (a one-pole of 30 ms), so a sweep doesn't
  step; Smoothing, Attack and Release act through the envelopes. Dry/Wet, Output and Delta ramp per sample (20 ms).
  At Smoothing 0 with fast times and high ratios some "musical noise" is the device's character, as nih-plug's.
- **Automation stays with the audio**: the engine hands a device its parameters in step with its input, so the
  device keeps them in step with its latency itself. Each frame works with the parameters of two hops before it,
  when its centre went in (so a step in Threshold crossfades in about the sample it came at); Dry/Wet, Output and
  Delta go through rings, as the dry path does, and reach the output the latency on, with the input they came
  with. Through an engine, an Output step at 1 s ramps from the sample at 1 s in the mix.
- **Dry/Wet and Delta**: the dry path reads the input ring the latency back (the input N + H ago), so
  `y = ((1 - δ) dry + (1 - 2δ) m (wet - dry)) · out`, with m Dry/Wet's share and δ Delta's 0 or 1 (ramped). At
  Dry/Wet 0 % it is the input delayed by the latency, bit for bit. With Delta it is `Dry/Wet × (dry - wet)`: what the
  device takes away as it is, what it adds inverted (which the ear can't tell alone), after Output; for setting
  Threshold and Smoothing by ear.
- **Silence**: a frame whose input and key are all exact zeros skips its transforms (its spectrum is zeros); its
  envelopes release, and once they are all at their floor their levels come from a table (no logarithms). So silence
  costs almost nothing and comes out as exact zeros from 2N + H after the last sound (the latency, and the last frames
  to hear it). Nothing recursive runs on the audio path but the envelopes (floored), and the accumulators are cleared as
  they are read.
- **Sidechain**: with a source chosen the key's two channels are transformed too (two more forward transforms a
  hop) and their levels set the gains; a source that is silent (null pointers) keys nothing, so the input passes as
  it is, delayed, even with Upward on (silence is under its floor). Without one, its own input keys it. The key is
  the engine's buffer, which `BuiltinProcessor::process()` doesn't sanitise as it does the input: the device takes a
  key sample that isn't audio (NaN, infinity, beyond `kMaxInput`) as silence as it reads it.
- **One channel**: that one, exactly as the left of a stereo pair with the same signal on both. More than two: the
  first two; the rest untouched.
- **Reset** clears the rings, accumulators and spectra, puts the envelopes at their floor, and snaps every glide,
  ramp and parameter snapshot to the parameters; `prepare()` (a new rate) sizes everything again (about 350 KB at
  48 kHz, 1.4 MB at 192 kHz, the display streams apart) and resets.
- **Displays**: per hop, 128 values each of four spectral streams, log-spaced from 20 Hz to 20 kHz
  (`displayFrequency()`), then one value each of the two levels:

  | id | Values | Each |
  |---|---|---|
  | `input` | 128 a hop | the input's spectrum, pink-referenced dB (floor -150): per bin the louder channel's power, per point the mean |
  | `key` | 128 a hop | the levels the gains are worked out from (after the envelopes and Stereo Link; the key's when keyed): per bin the louder channel's, per point the highest |
  | `output` | 128 a hop | the output's spectrum: each channel's power times the square of what is heard of its gain, ((1 - δ) + (1 - 2δ) m (g - 1))² · out² (δ, m, out: Delta, Dry/Wet and Output as the frame's parameters set them; with Delta, the difference's spectrum), the louder, per point the mean |
  | `gain` | 128 a hop | the gains applied in dB (the Focus and the smoothing included): per bin the deeper of the channels' cuts, else the bigger lift; per point the same |
  | `in_level` | 1 a hop | the peak of the dry input as heard (delayed by the latency) over the hop, dBFS (floor -90) |
  | `out_level` | 1 a hop | the peak of the output over the hop, dBFS (floor -90) |

  A display point with two bins or more inside its own width (±2.8 %; from about 770 Hz up at 48 kHz, and a few points
  from 550 Hz) takes them all as the table says; below that it interpolates between the two bins around its frequency
  (power for the spectra, dB for the levels and gains); above Nyquist (at rates under 40 kHz) the floor, or 0 dB of
  gain. Each hop's spectral frame starts at a multiple of 128 in each stream (the editor reassembles frames by the index
  `readDisplayAt` gives), and the four are published three hops late (the frame published at hop h went in at hop h - 3
  and is centred on input sample hH - N - H), so a frame shows when its centre is heard, in step with the level streams.
  At 8192 values a stream holds 64 frames (0.68 s at 48 kHz). The `samplesPerValue` figures (4 and 512) are for 44.1 and
  48 kHz. Every value is finite and at most +300 dB (far above any audio: a full-scale tone reads +20 dB): a bin's power
  overflowed by an absurd sample would make a spectrum infinite, and one just short of overflowing reads far over it.
- **The shared maths**: `spectral::frameSize`, `latencySamples`, `calibrationDb`, `pinkDb`, `thresholdDb`, `belowDb`,
  `focusWeight`, `smoothingOctaves`, `kneed`, `gainDb`, `displayFrequency` (inline, no FFT): the engine runs float
  copies of them per bin. [SpectralResponse.h](../../app/src/audio/SpectralResponse.h), in the application layer, wraps
  them for the editor (with the display points per frame and the pivot, checked against these), so the threshold drawn
  is the one that plays and the Focus band is dimmed by the weights the sound gets.
- About 0.6 % of one core at 48 kHz stereo at the defaults; 0.75 % at the heaviest settings (unlinked, Upward 10:1,
  Smoothing 100 %, everything over the threshold); about 0.1 % more keyed by a sidechain; 1.2 % at 96 kHz (1.4 %
  heaviest) and 2.3 % at 192 kHz (2.7 %), the frame doubling with the rate (measured with `builtin_devices_bench` on a
  2.1 GHz Xeon with the work done in one go; spread over the hop it costs up to about a seventh more). Two forward and
  two inverse transforms of 2048 take about 34 µs of a hop's 55-60 µs; the rest is a square root, a `log10` and an `exp`
  per bin (one of each linked). Spread over the hop, a 32-sample callback carries about 8 µs of it at 48 kHz and 14 µs
  at 192 kHz (99th percentiles; in one go, it was 80-130 µs and 250-400 µs, more than a 192 kHz callback's 167 µs), and
  many instances in step add up to about their average.

### Saturator (`builtin:saturator`, AudioEffect)

Waveshaping after Ableton Live 12's Saturator: Drive into one of eight curves, Color's EQ around the curve, Post
Clip, Output and Dry/Wet, a DC filter, and 4x oversampling (Hi-Quality). The maths it shares with its editor is in
[SaturatorDesign.h](../../engine/src/builtin/SaturatorDesign.h) (namespace `saturator`).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `drive` | Drive | dB | -36..36 | 0 |
| `type` | Type | | Analog Clip, Soft Sine, Bass Shaper, Medium Curve, Hard Curve, Sinoid Fold, Digital Clip, Waveshaper | Analog Clip |
| `threshold` | Threshold | dB | -50..0 (the Bass Shaper's) | -18 |
| `output` | Output | dB | -36..0 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 100 |
| `clip` | Post Clip | | No Clip, Soft Clip, Hard Clip | No Clip |
| `color` | Color | | Off, On | Off |
| `base` | Color Amt Low | dB | -36..36 | 0 |
| `freq` | Color Freq | Hz | 30..18500, log | 1000 |
| `width` | Color Width | % | 0..100 | 50 |
| `depth` | Color Amt Hi | dB | -36..36 | 0 |
| `dc` | DC | | Off, On | Off |
| `hq` | Hi-Quality | | Off, On (not automatable: it is the latency) | Off |
| `ws_drive` | WS Drive | % | 0..100 | 50 |
| `ws_lin` | WS Lin | % | 0..100 | 50 |
| `ws_curve` | WS Curve | % | 0..100 | 50 |
| `ws_damp` | WS Damp | % | 0..100 | 0 |
| `ws_depth` | WS Depth | % | 0..100 | 0 |
| `ws_period` | WS Period | % | 0..100 | 0 |

Color's four are named as in Live 12.1 (its editor captions them Amt Lo, Freq, Width and Amt Hi); their ids are the
names they had before it (Base, Frequency, Width, Depth).

- **Signal flow**, per channel (the channels are independent): x is the input after the DC filter (as DC says);
  the dry signal is x. u is x times Drive (`expDbToGain`) through Color's emphasis; the curve shapes u; then Color's
  de-emphasis gives the wet signal, and the output is `Output × PostClip((1 - mix) x + mix wet)` (that form, so fully
  wet the dry signal adds exactly nothing). With Hi-Quality all of it but DC and Output runs at 4x: x is brought up
  (the dry signal with it) and the result brought back down after Post Clip. At the defaults, audio below 0.75
  (-2.5 dBFS) comes out bit for bit.
- **The curves** are odd (odd harmonics only, no DC from a symmetric input) and memoryless, of u:
  - *Analog Clip*: u up to 0.75, then `|u| - (|u| - 0.75)²` (its slope falling smoothly to 0) to 1 at 1.25, then 1.
  - *Soft Sine*: `sin(pi/2 u)` up to 1 (a gain of pi/2 for quiet input), then 1.
  - *Bass Shaper*: u up to the threshold t (Threshold as a level), then `t + (1 - t) tanh((|u| - t) / (1 - t))`
    towards 1: a hard clip at 0 dB, all but a tanh at -50 dB.
  - *Medium Curve*: `tanh(u)`. *Hard Curve*: `u - 4/27 u³` to 1 at 1.5, then 1. *Digital Clip*: u clamped to ±1.
  - *Sinoid Fold*: `sin(pi/2 u)` for any u, so past full scale it folds back (0 at 2, -1 at 3).
  - *Waveshaper*: with its controls as fractions, v = u held within ±1000,
    `s = 2 lin v + 2 curve v³ + depth sin(pi (1 + 15 period) v)`; Damp multiplies s by `v² / (v² + k²)`,
    k = 0.25 damp² (a gate near 0, -6 dB at |v| = k); the output is `u + drive (tanh(s) - u)`. WS Drive 0 leaves
    the input as it is; below 100 % the curve isn't bounded (Post Clip is the cure, as in Live).
  - tanh is `dsp::fastTanh`; sin is `saturator::sine` (reduced in double to ±pi/2, then an odd polynomial to r¹¹,
    within 6e-8 there).
- **Color** is an EQ before the curve and its exact inverse after it: a low shelf at 150 Hz (Q 0.7071) of Amt Lo dB,
  then a peak at Freq (kept below 0.45 of the sample rate) of Amt Hi dB, Width wide (0.25 · 16^(width/100)
  octaves, Q 5.77 at 0 %, 1.41 at 50 %, 0.27 at 100 %), all RBJ sections; after the curve the peak's inverse, then
  the shelf's (`saturator::inverse`: numerator and denominator swapped, stable since the sections are minimum
  phase). Where the curve is straight they cancel, so a clean sound comes through unchanged (within 1e-5); where it
  bends, a band boosted before it saturates harder and is turned back down after (less of it: Amt Hi +24 at 2 kHz
  takes 9.4 dB out of that band of a hard-clipped noise), and a band cut before it stays clean and is restored on
  top, ringing like a resonance (Amt Hi -24 adds 16.7 dB; Amt Lo ±24, -5.4 and +17.8 dB below 100 Hz). Switched off,
  Amt Lo and Amt Hi glide to 0 dB (where RBJ's sections are exactly 1), and once every state is below 1e-6 the filters
  are switched out and cleared; on at 0 dB they are switched out too, bit for bit as off.
- **Post Clip** comes after the de-emphasis (so it catches what that adds back) and after Dry/Wet, on the blend:
  Soft is the Analog Clip curve again, Hard a clip at ±1, so at any Dry/Wet the output never passes the Output level
  (Ableton's manual: "Saturator's output will never exceed the level set by the Output control"; a dry sound under
  the Analog Clip curve's knee passes Soft Clip untouched). With Hi-Quality it clips at 4x, so its own harmonics
  don't fold back either: a 5 kHz tone's worst alias is -72 dB rather than -35 under Soft Clip after Analog Clip at
  +12 dB, and -59 rather than -22 under Hard Clip after the Waveshaper at +18 dB. Brought back down, band-limited, a
  bright clipped sound can then pass the ceiling a little: a hard-clipped 5 kHz tone by 15 %, full-scale noise
  driven hard by up to 57 %, never more than the down filters' worst gain (1.84), as the clipping curves' own flat
  tops do with Hi-Quality.
- **DC** is a 5 Hz `dsp::DcBlocker` at the input; it always runs (so it is warm when switched on), and DC crossfades
  between the input and its output.
- **Hi-Quality** runs Color's emphasis, the curve, the de-emphasis, Dry/Wet and Post Clip at 4x
  (`dsp::Oversampler`, factor log2 2), on x brought up; the dry signal goes up and down with it (flat within
  0.003 dB to 20 kHz at 48 kHz: the input 36 samples late). `latencySamples()` reports the 36 samples (as the
  parameter is, at once); `idle()` returns true once when it changes, so the engine realigns the tracks. Color has
  filters for each rate, designed for it. What folds back below Nyquist drops from -25 to -118 dB of the output
  (Medium Curve, +18 dB, 5 kHz) and from -12 to -45 dB (Digital Clip, +24 dB, 7.1 kHz). Switched on, the 4x path
  starts from silence and runs unheard for 80 samples (its filters hold 74) before it fades in, so once faded it
  plays bit for bit as if it had been on all along (Color's 4x filters, if Color runs, start from silence too: on a
  clean sound they still cancel exactly); switched off, it fades out to the 1x path, whose Color filters likewise
  start from silence.
- **Smoothing**: the work goes in chunks of up to 16 samples. Drive, Output, Dry/Wet, Threshold and the six WS
  controls glide through two one-poles in a row of 10 ms each (`dsp::Glide`; Color's four, Freq in log, 20 ms), so
  a jump eases in and out; a glide moves a chunk at a time in closed form (after n samples its errors are
  `e1 cⁿ` and `cⁿ (e2 + n (1 - c) e1)`, exact at each chunk's end however automation splits the block) and lands
  exactly once within 1e-4 dB (1e-6 for fractions and log Hz). Across a chunk the gains and the curve's settings
  ramp linearly, sample by sample. Type, Post Clip, DC and Hi-Quality crossfade over 10 ms (an S-curve) between
  both sides (a type's two curves, at 4x too); a change during a fade starts when it is done.
- **Color while it glides** is designed anew at each chunk's end, and its coefficients move there in a straight line
  base sample by base sample (`saturator::between`; with Hi-Quality, each held over its four). The de-emphasis follows
  the same line at the same samples, since the curve between them has no memory. A section H and its inverse fed H's
  output (transposed direct form II) give back H's input exactly while the inverse's states are -1/b0 times H's; a new
  b0 breaks that, so each time the de-emphasis's coefficients move its states are rescaled by b0 old / b0 new. Through
  any glide a clean sound stays itself to float rounding (1e-8 on a 0.05 noise, also with Hi-Quality against Hi-Quality
  without Color), and switching Hi-Quality on or off mid-glide leaves it clean once the fade is done; without the
  rescale it would zipper. Coefficients held over each chunk would step 3000 times a second, which a saturated sound
  makes audible.
- `tailSamples()`: what the curve leaves ringing in Color's filters once it bends (the slowest of the peak's poles
  and zeros, Q·max(A, 1/A) / (pi f) seconds with A = 10^(depth/40), and the shelf's lower corner,
  0.7071 / (pi · 150 · 10^(-|base|/80)); only a section whose gain isn't 0) or the DC filter (1 / (2 pi 5 Hz)):
  8 time constants of the slowest, plus 80 samples with Hi-Quality (its filters' memory), at most 5 s. 0 at the
  defaults; 3.9 s at its longest (Amt Hi -36 dB at 30 Hz, Width 0 %).
- **Denormals**: every curve gives exactly 0 for 0; the DC filter's states are flushed below 1e-20, and Color's
  sections (`dsp::Biquad`) clear both their states together once both are below it (one at a time, a slow section,
  at 4x or at a low Freq, could ring at about 1e-19 for ever: what clearing one takes out, its partner puts back);
  the FIRs aren't recursive and the glides land, so silence rings out to exact zeros without the renderer's
  flush-to-zero. `reset()` clears every state and snaps every glide and fade to the parameters (no pre-roll: from
  silence a 4x path is already in step); `prepare()` (a new rate) works out the glides and fades again and resets.
  On one channel it processes that one, as a stereo pair's left.
- **Displays**: `in_peak` and `out_peak`, one value per 128 samples (`kMeterSamples`, counted across stretches): the
  louder channel's peak of x (after DC, before Drive) and of the output, linear, since the editor's curve has linear
  axes; `input` and `output`, one value per sample, x and the output summed to mono. The editor draws the live level
  on its curve from `in_peak` and the In/Out strips, and the input and output spectra behind Color's EQ curve.
- **Input that isn't audio** (NaN, infinity) never reaches its states: `BuiltinProcessor::process()` takes it as
  silence first, so the DC filter (which runs while DC is off too), Color's sections and the 4x path's filters can't
  keep it; what comes out is exactly what zeros there would give.
- **Shared with the editor**: the curves (`saturator::makeShape`, `params`, `curve`, `transfer`), Post Clip, Color's
  design (`colorDesign`, `colorResponseDb`) and Hi-Quality's factor and latency (`kHqFactorLog2`, `hqLatency()`) are
  inline in SaturatorDesign.h; the application layer's `saturatorCurve()`, `saturatorSlope()`, `saturatorColorDb()` and
  `saturatorHqLatency()` ([app/src/audio/SaturatorResponse.h](../../app/src/audio/SaturatorResponse.h)) call them, so
  the curve drawn is the curve played, from the same float arithmetic. The editor also takes the Type list's Bass Shaper
  and Waveshaper entries and the parameters' ranges (`saturatorRange()`, what its drags stay within) from there.
- At 48 kHz stereo on the machine it was written on (`builtin_devices_bench`): 0.09 % of one core at the defaults,
  0.12 % at Medium Curve +12 dB, 0.31 % with the Waveshaper's ripples and gate, Color and DC; with Hi-Quality too,
  and Drive at +12 dB, 0.67 % ([benchmarks/README.md](../../benchmarks/README.md)). Before `dsp::Oversampler`'s stages
  were vectorised, Hi-Quality took about 1 % (the 4x filters most of it), 1.7 % at its heaviest (Hi-Quality,
  Waveshaper, Color at 4x, DC, Soft Clip, half wet) and 2.0 % with every glide moving all the time.

### Amp (`builtin:amp`, AudioEffect)

A guitar amplifier after Ableton Live's Amp: seven models, an amp's dials, Mono or Dual and Dry/Wet. Each model is a
voicing of one structure ([AmpDesign.h](../../engine/src/builtin/AmpDesign.h)): an input stage, three triode stages
around a passive tone stack, Presence, a power stage whose supply sags, and the output transformer. Softube's models
are unpublished: this is a behavioural design, built to do what Live's manual says each control does and to sound like
the amps the models are known to be. As Live's, it has no speaker simulation: alone it sounds bright and fizzy, as a
real amp's line output does, and wants a cabinet or a low-pass after it.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `type` | Amp Type | | Clean, Boost, Blues, Rock, Lead, Heavy, Bass (a list) | Clean |
| `gain` | Gain | dial | 0..10 | 5 |
| `bass` | Bass | dial | 0..10 | 5 |
| `middle` | Middle | dial | 0..10 | 5 |
| `treble` | Treble | dial | 0..10 | 5 |
| `presence` | Presence | dial | 0..10 | 5 |
| `volume` | Volume | dial | 0..10 | 5 |
| `dual` | Dual Mono | | Mono, Dual (a list) | Mono |
| `mix` | Dry/Wet | % | 0..100 | 100 |

The names are Live's automation names ("Dual Mono" is its Output switch). The dials' unit `dial` reads "5.0" (one
decimal, no sign: `formatValue` in [ParamSpec.cpp](../../app/src/model/ParamSpec.cpp)).

- **Signal flow** (one amp): at the base rate an input coupling high-pass and a bright cap's shelf (first order);
  then, 4x oversampled (`dsp::Oversampler`), V1, V2, the tone stack, V3, Presence, the power tubes' input (a one-pole
  low-pass at 12 kHz), the power stage and the transformer (a one-pole low-pass at 7.5 to 12 kHz); back at the base
  rate, a 10 Hz DC blocker and the model's output trim.
- **A triode stage** is a Miller low-pass, a gain, a cubic soft clipper (1.5 x − 0.5 x³ inside ±1, ±1 outside) around
  its bias point, and a coupling high-pass. Its small-signal gain is exactly the voicing's (the curve's slope at the
  bias is divided out); a positive bias clips the top first, so it makes the even harmonics of a triode driven into
  grid current, and the coupling high-pass takes away the DC the asymmetry makes. The curve's antiderivative is a
  polynomial, so first-order antiderivative anti-aliasing (ADAA: the curve averaged over the line from the last input
  to this one) costs a division per sample. It is written without branches (every case worked out, the right one
  chosen), and two clipped samples give exactly ±1. With the 4x oversampling it keeps what folds back of a 997 Hz
  sine at −12 dBFS 82 to 85 dB down at noon and 66 to 83 dB at Gain 10 (Lead the most), 57 dB with Gain, Presence and
  Volume at 10 on Lead (77 to 83 on the others); 4x without it measured −36 dB on Lead at Gain 10.
- **The tone stack** is the passive Fender/Marshall/Vox stack as Yeh and Smith model the '59 Bassman's: a third-order
  filter whose coefficients are polynomials in the three pots' positions, so the controls interact as a real amp's do
  (Treble and Middle are linear pots, Bass an audio taper: 9 % at noon). Each model has its own component values
  (`kVox`, `kFender`, `kMarshall`, `kModern`, `kVintage`, `kPa`) and a make-up gain that puts its response at noon at
  0 dB at its peak. It is the bilinear transform at the oversampled rate (within 0.02 dB of the analog response from 30
  Hz to 16 kHz), run in double as transposed direct form II. It sits between V2 and V3, so turning a band up drives V3
  harder in it. Its three states are flushed together, never one at a time: with its poles near z = 1 they nearly
  cancel, and clearing some of them kicks the stack back into ringing.
- **Presence** is a first-order high shelf (±8 or ±9 dB at the ends) before the power stage, as a power amp's
  feedback presence is, so turning it up drives the power stage's highs harder. Between them, the power tubes' input
  (their grids' Miller capacitance) is a one-pole low-pass at 12 kHz (`amp::kGridHz`): without it a preamp already
  clipped, its edges boosted by Presence, reached the power stage's curve at full height, and with Gain, Presence and
  Volume at 10 what folded back was only 44 dB down on Lead. It takes 0.03 dB off 1 kHz.
- **The power stage** is one more such stage with a push-pull's slight bias. Its supply **sags**: per base-rate
  sample its output's peak (of its four oversampled samples) drives an envelope (attack 5 to 15 ms, release 80 to 300
  ms), and the drive is divided by `1 + sag × env`: a loud chord pulls it down by up to 20 log10(1 + sag) dB and lets
  it back over the release, compression and bloom.
- **The voicings** (`amp::kVoicings`, every number in [AmpDesign.h](../../engine/src/builtin/AmpDesign.h)):

  | Model | Stack | Gain 0..10 | Character at noon (220 Hz, −12 dBFS) |
  |---|---|---|---|
  | Clean | Vox | −18..+18 dB | chiming, V1 well under clipping (4 % THD, the 2nd harmonic 12 dB over the 3rd), V2 a clean cathode follower |
  | Boost | Vox | −6..+30 dB | V1 at the knee, V2 past it: crunch (31 %) |
  | Blues | Fender | −12..+24 dB | a clean preamp, the deep Bassman scoop, a power stage near its knee with the deepest sag: Volume distorts it (9 % THD at noon, 28 % at Volume 10) |
  | Rock | Marshall | −6..+30 dB | V1 at the knee into a cascaded V2: the Plexi's crunch (28 %) |
  | Lead | Modern | 0..+30 dB, half at V1, half at V3 | tight (an 80 Hz input high-pass, 120 Hz coupling), V2 and V3 far over (45 %), little sag |
  | Heavy | Vintage | −6..+32 dB, half at V1, half at V3 | looser and darker, V2 and V3 at their knees: a crunch (34 %, the 2nd harmonic 12 dB over the 3rd) into a power stage that sags and is driven 9 dB past clipping at Volume 10 (44 %) |
  | Bass | PA | −12..+30 dB | big asymmetric bias at V1 and V2 (fuzz: the 2nd harmonic 18 dB over the 3rd), the PA stack's lows, a power stage that distorts with Volume |

- **The dials**: Gain is linear in dB from the model's Gain 0 to its Gain 10 (on Lead and Heavy split between V1 and
  V3, around an early tone stack). Volume is the power stage's drive, ±12 dB around the model's noon (Volume 0 is
  quieter, not silent): on Blues, Heavy and Bass it takes the power stage 5 to 9 dB past clipping at 10 and adds
  distortion (Blues 9 to 28 % THD from noon, Heavy 34 to 44, Bass 26 to 32), on the others it stays at or under it,
  as Live's manual says. Each model's trim level-matches it at the defaults: a −12 dBFS sine comes out at its own RMS
  (−15 dBFS) on every model.
- **Smoothing**: control-rate work happens per chunk of up to 16 samples (`kChunk`), on a grid counted from the
  meters' 256-sample windows (so a window always ends with a chunk). The six dials glide (one-poles of 20 ms); while
  anything moves, every gain, bias and filter coefficient (the one-poles', the tone stack's) and the sag's amount
  ramps linearly across each chunk from where the last one ended, per oversampled sample in the oversampled section:
  a coefficient's step is a step in the signal's slope, which a cleanly played sine shows. A stage's offset follows
  its bias sample by sample (the curve half a step back, where the anti-aliasing averages it as the bias moves), so a
  stage at rest stays at 0: ramped, it left a step of DC at every chunk's join. While nothing moves the chunks reuse
  the last controls and run loops without ramps. A dial moving recomputes only what it sets (the stages' input gains,
  the tone stack, Presence's shelf or the drive).
- **Amp Type** morphs over 50 ms (an S-curve) from the voicing as it is to the new one: dB values, biases and the sag
  linearly, frequencies, times and the tone stack's parts geometrically, so every coefficient moves continuously. A
  new model during a morph starts a new one from the voicing in between. A blend of two voicings clips where neither
  does (the heads, the gains and the trim stop agreeing midway), so on the way it came out up to 6 dB louder than both
  (Lead to Bass); the trim keeps it level-matched: `amp::Transfer`'s harmonic balance (below; a `BasicTransfer<64>`)
  gives each voicing's level (the RMS of a 1 kHz tone at the input's recent peak, 64 samples a period), and the blend
  is turned by how far its level is from the two models' in between, worked out at 17 points of the morph as it
  reaches them and joined by a smooth curve. What the rate sets (every sine and cosine those levels take) and what
  each model sets (its filters' coefficients and their responses: `amp::BasicTransfer`'s `Rate` and `Voice`), and the
  logarithms a blend takes of each model's frequencies, are worked out in `prepare()` and at construction: a level
  from a model's costs what the dials add and the period's FFTs (a real period through a complex FFT of half its
  length), about the work the amp does for 16 samples, a blend's half as much again. They come one per 16-sample cell
  of the control grid at most: the morph sets off once its first four are known (1 ms after the change at 48 kHz,
  during which the amp plays on as the old model), a change's first 32-frame block (the two models' levels) costs
  about two steady ones, and no block of a morph more than about three (measured with glibc: a blend's level still
  calls exp and pow, so a slower maths library costs more). It goes by the grid, so a morph sets off and
  moves at the same samples whatever the blocks. Through every one of the 42 morphs the level stays within +0.9 /
  −1.4 dB of the two models (a −12 dBFS sine, 5 ms windows). Each chunk of a morph applies the compensation to the
  voicing's own trim (never to the last chunk's, which carries it already), so dials moving while a morph waits change
  nothing in its level, and changes coming faster than a morph can set off hold the blend they found, at its level.
- **Mono and Dual** (the Output switch): Mono runs one amp on (L + R) / 2 and writes it to both channels (half the
  work); Dual runs one per channel. The switch crossfades over 20 ms (an S-curve on the amps' inputs and on the right
  channel's output); the second amp starts from rest, its input fading in over 5 ms, and stops once Mono is back. A
  switch during a fade starts when it is done. On one channel, one amp, and the switch does nothing.
- **Dry/Wet** (20 ms ramp) mixes in each channel's input delayed by the latency, so at 0 the output is the input
  delayed, bit for bit, while the amps go on running (it comes back without a seam).
- **Latency**: 37 samples at any rate (`latencySamples()`, constant): the oversampler's 36, and one for the four stages'
  anti-aliasing (half an oversampled sample each) with a two-sample FIFO after the transformer. Besides it the wet has
  its filters' own phase delay (the one-pole low-passes a fraction of a sample each, as an analog amp's;
  `amp::Transfer::phaseDelay()` at 1 kHz). **Tail**: the latency and half a second. The slowest pole is the tone
  stack's: 53 ms at most (the PA stack with Bass and Middle at 10, Treble at 0); half a second takes it 80 dB down.
- **Silence and sleep**: every stage's anti-aliasing is primed at its bias wherever it starts from silence (`reset()`,
  the second amp starting, waking), so a stage at rest puts out exact zeros (`amp::restValue`); every recursive state
  is flushed when tiny (under 1e-20) at the chunk grid's points, and the supply's envelope under 1e-8 every sample
  (where `1 + sag × env` rounds to 1), so silence rings out to exact zeros (within a second at noon, 2.5 s at most:
  the PA stack's slowest pole) and blocks of any size play alike, bit for bit (flushed at the blocks' ends, they
  didn't). An amp whose input has been exactly silent for 4096 frames, whose output has been exact zeros for 256 (its
  down-sampler and DC blocker, whose states it can't read, hold nothing more) and whose states are all at rest
  **sleeps**: it puts out the zeros it would, its supply's sag recovering over its release meanwhile (sample by
  sample, as awake), and costs next to nothing until its input sounds again. Once the sag has recovered, a woken amp
  plays as a freshly reset one, bit for bit. `reset()` snaps the dials and the model to the parameters and silences
  both amps; `prepare()` sizes the buffers (blocks longer than its maximum are worked in slices) and resets.
  Input that isn't audio (NaN, infinity, beyond ±1e30) reaches it as silence (`BuiltinProcessor::process()`), so
  none of its states ever holds it: a bad sample plays as a zero would, bit for bit. One that is audio but absurd
  (+600 dBFS) blocks it as it would a real amp: the input coupling holds the spike's tail far over the signal until
  it has died away (0.15 s on Lead's 80 Hz, 0.7 s on Bass's 15 Hz), and it plays on.
- **Displays**, one value each per 256 samples (`amp::kDisplaySamples`), published together at each window's end
  (with two amps, the larger): `input` (the input's peak, dBFS), `drive1`, `drive2`, `drive3` (each preamp stage's
  peak against its clipping point, dB: 0 is where it clips), `power` (the power stage's, the same), `sag` (the
  supply's sag at the window's end, dB, ≥ 0) and `output` (the output's peak after Dry/Wet, dBFS); floored at −90
  (`amp::kDisplayFloorDb`). The editor takes the floor from the design through the application layer
  (`ampDisplayFloorDb()`), as it takes the models' names (`ampModelNames()`), and the rate from the displays
  themselves (`DeviceCanvas::readRecent`).
- **Shared with the editor**: `amp::toneResponseDb()` (the tone stack's digital response with its make-up, times
  Presence's shelf, at the oversampled rate: the tone controls' part; the fixed roll-offs of the power tubes' input
  and the transformer aren't in it) and `amp::Transfer`, the transfer for a 1 kHz tone worked out by harmonic balance:
  one period of the tone (256 samples) through each stage's curve sample by sample and through each filter between
  them harmonic by harmonic, by its own response there (the anti-aliasing's half-sample average among them; harmonics
  above the oversampled rate's half dropped, and above the base rate's at the output), the supply's sag a lower power
  drive. For a tone of each peak it gives the output's highest and lowest values: played, a tone's peaks land within
  0.1 dB of them on every model at the defaults (−40 to −3 dBFS), and within 0.6 dB with Gain, Presence and Volume at
  10. Its preamp's part doesn't depend on the sag, so the editor keeps it and remakes only the power stage's as the
  sag moves. Both through the application layer's [AmpResponse.h](../../app/src/audio/AmpResponse.h)
  (`ampToneResponseDb`, `ampTransfer`, `AmpTransferCurve`): the curves drawn are the sound.
- **Cost** (48 kHz, `builtin_devices_bench`, the best of three on a 2.1 GHz VM, where OTT measured 0.44 % in the
  same runs; [benchmarks/README.md](../../benchmarks/README.md)): 0.68 % of one core at the defaults (Mono), 1.36 % in
  Dual (Lead, Gain 10: the same at any setting, the structure never changes), in proportion to the rate. Measured
  before `dsp::Oversampler`'s stages were vectorised (when the defaults took 1.0 %, Dual 1.9 %, and about 40 % of it
  was the oversampler's up and down): automating a dial added next to nothing (0.04 %), a model change every 100 ms
  0.3 % (each morph works its levels out), and a model change's first block cost about 3.3 times a steady one in
  blocks of up to 64 frames (3 % of the block's time), 2.3 times at 128, 1.9 times at 256. A silent track costs next
  to nothing once its amp sleeps.

### Erosion (`builtin:erosion`, AudioEffect)

After Live 12.4's Erosion: the input read out of a 2 ms delay whose read position a sine or band-passed noise
wobbles at audio rate ([ErosionDesign.h](../../engine/src/builtin/ErosionDesign.h)). That phase-modulates every
partial, the more the higher it is: lows survive, highs turn to grit and hiss, and the sidebands past Nyquist fold
back as the "digital" aliasing the device is for. There is no Dry/Wet (Live's has none either): at Amount 0 it is a
clean delay of its latency.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `freq` | Frequency | Hz | 20..18000, log | 1000 |
| `width` | Filter Width | oct | 0.1..10, log | 2.5 |
| `amount` | Amount | % | 0..100 | 25 |
| `blend` | Noise Blend | % | 0..100 | 100 |
| `stereo` | Stereo Width | % | 0..100 | 0 |

- **The delay**: its centre D is 2 ms in whole samples (`centreDelaySamples`: 88 at 44.1 kHz, 96 at 48 kHz, 384 at
  192 kHz), read with 4-point Hermite interpolation at D + E·m, m the modulator (RMS 1/√2) and E the excursion:
  Amount's square times the most (`excursionSamples`; 25 % moves it ±87 µs, 100 % ±1.38 ms). The most is the limit
  D − 2 over √2, so a sine never reaches the limit, where the read is clamped (it never reads the future or past the
  line), and noise peaks only near the top of Amount. No dry path; with E exactly 0 for a chunk the line is read at
  D as it is (what Hermite gives at a whole delay, and cheaper).
- **The modulators**: the sine is one quadrature phasor in double (renormalised each chunk), its sides
  sin(phase ± h), h = Stereo × π/4: a quarter cycle apart at 100 %, uncorrelated as the noise is. The noise is two
  white noises (mid and side, `dsp::Noise`), each through two identical band-pass sections (TPT
  state-variable, `k·v1`: the RBJ band-pass at 0 dB peak) whose −3 dB edges are Width octaves apart around
  Frequency (each section's Q is √(√2 − 1) / (h − 1/h), h = 2^(Width/2), so each is 1.5 dB down there); two
  sections give 12 dB/octave skirts, so a narrow band is selective. The noise is scaled to RMS 1/√2 exactly: its
  power gain through the pair is worked out in closed form (`noisePowerGain`: the sum of the squares of the pair's
  impulse response, from its autocorrelation), so Frequency and Width move where its energy is, never how hard it
  modulates. Stereo mixes mid and side to the sides with gains 1/√(1 + w²) and w/√(1 + w²) (w = Stereo/100): each
  side keeps its RMS, their correlation is (1 − w²)/(1 + w²). Noise Blend crossfades sine and noise at equal power
  (the weights exactly 1 and 0 at the ends). The modulator plays at most 0.45 of the sample rate. Live's legacy
  modes are settings: Sine is Noise Blend 0 %, Noise 100 %, Wide Noise 100 % with Stereo 100 %.
- **Its own noise**: each instance salts the noises' seeds (`kMidSeed`, `kSideSeed`) with a number of its own
  (`dsp::hash32` of the count of Erosions made before it, `instancesMade`), kept for its life. Two Erosions set
  alike therefore don't modulate alike: on double-tracked parts they would otherwise wobble in lockstep, and an
  export, which resets every device at once, would line them all up. Its renders still repeat (reset() reseeds it
  with its own seeds); a project loaded again gets new noise, statistically the same. (The tests set the count
  back to 0 to make devices that play alike.)
- **Smoothing**: the work runs in chunks of 16 samples on a grid that goes on across blocks and stretches, so the
  output never depends on how a block was split (offline renders are bit-identical at any block size). At each chunk's
  start every control's glide (`dsp::Glide`: two one-poles of 10 ms in a row, so a jump eases in and out; Frequency
  and Width in log) moves on a chunk, and what it makes ramps linearly across the chunk: the excursion, the blend's
  weights, the noise's gain and its mid and side gains, the sine's offset, and the sections' g and k, from which the
  sections' coefficients are made sample by sample while they move. While the band moves, the sections' states ease
  across each chunk by as much as their steady level changes (k × scale over the chunk's: the noise, scale k v1, keeps
  RMS 1/√2): the energy they built at the old band would otherwise modulate several times too hard after a long move
  down or a widening, and too weakly after a narrowing, for a few hundred ms, until it decayed at the new band's rate.
  The phasor's rate steps per chunk (its phase stays continuous). Once a glide has landed nothing that depends on it
  is worked out again (Frequency's and Width's logs are taken only when the parameter changes): a settled chunk does
  no transcendental work. The modulators never stop or restart, so there is no phase jump to hide, and there are no
  lists or switches to crossfade.
- **One channel** gets the left modulator, worked out exactly as in stereo (both noises run), so a mono run is the
  left side of a stereo one. More than two: the first two. A channel that wasn't processed starts from silence.
- **Latency and tail**: `latencySamples()` is D (2 ms), so the engine delays the other tracks by it and at Amount 0
  the device is transparent, to the sample. `tailSamples()` is 2D: no sample stays longer. Nothing in it decays (the
  delay has no feedback, the sections always hear noise), so silence comes out as exact zeros 2D samples on. It
  reads no sidechain; NaN or infinity in its input is silence by the time it hears it (`BuiltinProcessor`), so its
  line never holds one.
- **Reset**: the line and sections cleared, the noises reseeded with the instance's own seeds and the phasor
  restarted (its renders repeat exactly, noise included), every glide and ramp where the parameters are.
  `prepare()` (a new rate) works out D, the limit and the glides again, and resets.
- **Displays**:

  | id | Values | Each |
  |---|---|---|
  | `input` | one per sample | the input summed to mono ((L + R)/2, or the one channel) |
  | `output` | one per sample | the output summed to mono |
  | `erosion` | one per 256 samples (`erosion::kMeterSamples`) | what the device changed, in dB: the power of the output less the input D samples ago; floor −90 (`erosion::kFloorDb`; exactly that at Amount 0) |
  | `mod_l`, `mod_r` | one per sample | the left and right modulators (before the excursion and the clamp; one channel: the left twice), pushed together so they stay in step |

  The modulators go out every sample: decimated, a sine near a multiple of the decimated rate's Nyquist would look
  like two points on a line, and the editor's scope (and the tests' correlations) would mislead.
- **The shared design**: `erosion::band`, `bandMagnitude` and `bandEdges` give the noise band's sections, its
  magnitude and its −3 dB edges from the same maths the device plays; `excursionMs` the excursion; `blendWeights`
  and `stereoSpread` the blend's and Stereo's gains. The application layer's
  [app/src/audio/ErosionResponse.h](../../app/src/audio/ErosionResponse.h) (`erosionBandMagnitude`,
  `erosionBandEdges`, `erosionExcursionText`...) wraps them for the editor, so the band drawn is the filter that
  plays; `erosionPeakDb` reads the `erosion` display's values against its floor (`kFloorDb`: the application
  layer's `kErosionFloorDb` is checked equal to it).
- About 0.21 % of one core at 48 kHz stereo, at the defaults and at Amount 100 %, Noise Blend 50 %, Stereo 100 %
  (0.12 % at Amount 0); about 0.26 % with all five controls gliding all the time (measured with
  `builtin_devices_bench` on a 2.1 GHz Xeon).

### Bitcrusher (`builtin:bitcrusher`, AudioEffect)

Fewer bits and a lower sample rate.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `bits` | Bits | | 1..16 | 8 |
| `rate` | Rate | Hz | 200..48000, log | 11025 |
| `drive` | Drive | dB | 0..24 | 0 |
| `dither` | Dither | % | 0..100 | 0 |
| `tone` | Tone | % | 0..100 | 100 |
| `output` | Output | dB | -24..12 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 100 |

- **Per channel**: × Drive, clipped to ±1; then held: a clock adds Rate / sample rate
  each sample, and each time it passes 1 the next value is taken (fractional, so any
  rate; the aliasing is meant); that value + Dither × TPDF noise of up to a step,
  rounded to 2^(bits − 1) steps either side of zero (fractional bits too), held to ±1.
- **Tone**: a one-pole low-pass from 1 kHz (at 0) to 20 kHz (at 99 %), evenly in log;
  off at 100 %. Then Output and Dry/Wet (ramped over 20 ms, snapped on `reset()`).
- **Display**: `level`, one value per 256 samples: the input's peak over them (the louder channel). Its
  editor works its preview out from the parameters, and runs it along while this comes.

### Tape Stop (`builtin:tape_stop`, AudioEffect)

The sound slowing to a halt, as a tape machine's or a turntable's does when its motor is cut, and spinning up
again.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `stop` | Stop | | Off, On | Off |
| `stop_time` | Stop Time | ms | 20..4000, log | 600 |
| `start_time` | Start Time | ms | 1..4000, log | 250 |
| `curve` | Curve | % | -100..100 | 0 |
| `tone` | Tone | % | 0..100 | 50 |
| `mix` | Dry/Wet | % | 0..100 | 100 |

- **The tape**: the input goes into a ring (a `dsp::DelayLine` per channel, 8 s long: a whole stop and a whole
  start at their longest), read `lag` samples behind (linearly under 2 samples, Hermite beyond). `progress` runs
  from 0 (full speed) to 1 (stopped): up by 1 / Stop Time's samples each sample while Stop is on, down by 1 / Start
  Time's while it is off; the speed is `speedAt(progress, curve)`, and the lag grows by 1 − speed each sample.
- **Curve**: k = 2^(|curve| / 40); from 0 up, speed = 1 − p^k (held, then dropping: 0 is an even slope); below,
  (1 − p)^k (dropping at once, crawling to the end). A start runs back along the same curve.
- **Starting again**: let go once stopped (silent), the head starts from the input as it is then (lag 0); let go
  partway, it speeds up from where it had got to. Back at full speed with a lag, it crossfades to the input as it
  is over 20 ms (a stop waits for that to end), and from then on passes it bit for bit.
- **Level and tone**: the level fades out (an S-curve) over the last tenth of the speed; Tone darkens it as it
  slows: a one-pole low-pass at 80 Hz + 18 kHz × speed², its coefficient × Tone, off at full speed. Dry/Wet ramps
  over 20 ms. On `reset()` it starts stopped if Stop is on.
- **Tail**: Stop Time (a stop goes on playing what came before).
- **Display**: `speed`, one value per 256 samples: the speed (0..1). Its editor's reels turn at it.

### Glitch (`builtin:glitch`, AudioEffect)

Steps of the sound on a grid of note values cut up at random: stuttered, reversed, slowed to a stop or chopped.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `grid` | Grid | | 1/32, 1/16, 1/8, 1/4, 1/2, 1 Bar | 1/8 |
| `chance` | Chance | % | 0..100 | 40 |
| `stutter` | Stutter | % | 0..100 | 100 |
| `reverse` | Reverse | % | 0..100 | 60 |
| `tape` | Tape | % | 0..100 | 40 |
| `gate` | Gate | % | 0..100 | 40 |
| `variation` | Variation | | 0..99, whole | 0 |
| `mix` | Dry/Wet | % | 0..100 | 100 |

- **The grid**: steps of Grid's note value from the song's start while it plays (`ctx.beatPos`), on its own clock
  at the tempo while it doesn't (so live input glitches too). A step entered other than at its start (playback
  starting or jumping into it, Grid changed) plays as it is.
- **The roll**: at a step's start, 64 random bits from its number and Variation (splitmix64's finalizer: the same
  place always glitches the same way, and a render repeats exactly): under Chance it glitches, its kind picked by
  the four weights, then its parts.
- **The kinds**, as a voice reading the history (a `dsp::DelayLine` per channel, 8 s) `delay` samples behind, moving
  `rate` further each sample, `t` samples into a step of `L`:
  - **Stutter**: the step's first 1/2, 1/3, 1/4, 1/6 or 1/8 over and over (no repeat under 10 ms):
    delay = ⌊t / S⌋ · S.
  - **Reverse**: the step before, backwards: delay = 2t + 1; not until a whole step has come in since `reset()`
    (the step before would be silence).
  - **Tape**: the speed falling from 1 to 0 over the step: delay = t² / 2L, rate t / L; the level fades out (an
    S-curve) over the last 8 % of the speed.
  - **Gate**: the step cut into 4, 6 or 8, every other part silent (a gain ramped over 2 ms).
  A step too long to reverse from the history doesn't glitch.
- **Crossfades**: wherever the voice jumps (more than half a sample from where it was going), the old one goes on
  as it was (its delay moving at its rate) and fades out over 2 ms, the new one in (an S-curve). Unglitched and
  with no fade going, the input passes bit for bit. Dry/Wet ramps over 20 ms.
- **Tail**: a step at 60 BPM (a reverse plays the step before).
- **Displays**, one value each per 256 samples: `step`, the step's number modulo 16 plus 16 × its kind (0 none,
  1 Stutter, 2 Reverse, 3 Tape, 4 Gate; one value, so the editor never sees one without the other), and `level`,
  the input's peak.

### Overdrive (`builtin:overdrive`, AudioEffect)

A pedal's drive after Ableton's Overdrive.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `freq` | Frequency | Hz | 50..20000, log | 1500 |
| `width` | Width | oct | 0.5..9 | 6.5 |
| `drive` | Drive | % | 0..100 | 50 |
| `tone` | Tone | % | 0..100 | 50 |
| `dynamics` | Dynamics | % | 0..100 | 50 |
| `mix` | Dry/Wet | % | 0..100 | 100 |

- **Band**: a 12 dB/octave Butterworth high-pass at `freq / 2^(width/2)` then low-pass at
  `freq × 2^(width/2)` (TPT sections; `overdrive::bandResponseDb` is their exact response).
- **Drive**: a gain of 0 to 42 dB (`kMaxDriveDb`) into tanh, its negative side clipping at 0.75
  (`kNegative`: even harmonics, until it is driven nearly to a square), anti-aliased by its antiderivative
  (first-order ADAA: half a sample late, `cos(πf/sr)` in level), then a 5 Hz DC blocker.
- **Dynamics**: the clipped signal times `min(1, band level / clipped level)^(dynamics/100)`, the
  levels from peak followers (2 ms, 60 ms) on the band and the clipped signal, worked out a chunk at a
  time and ramped across it.
- **Tone**: a 12 dB/octave low-pass from 1 kHz to 20 kHz, evenly in log.
- The filters' cutoffs glide in log (15 ms) a chunk (16 samples, counted across `render()` calls, so
  blocks don't matter) at a time; Drive and Dry/Wet ramp over 20 ms. No latency.
- **Displays**: `drive` (the driven band's peak) and `output` (the output's peak), one value per 128
  samples; `input` (the input summed to mono), a value a sample.

### Auto Filter (`builtin:auto_filter`, AudioEffect, sidechain)

A resonant filter with an envelope follower and an LFO, after Ableton's Auto Filter.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `type` | Filter Type | | Lowpass, Highpass, Bandpass, Notch, Morph | Lowpass |
| `circuit` | Circuit | | Clean, OSR, MS2, SMP, PRD | Clean |
| `slope` | Slope | | 12 dB, 24 dB | 12 dB |
| `freq` | Frequency | Hz | 20..20000, log | 2000 |
| `resonance` | Resonance | % | 0..125 | 20 |
| `drive` | Drive | dB | 0..24 | 0 |
| `morph` | Morph | % | 0..100 | 0 |
| `env_amount` | Env Amount | % | -100..100 | 0 |
| `env_attack`, `env_release` | Env Attack, Env Release | ms | 0.1..30, 1..3000, log | 6, 200 |
| `lfo_amount` | LFO Amount | % | 0..100 | 0 |
| `lfo_wave` | LFO Waveform | | Sine, Triangle, Saw Up, Saw Down, Square, S&H, Smooth | Sine |
| `lfo_sync`, `lfo_freq`, `lfo_rate` | LFO Sync, Freq, Rate | | Off/On; 0.01..40 Hz, log; Live's synced rates | Off, 1 Hz, 1/4 |
| `spin_on`, `phase`, `spin` | Spin On, Phase, Spin | | Off/On; 0..360°; 0..50 % | Off, 0, 0 |
| `lfo_offset` | LFO Offset | ° | 0..360 | 0 |
| `output` | Output | dB | -24..12 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 100 |
| `lfo_morph` | LFO Morph | % | -100..100 | 0 |
| `lfo_quantize`, `lfo_steps` | LFO Quantize, LFO Steps | | None, Steps; 2..64 | None, 8 |
| `soft_clip` | Soft Clip | | Off/On | Off |

- **The filter**: a TPT state-variable section (`dsp::Svf`) at Q `0.7071 + 24.29 × (res/125)²`; 24 dB
  puts a Q 0.5412 section before it and raises its Q by 1.8478 (a fourth-order Butterworth at no
  resonance). Each section's output is `lp × LP + bp × (k × BP) + hp × HP` (`autofilter::Weights`): a
  notch is LP + HP, and Morph walks the weights round Lowpass, Bandpass, Highpass, Notch, Lowpass.
  `responseDb` is the bilinear (prewarped) transform of the analog sections: exact for Clean.
- **Circuits**: all but Clean drive the input (Drive's gain) into a curve of their own (slope 1 at 0,
  never above its input) and squash the band-pass state over a limit of their own through the same
  curve, so the resonance saturates; the output is lowered by half Drive's gain (in dB).
- **Modulation**, in chunks of 16 samples (counted across `render()` calls): a peak follower on the
  louder channel of the input, or of the sidechain when one is connected, read as `1 − dB / −48`
  (0..1); the LFO (`dsp::Lfo`) moved to each chunk's end, free or, synced while the song plays, at the
  song's position; the right side Phase ahead (gliding the short way round) or, with Spin, running Spin
  faster. LFO Morph bends its cycle (`autofilter::morphPhase`: the shape's first half takes (1 + morph) / 2 of
  it), Quantize rounds its value to Steps levels (`quantizeLfo`). The cutoff is `freq × 2^(env_amount/100 × 6 × env + lfo_amount/100 × 4 × lfo)`, held to
  20 Hz..min(20 kHz, 0.45 sr), glided in octaves (1.5 ms) and its `tan()` stepped across the chunk.
- Type, Morph, Slope (the first section's input faded from the dry signal to its output) and Resonance
  glide a chunk at a time (10 ms); Drive, Output and Dry/Wet ramp over 20 ms; Soft Clip puts the wet output
  through `dsp::fastTanh`. No latency; a quarter second of tail.
- **Displays**: `sweep_l`, `sweep_r` (how far the modulation moves each side's cutoff from Frequency, in
  octaves), `env` (0..1) and `lfo` (-1..1), one value per 128 samples;
  `input`, a value a sample.

### Delay (`builtin:delay`, AudioEffect)

A stereo delay after Ableton's, which sounds as Valhalla Delay's Digital mode at its default preset (its
Digital character, below). The engine is [DelayDevice.h](../../engine/src/builtin/DelayDevice.h), shared with
the Vintage Delay; the characters are [DelayCharacter.h](../../engine/src/builtin/DelayCharacter.h).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `l_sync` | L Sync | | Off, On (a list) | On |
| `l_division` | L 16th | | 1, 2, 3, 4, 5, 6, 8, 16 (a list) | 3 |
| `l_time` | L Time | ms | 1..5000, log | 250 |
| `l_offset` | L Offset | % | -33..33 | 0 |
| `r_sync`, `r_division`, `r_time`, `r_offset` | R ... | | as the left's | On, 4, 375, 0 |
| `link` | Link | | Off, On | Off |
| `feedback` | Feedback | % | 0..95 | 50 |
| `freeze` | Freeze | | Off, On | Off |
| `filter` | Filter | | Off, On | On |
| `freq` | Filter Freq | Hz | 50..18000, log | 1000 |
| `width` | Filter Width | | 0.5..9 (octaves) | 8 |
| `mode` | Mode | | Repitch, Fade, Jump | Repitch |
| `ping_pong` | Ping Pong | | Off, On | Off |
| `mix` | Dry/Wet | % | 0..100 | 50 |
| `swing` | Swing | % | 0..100 | 0 |
| `drive` | Drive | % | 0..100 | 0 |
| `duck` | Duck | % | 0..100 | 0 |
| `follow_groove` | Follow Groove | | Off, On | Off |

- **Times**: synced, `division × (15 / tempo) × (1 + offset / 100)` seconds; free, the
  time in ms. Linked, the right side uses the left's four parameters. At most 10 s
  (`kMaxDelaySeconds`); each delay line is that long (rounded up to a power of two),
  allocated in `prepare()`. Read with 4-point Hermite interpolation.
- **Per side, two lines in a loop** (stages A and B, `stageOf(side, part)`): each is read
  (`echo`, at the time plus the character's modulation), given the character, filtered and driven (`wet`); A is written with the input plus feedback × B's
  wet, B with feedback × A's wet; the side's output is the sum of both wets. **Swing**
  reads A at the time × (1 + s) and B at the time × (1 − s), s = 0.5 × Swing / 100
  (`kMaxSwing`), so echoes come at T(1 + s), 2T, 3T(1 + s)…: without swing, exactly a
  single line of time T (A longer than a line holds reads at the line's end). Ping
  pong: the input summed to mono goes into the left's A, which feeds the right's B,
  which feeds the left's A (the left's B and the right's A rest). Frozen, each line is
  written with the other's echo as it was read (an exact loop) and the input is ignored;
  feedback is 1.
- **Follow Groove**: with the project's groove (`ctx.groove`), s is the groove's: how late
  its offbeat steps are on average (its default lane's odd steps, their timing in beats
  plus their milliseconds at the tempo), as a share of the side's time in beats, held to
  0..0.5. So a 16th's echoes swing as the groove's 16ths do. Without one, Swing as set.
- **Digital character** (Valhalla Delay's Digital mode, Era Past, Age 50 %, its modulation at 0.5 Hz and
  50 %, measured from renders of it): the read swings ±2.667 ms on a 0.5 Hz triangle rounded by a 2 Hz
  one-pole, starting at its middle, rising (`DigitalModulation`); each echo goes through high-passes at 5,
  10 and 10 Hz (one-poles) and the plug-in's band-limit (about 7.8 kHz, a droop of 1.3 dB at 5 kHz; its
  impulse response measured at 44.1, 48, 88.2 and 96 kHz, `DelayCharacterTables.h`, others resampled; 112
  taps at 48 kHz, peaking 11 samples late); then a compressor on each side's echoes together (the plug-in's
  one line holds every repeat at once): an RMS level of them through a high shelf (+12.5 dB above
  3.75 kHz; 2 ms up, 0.94 ms down) and the gain (1 + (level / 0.847)^1.97)^(-1/1.97) on each. What goes
  into the lines is rounded to 16 bits. Against the plug-in (time-aligned: its own time is off by up to
  half a sample), the difference is -45 dB on music at moderate levels, -30 dB driven hot at 85 % feedback.
- **Dry/Wet** crosses over at equal power: the dry × cos(90° × mix), the wet × sin(90° × mix).
- **Drive**: after the filter, in the loop, `tanh(g·y) / g` with g = 1 + 7 × Drive / 100
  (`dsp::fastTanh`): unity for quiet echoes, loud ones rounded off; bypassed at 0.
- **Duck**: the input's peak (the louder channel; 2 ms attack, 250 ms release:
  `dsp::EnvelopeFollower`) dips the wet by 1 / (1 + 16 × Duck / 100 × peak)
  (`kDuckDepth`); the loop itself is not ducked.
- **Filter**: a TPT state-variable high-pass at `freq / 2^(width/2)` then low-pass at
  `freq × 2^(width/2)`, both Butterworth, limited to 0.49 of the sample rate.
- **Modes**: Repitch glides the delay to its target with a one-pole (120 ms), so the
  read speed (pitch) changes; Fade crossfades (equal power, 60 ms) from the old time to
  the new, starting a new fade only when the last one is done; Jump switches at once.
- Dry/Wet and feedback are ramped over 20 ms (snapped on `reset()`). On one channel the
  two sides' wet are averaged. `tailSamples()`: until feedback takes the echoes down
  60 dB, at most 60 s.
- **Display**: `input`, one value per sample: the input summed to mono. The editor
  draws its spectrum behind the filter curve.

### Vintage Delay (`builtin:vintage_delay`, AudioEffect)

The Delay's engine, parameters and editor (`DelayEditor.qml`) with the Tape character: Valhalla Delay's Tape
mode at its default preset (Era Past, Age 50 %, Wow and Flutter 50 %), measured from renders of it. In
[DelayCharacter.h](../../engine/src/builtin/DelayCharacter.h):

- **The tape's speed** follows the time: slowdown k = time / 200 ms, kept in 1..3. Each echo's playback EQ
  is a curve scaled down in frequency by k (a low shelf, a broad bump, the treble's roll-off), after a fixed
  10 Hz high-pass and a fixed cut-off just under 24 kHz, 4.86 samples late; a shelf above 9 kHz follows the
  plug-in's treble steps at the times measured (interpolated between). It follows the echo's time as it is
  (gliding in Repitch), recomputed a block.
- **Wander** (the same for both sides): cubic B-splines through random values (fixed seeds, so renders
  repeat): a drift (a new value every 8.3 s, 2.6 ms RMS), wow (5 Hz, 0.32 ms RMS × (1 + 0.86 (k − 1))) and
  flutter (33 Hz, 0.064 ms RMS × (1 + 0.5 (k − 1))); and the splice: once a pass of the 10 s × k loop (the
  first 9.3 s in), the time bumps up 1.08 ms × k and settles (an impulse of 7.32 ms × k into exponentials of
  0.1875 and 0.125 s, their difference), the echo dipping 12 % at the bump's height.
- **Saturation**, on each side's echoes together: a fast compressor (an RMS level through +20 dB above
  10 kHz, 0.48 ms up, 0.23 ms down; (1 + (level / 0.476)^1.92)^(-1/1.92), smoothed over 0.04 ms), then a
  tanh well off centre (drive 0.368, bias -1.15), as a gain on each echo.
- **Feedback path**: each echo fed back goes through a 100 Hz one-pole high-pass and +2 dB at 8.5 kHz, so
  the repeats thin out at the bottom.
- Against the plug-in: the wander's depth band by band, and the level band by band within 0.5 dB below
  4 kHz (up to 2 dB above 8 kHz with feedback). Not modelled: what the plug-in's tape buffer puts above
  14 kHz that isn't the input (images), and its quiet modulation noise.

### Chorus-Ensemble (`builtin:chorus`, AudioEffect)

Modulated delays added to the sound, after Ableton Live 12's Chorus-Ensemble: from light thickening through flanging
to vibrato. Three modes: **Chorus** (Live 11's Classic: one or two delays a side, with Live 12.4's Taps and Time),
**Ensemble** (three a side, their modulation a third of a cycle apart: thicker and smoother) and **Vibrato** (one a
side, moved further, its wave from a sine to a triangle, the right side Offset behind the left). Where each voice's
delay sits and how it moves is in [ChorusDesign.h](../../engine/src/builtin/ChorusDesign.h), which the editor's
graph shares.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `mode` | Mode | | Chorus, Ensemble, Vibrato (a list) | Chorus |
| `taps` | Tap Count | | 1, 2 (a list; Chorus only) | 2 |
| `time` | Delay Time | | Auto, 7 ms, 10 ms, 20 ms, 35 ms, 50 ms (a list; Chorus only) | Auto |
| `rate` | Rate | Hz | 0.1..15, log | 0.8 |
| `amount` | Amount | % | 0..100 | 50 |
| `feedback` | Feedback | % | 0..100 (not in Vibrato) | 0 |
| `fb_invert` | Feedback Invert | | Off, On (not in Vibrato) | Off |
| `width` | Width | % | 0..200 (not in Vibrato) | 100 |
| `offset` | Offset | ° | 0..180 (Vibrato only) | 0 |
| `shape` | Shape | % | 0..100 (Vibrato only) | 0 |
| `warmth` | Warmth | % | 0..100 | 0 |
| `hp` | High-pass | | Off, On | Off |
| `hp_freq` | High-pass Freq | Hz | 20..2000, log | 100 |
| `output` | Output | dB | -36..6 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 50 |

A control a mode doesn't use is still listed, kept and automated (as Ableton keeps it); it plays again when its mode
comes back.

- **Voices.** Each reads its side's delay line at `centre + swing × lfo` (lfo -1..1), Amount being `a` (0..1):

  | Layout | Voices a side | Centre | Swing | Their phases, left / right (cycles) |
  |---|---|---|---|---|
  | Chorus, Time Auto | 1 or 2 (Taps) | 1.5 + 5a ms | 5a ms | 1 tap: 0 / ½; 2 taps: 0, ½ / ¼, ¾ |
  | Chorus, Time 7..50 ms | 1 or 2 | that time | 4a ms | as Auto's |
  | Ensemble | 3 | 7.5 ms | 5a ms | 0, ⅓, ⅔ / ⅙, ½, ⅚ |
  | Vibrato | 1 | 6 ms | 5.5a ms | 0 / Offset |

  Auto's centre grows with Amount (a short, gentle chorus at low Amount, a deep one at high), as the device did before
  Live 12.4; a fixed Time holds still whatever the Amount (no phasing creeping into a bass or a guitar). Chorus and
  Ensemble move by a sine; Vibrato by a sine that Shape morphs towards a triangle in step with it. The voices are
  averaged, so at Amount 0 the wet is the input delayed by the centre, exactly. Two taps a side are opposite (equal and
  opposite detune, their mean delay steady), and the two sides' voices are always out of step, so a mono input comes
  out wide. A delay shrinking at speed m (ms a ms) plays 1 + m times as fast, and m is at most `swing × 2π × rate`
  (swing in seconds): ±22 cents at the defaults; never so fast that a voice plays backwards (at most 0.52: Vibrato
  at 15 Hz).
- **Per channel, a sample at a time:**
  - The High-pass splits the input with a 4th-order Linkwitz-Riley crossover (`dsp::Crossover`; 20..2000 Hz, at most
    0.45 of the rate): the highs go into the delay line, the lows to the wet unmodulated, so a bass stays solid and a
    fully wet Vibrato keeps its lows. The dry goes through the same crossover (its lows and highs summed: an all-pass,
    flat in level), so it is in phase with the wet's lows and Dry/Wet blends the two whole (left as it was, the dry
    would cancel the crossover's lows round the frequency, phase-shifted by up to 180°: 13 dB at it half wet). The
    switch blends between the whole input going in and the highs, and the dry between the input and the all-pass.
    The crossover always runs, so switching it on finds it warm.
  - The voices are read before the line is written (an echo of d samples comes exactly d samples later, and feedback
    adds none), 4-point Hermite between samples, at a delay kept in double (a 50 ms line is read to a millionth of a
    sample), and averaged.
  - **Warmth** (`w` = Warmth / 100): into the line, the input blended by `w` with a soft sigmoid, `u / sqrt(1 + u²)`
    of `u = 0.65 (x + 0.15)` (the bias for even harmonics), its offset taken out and its slope at silence brought back
    to 1: a bucket-brigade chip's input stage, so echoes going round the loop saturate and darken instead of growing.
    After the voices, a one-pole low-pass at `4 kHz × w^-0.6` (about 16 kHz at 10 %, 6 kHz at 50 %, 4 kHz at 100 %;
    none at 0), and a 5 Hz DC blocker on the wet while it plays (the bias makes DC). Subtle, a colour rather than a
    limiter: at Warmth 100 a 200 Hz sine at -6 dBFS comes out 0.3 dB down with 2.5 % distortion (its 2nd harmonic at
    -33 dBc, its 3rd at -39 dBc), at 0 dBFS 1.1 dB down with 5.4 %; quiet sounds keep their level; at Amount 0 it is a
    gentle saturation of its own. There is no oversampling (the curve is inside the feedback loop, where an
    oversampler's latency can't go): the curve's bend (it less the identity) is anti-aliased through its
    antiderivative, `sqrt(1 + u²)` (first-order ADAA: the bend averaged over each step, half a sample late; a square
    root a sample), which takes the folded harmonics down 10 to 17 dB more, while the identity part has no delay or
    droop. A loud (-6 dBFS) 13 kHz tone's folded harmonics come out 47 dB down, a 9 kHz one's 53 dB, a 1760 Hz saw's
    63 dB. At Warmth 0 the wet is untouched, bit for bit.
  - **Feedback**: the wet goes back into the line through a second 5 Hz DC blocker (a DC offset passes once, as through
    any delay, and doesn't build up round the loop) and a limiter (the identity within ±1, `sign(x) (1 + tanh(|x| -
    1))` beyond it: never past ±2), so even a loud tone on the loop's resonance at Feedback 100 % stays within the
    input and ±2 (without it, it would head for 33 times the tone). 100 % is a loop gain of 0.97.
    Invert flips its sign (hollow, nasal combs). Vibrato has none, as Live's (whose Feedback and Ø are greyed
    there): going into it takes the gain down to 0, and Feedback and Invert wait for Chorus or Ensemble.
  - The wet (with the lows added back while the high-pass is on): Width scales its side (0 % mono, 100 % as it is,
    200 % twice as wide; 100 % in Vibrato), Output its level, and Dry/Wet blends it with the dry as the Delay's does
    (fully dry, the input passes bit for bit; with the high-pass on, through the crossover's all-pass).
- **Smoothing**, so no control clicks or zippers (measured: see the tests):
  - The modulation is worked out per chunk of up to 16 samples (`chorus::kChunk`). Rate (in log), Amount, Shape,
    Offset, Warmth and the high-pass frequency (in log) each glide through two one-poles in a row (of 10, 25, 10, 50,
    10 and 10 ms), solved exactly in continuous time at each chunk's middle and end, so a jump eases in and out. Each
    voice's delay is worked out there and follows the parabola through the chunk's start, middle and end: the delay
    has no corners, so the pitch never steps (a straight line per chunk would step it at every chunk's end). Warmth's
    amount and filter and the crossover's coefficients follow their glides the same way, sample by sample. The LFO's
    phase integrates the rate, so a jump of Rate never clicks.
  - Dry/Wet, Output, Width and the high-pass switch glide per sample through two one-poles of 6 ms each (a jump is
    90 % there in 23 ms, and lands exactly within about 120 ms), the feedback's signed gain (Invert passes it through
    0, Vibrato takes it to 0) and the DC blocker's blend through two of 10 ms (`dsp::Glide`; the per-chunk glides
    keep their own, solved over a fraction of a sample as well, so they land alike however a block cuts the chunks).
  - A change of Mode, Taps or Time cross-fades over 30 ms (an S-curve) from the old voices' reading of the lines to
    the new voices' (both read the same lines, so the feedback carries the blend); a change during a fade starts when
    it is done. A change that doesn't apply to the mode (Taps in Ensemble) changes nothing.
  - A chunk never crosses a display value's end, so the displays come every 128 samples however the blocks fall. A
    block's end (or automation) cuts a chunk short, but the delays follow the same curve however the chunks fall:
    renders in different block sizes differ by about 1e-5 at most.
- **Channels**: one or two; any more pass through untouched. One plays as the left of two (Chorus's one or two taps,
  Ensemble's three, Vibrato's one), without Width. A change of the channel count clears the lines and filters (a
  fill: nothing is allocated).
- `prepare()` allocates the lines (60 ms each, rounded up to a power of two), works out the glides' steps and resets.
  `reset()` clears every line and filter, starts the LFO again at phase 0 (so offline renders repeat exactly) and snaps
  every glide and ramp to its parameter. The lines then hold none of the sound, so what goes into them fades in over
  5 ms (an S-curve, as long as the renderer's switch fade): a device switched on in the middle of a sound (reset as
  it comes on, its output faded in) starts each voice's copy of it smoothly, a delay later, instead of with a step
  mid-waveform after the renderer's fade is over. (The first 5 ms of a sound that starts with a render reach the
  delays faded too.) A change of the channel count, which clears them, does the same.
- Input that isn't audio (NaN, infinity, beyond 1e30) is silence (`BuiltinProcessor::process()`), so nothing of it
  stays in the lines, the loop or the filters.
- **Denormals**: the crossovers' (`Crossover::flush()`) and the low-passes' states are flushed below 1e-20 after each
  stretch, the DC blockers flush their own, and what is fed back is gated below 1e-15, so silence rings out to exact
  zeros (after Feedback 90 %, in about 2 s).
- `latencySamples()` is 0: the delay is the effect. `tailSamples()`: the layout's longest delay at any Amount
  (`chorus::highestMs`) times one plus the repeats until the feedback has taken an echo down 60 dB (`-3 / log10(0.97 ×
  feedback)`, at most 1000; none in Vibrato), and 50 ms for the filters; at most 60 s. 2952 samples at the defaults
  (48 kHz); 12.4 s at 50 ms with Feedback 100 %. Fully dry, 0 (with the high-pass on, 50 ms for the dry's crossover).
- **Displays**, a value per 128 samples each (`chorus::kDisplaySamples`), published together so value k of each stands
  for the same samples: `phase` (the LFO's phase, 0..1, at the end of those samples: the editor draws every voice
  from it with the shared maths, so its traces move as the delays do) and `level` (the wet's peak after Output, both
  sides, in dB, floor -90 (`kLevelFloorDb`): the traces' glow). The editor takes the newest phase, and of `level`
  the values its last tick covers (`DeviceCanvas::readRecent`, which knows each display's samples per value).
- **Design**: [ChorusDesign.h](../../engine/src/builtin/ChorusDesign.h) (namespace `sub::chorus`, inline, no Qt)
  holds the layouts (`layout()`, which normalises what a mode doesn't use), `centreMs`, `swingMs`, `lowestMs` /
  `highestMs`, `voicePhase`, `lfoValue`, `delayMs`, the detune (`detuneUpCents`, `peakDetuneCents`), the warmth's
  curve (`WarmCurve`, its antiderivative, `warm`; `WarmStage`, the device's anti-aliased stage) and filter
  (`warmLowpassCoefficient`, through `dsp::onePoleCutoff`), and `limitFeedback`. The application layer's
  `chorusDelayMs()` and its neighbours ([app/src/audio/ChorusVoices.h](../../app/src/audio/ChorusVoices.h)) wrap it
  for the editor's graph, so the delays drawn are the delays that play.
- At 48 kHz on two channels it took about 0.3 % of one core at its defaults, and under 0.6 % at its heaviest
  (Ensemble, Amount 100 at 15 Hz, Feedback 100 inverted, Warmth 100, the high-pass on, Width 200; Warmth's
  anti-aliased curve and filter about 0.15 % of that), on the machine it was written on (`builtin_devices_bench
  --device chorus`, where the Disperser took 0.23 %); with every glide moving and Mode changing five times a second,
  about 0.5 %.

### Phaser-Flanger (`builtin:phaser`, AudioEffect)

Ableton's Phaser-Flanger: a phaser, a flanger and a doubler in one device, swept by two LFOs and an envelope follower
([PhaserDesign.h](../../engine/src/builtin/PhaserDesign.h), shared with its editor's graph).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `mode` | Mode | | Phaser, Flanger, Doubler (a list) | Phaser |
| `notches` | Notches | # | 1..42, 41 steps | 4 |
| `center` | Center | Hz | 70..18500, log | 1000 |
| `spread` | Spread | % | 0..100 | 50 |
| `blend` | Blend | | 0..1 | 0 |
| `flange_time` | Flanger Time | ms | 0.1..20, log | 2.5 |
| `doubler_time` | Doubler Time | ms | 20..150, log | 30 |
| `amount` | Amount | % | 0..100 | 50 |
| `feedback` | Feedback | % | 0..100 | 50 |
| `fb_invert` | Feedback Invert | | Off, On | Off |
| `lfo_sync` | LFO Sync | | Off, On | Off |
| `lfo_freq` | LFO Freq | Hz | 0.01..40, log | 0.5 |
| `lfo_rate` | LFO Rate | | Live's 22 divisions, 1/64 .. 8 Bars (`dsp::syncedDivisionLabels()`, a list) | 1 Bar |
| `lfo_wave` | LFO Waveform | | Sine, Triangle, Triangle Analog, Triangle 8, Triangle 16, Saw Up, Saw Down, Rectangle, Random, Random S&H | Triangle |
| `lfo_duty` | Duty Cycle | % | -100..100 | 0 |
| `spin_on` | Spin On | | Off, On | Off |
| `phase` | Phase | ° | 0..360 | 180 |
| `spin` | Spin | % | 0..50 | 10 |
| `lfo2_mix` | LFO 2 Mix | % | 0..100 | 0 |
| `lfo2_sync` | LFO 2 Sync | | Off, On | Off |
| `lfo2_freq` | LFO 2 Freq | Hz | 0.01..40, log | 2 |
| `lfo2_rate` | LFO 2 Rate | | as `lfo_rate` | 1/4 |
| `env_on` | Env Follow | | Off, On | Off |
| `env_amount` | Env Amount | % | -100..100 | 50 |
| `env_attack` | Env Attack | ms | 0.1..30, log | 10 |
| `env_release` | Env Release | ms | 0.1..400, log | 200 |
| `safe_bass` | Safe Bass | Hz | 5..3000, log (5: off) | 5 |
| `warmth` | Warmth | % | 0..100 | 0 |
| `output` | Output | dB | -36..6 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 50 |

The ranges are Live's (its Output a gain of 0..2, here in dB down to -36). The LFOs' ids begin `lfo_` and `lfo2_`, so
`freq` keeps meaning a filter's frequency on every device.

- **Phaser**: Notches identical second-order all-passes in a row, the Disperser's normalized lattice stages
  (`disperser::design`), tuned to Center, their Q the Spread (`qOfSpread`: 5 · 0.03^spread, 5 at 0 %, 0.866 at 50 %,
  0.15 at 100 %). Blended with the input each cuts a notch where the cascade turns the phase an odd number of half
  circles (all the way down at Dry/Wet 50 %); Spread moves them apart. Each channel has its own states, in double.
  Its feedback is taken a sample late (the last wet sample goes back in). The modulation moves Center ±3 octaves per
  unit (Blend 0) or Spread by half its range (Blend 1), or both in between. The stages are kept between 10 Hz and
  0.45 of the rate softly: within a quarter of an octave of either the swept frequency bends (a tanh knee in log2 Hz,
  `softCenterLog2`) towards the limit, and a Spread modulated past its range bends the same way (at most 0.1 beyond),
  so a sweep run into a limit turns smoothly rather than stopping with a corner (which the stages would turn into a
  tick at the LFO's rate). Only the top of the Center parameter's own range reaches the knee: at 48 kHz 18.5 kHz
  plays 1.3 Hz lower, at 44.1 kHz at 18.3 kHz (at 96 kHz and above, none of it).
- **Flanger and Doubler**: a delay line per channel (`dsp::DelayLine`, 250 ms), read with 4-point Hermite
  interpolation at the delay, its fraction in double (a float delay resolves only 1/2048 of a sample at 150 ms, which
  a moving read turns into jitter), exact at whole samples: 1 ms at 48 kHz is the input 48 samples later. Amount moves
  the flanger's delay ±2 octaves per unit, the doubler's ±15 %; the delay is held to at least 2 samples. The line is
  written in every mode (in Phaser mode with the input alone), so a delay mode coming in reads real history at once.
  After a reset the line is empty; if the first frame the device then hears isn't silent (it was switched on in the
  middle of a sound), what the line is written with fades in over 5 ms (`kStartFadeSeconds`, an S-curve, sample by
  sample whatever the blocks), so the copy read a delay later comes in smoothly instead of with a step (which would
  come after the renderer's own 5 ms switch fade had ended: a 30 ms Doubler's measured 0.134 on a 0.3 sine). A sound
  that starts after silence goes in untouched. (An offline render starts with a reset too: a sound already playing at
  its first frame, or a hit right on it, reaches the line faded the same way.)
- **Feedback**: `feedbackGain` (±0.95 at 100 %, negative with Ø) into the core's input, through a soft limit (as it
  is up to +6 dBFS, then bent to stay below ±4): 95 % with any modulation can't run away.
- **Modulation**: `(1 - mix2) · LFO 1 + mix2 · LFO 2`, times Amount, plus the envelope times Env Amount, held to ±2.
  Control works per chunk of 16 frames: the LFOs and the envelope give the modulation at each chunk's end; it is
  drawn across the chunk and smoothed sample by sample (two one-poles of 1 ms in a row). The Phaser's stages move
  linearly from one chunk end's coefficients to the next's (a lattice's coefficients in between are points inside the
  unit circle: it can only lose energy while they move), unless that straight line would miss their true way at the
  chunk's middle by more than a millionth of an octave (of the stage's frequency or its bandwidth): a fast, deep
  sweep, a triangle's turn, Amount or Duty gliding, the envelope's attack, where straight pieces would kink at every
  chunk's end. Then they are designed sample by sample from the smoothed modulation, as while Center glides. The
  delays follow the smoothed modulation sample by sample (a long delay, swept, would turn a chunk's corners into a
  zipper).
- **LFOs**: Ableton's ten shapes (`waveValue`), Duty bending the phase (`warpPhase`: the first half of the shape takes
  5..95 % of the cycle; Rectangle's width). Triangle 8 and 16 step the triangle 8 or 16 times a cycle. Triangle Analog
  is a ±1 rectangle through a 100 ms one-pole in its steady state (`analogShape`, `analogValue`): nearly square when
  slow, a quieter rounded triangle when fast (0.46 high at 5 Hz), its shape following the rate (glided, so a jump of
  Freq doesn't jump its level). Random glides from one cycle's value to the next's, Random S&H holds one a cycle,
  both from the cycle's number (renders repeat exactly). LFO 2 is a triangle. Free, they run at 0.01..40 Hz (at
  40 Hz, a chunk is 1/75 of a cycle). The right channel runs Phase ahead of the left (gliding to it the short way
  round, 50 ms), or with Spin `1 + spin` times as fast (at most 1.5). Synced while the song plays an LFO is at the
  song's position (`(beat / division)`, cycles and all); stopped, it runs free at the tempo's rate. The divisions
  are Live's: 1/64, 1/48, 1/32, 1/24, 1/16, 1/12, 1/8, 1/6, 3/16, 1/4, 5/16, 1/3, 3/8, 1/2, 3/4 of a bar, then 1,
  1.5, 2, 3, 4, 6 and 8 bars (`dsp::syncedCycleBeats`).
  Where its phase jumps (a new waveform, Sync switched on, a synced division changed, the transport starting or
  looping) its value crossfades from where it was over 20 ms (an S-curve) rather than jump.
- **Envelope follower** (Env Follow): the input's peak (the louder channel; before Safe Bass) with Attack and Release,
  as `-48..0 dBFS → 0..1` (`envelopeAmount`). Off, once its amount has glided to 0, it is reset, so switching it on
  starts from silence.
- **Warmth**: `y + w (L(tanh(2y) / 2) - y)`, `L` a 5 kHz one-pole: the effect's output saturated and darkened, inside
  the feedback loop, so echoes and resonances darken as they recirculate. Skipped while 0. The saturation runs at the
  sample rate, antialiased by its antiderivative (first-order ADAA: each sample the curve's mean between the last input
  and this one, `(F(y) - F(y₋₁)) / (y - y₋₁)` with `F = ln cosh(2y) / 4`; its value at the middle when the two are
  within 1e-5), so the harmonics it makes beyond Nyquist fold back far quieter: a 9 kHz tone at -1 dBFS (48 kHz) folds
  its 3rd to 21 kHz 31 dB under the tone and its 5th to 3 kHz 49 dB under (20 and 26 dB unantialiased), a 7 kHz tone its
  5th to 13 kHz 49 dB under (34). Only what the curve adds to `y` goes through the mean; `y` itself passes as it is, so
  a quiet signal has exactly the response the design draws (no half-sample delay).
- **Safe Bass**: a Linkwitz-Riley crossover (`dsp::Crossover`) at its frequency per channel; the lows stay dry
  (`out = low + (1 - mix) high + mix wet(high)`: the bands add up to an all-pass, flat in level). It fades in and out
  over 20 ms, its crossover's frequency moving sample by sample as it glides; at 5 Hz it is off and costs nothing.
- **Output and Dry/Wet**: `out = Output · ((1 - mix) dry + mix (s low + wet))`. Fully dry with Output at 0 dB and Safe
  Bass off it passes the input bit for bit, whatever the rest does.
- **Nothing steps**: Center, Spread and Blend glide (two one-poles of 20 ms in a row), the delay times too (50 ms: a
  gentle swoop of pitch), and while they glide what they move is worked out sample by sample (the stages designed, as
  the Disperser's are; the delays), landing exactly on what is set (the delay times within 1e-9 of an octave: a
  coarser landing would be a jump of 0.005 samples at 150 ms). Amount, Env Amount, LFO 2 Mix, Duty and Safe Bass's
  frequency glide per chunk (20 ms each pole); Feedback, Warmth, Dry/Wet and Output per sample (5 ms each pole). A
  change of Mode crossfades the cores over 30 ms (into the Phaser, its cascade starts from silence and its input
  fades in; a delay core's feedback fades with its share; Flanger and Doubler crossfade two reads of one line, with
  no swoop and no gap). A change of Notches fades over 20 ms as the Disperser's Amount does (fewer: to the tap after
  the stages kept; more: the stages added hear their input fade in). A change during a fade waits for it. Every one
  of the 30 controls changing in turn where it is heard (81 changes over 16 s), then a 5 Hz sweep run into the
  stages' top and bottom limits, a 220 Hz tone's 6th difference stays below 2e-5 (the Disperser's limit: 1e-4):
  1.9e-5 at Dry/Wet's jump from 0 to 100 %, 1.7e-5 at Output's 30 dB, every other change 1.4e-5 at most and most of
  them about 2e-6, which is also what a steady sweep measures.
- **Denormals**: it doesn't rely on the renderer's flushing. Recursive states (the cascade's, Warmth's, the
  crossovers') are flushed per chunk (`dsp::flushTiny`, `Crossover::flush()`); the cascade's output and the delay
  line's writes below 1e-20 are zero, and so is an output sample that would be a denormal float (gains gliding to 0
  together, Dry/Wet and Warmth turned down at once, take their product through that range). Silence rings out to
  exact zeros. NaN and infinity in its input never reach it (`BuiltinProcessor::process()` takes them as silence): it
  plays on as if they had been zeros.
- `reset()` clears every state, starts the LFOs from phase 0 and snaps every glide and fade to the parameters (an
  offline render starts the same every time), and arms the line's start fade; `prepare()` sizes the lines (250 ms) and
  resets. On one channel the left LFO plays; channels past the second pass untouched.
- `latencySamples()` is 0 (the delay and the sweep are the effect). `tailSamples()` (0 fully dry): the feedback's
  passes to -60 dB (`ln 1e-3 / ln |g|`) times the loop's longest delay (Phaser: the slowest stage the modulation
  reaches, its largest group delay times Notches, plus its own ring, with half as much again to spare; the delay
  modes: the longest delay), plus Safe Bass's ring; at most 60 s.
- **Displays**, one value per 256 samples each, pushed together: `phase` and `phase_r` (LFO 1's phase on the left and
  the right, 0..1), `lfo` (LFO 1's value on the left, after the shape, Duty and any jump's fade), `mod` (the left's
  smoothed modulation, -2..2), `env` (the envelope's 0..1; 0 while Env Follow is off), `sweep_l` and `sweep_r` (Phaser:
  the stages' frequency in Hz, as tuned; Flanger and Doubler: the delay in ms; during a mode's fade, the incoming
  mode's), `q_l` and `q_r` (the stages' Q; 0 in the delay modes), `input` and `output` (the peaks, dB, floor
  `kLevelFloorDb`, -90), and `mod_r` (the right's smoothed modulation; one channel: the left's). Each side's sweep
  and Q are its parameters at that side's modulation (`phaserCenterHz`, `phaserQ`, `delayMs` of `mod` or `mod_r`):
  the editor works them out so from the parameters as they are, so an edit shows at once. The editor reads the
  rate (`kDisplaySamples`) and the parameters' ranges through the application layer (`phaserDisplaySamples()`,
  `phaserRanges()`).
- **Response**: what plays, as a linear filter (Warmth's saturation left out), is `phaser::transfer()` and
  `responseDb()`: the cascade's `A^N` through the feedback loop and its extra sample (`P / (1 - g z⁻¹ P)`), or the
  comb's `D / (1 - g D)`, with Warmth's low-pass, Safe Bass's bands, Dry/Wet and Output. `notchFrequencies()` puts the
  notches in closed form (the bilinear transform keeps the analog all-pass's phase at the warped frequency:
  `Ω² + (cot θ / Q) Ω - 1 = 0`), `stagePhase()` a stage's phase, unwrapped. `phaser::curve()` is what the editor draws:
  per column of the plot, up to 12 points and every notch inside it where the response turns less than a cycle; where a
  comb is finer than the columns, its top and bottom at 48 fixed angles of the turning factor (the same each time, so
  the band stands still as the sweep moves). It designs the Phaser's stage and works out Output's gain once per curve
  (the same sums `responseDb()` does per point): 0.1-0.3 ms for the editor's 226 columns. The application layer's
  `phaserResponseDb()` and `phaserCurvePoints()` ([PhaserResponse.h](../../app/src/audio/PhaserResponse.h)) wrap them,
  so the curve drawn is the sound; `curve()` also gives each column's turn (the radians the wet path turns across it,
  what tells the dense columns), with which the graph draws a comb finer than it can show as a line as a band too.
  `wetTransfer()` is the wet path alone (what Dry/Wet at 100 % plays, before Output).
- **Cost** (`builtin_devices_bench`, 48 kHz stereo): about 0.2 % of one core at the defaults (0.21 %, the
  Disperser 0.21 % in the same runs); the Flanger 0.30 % and the Doubler 0.25 %; 42 notches, Feedback 95 %, Amount
  100 %, Random S&H 0.65 %; a deep, fast sweep (Amount 100 %, a 5 Hz sine: the stages designed every sample) 0.49 %
  (at 40 Hz, Live's fastest, 0.48 %); Warmth at 100 % 0.43 % (the Flanger's 0.68 %: the antialiased saturation's exp
  and log, per sample and channel); everything on at once (42 notches, Triangle Analog, Warmth, Safe Bass, Env
  Follow, LFO 2, Spin) 1.3 %. Center automated without pause 0.49 %, with 42 notches 1.0 %.

### Reverb (`builtin:reverb`, AudioEffect)

An algorithmic reverb after Ableton Live 12's Reverb: an input filter, early reflections, and a diffusion network with a
decay time per band, chorus, Freeze, and three output levels. Mono in, stereo out, as Live's is: the input's two sides
are summed before the reverb (a source panned hard left reverberates in the middle), and Stereo sets how wide the reverb
comes out. The maths it shares with its editor is in [ReverbDesign.h](../../engine/src/builtin/ReverbDesign.h)
(namespace `reverb`).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `predelay` | Predelay | ms | 0.5..250, log | 2.5 |
| `lo_cut` | Lo Cut | | Off, On | On |
| `hi_cut` | Hi Cut | | Off, On | On |
| `in_freq` | In Filter Freq | Hz | 50..18000, log | 830 |
| `in_width` | In Filter Width | oct | 0.5..9 | 7.5 |
| `spin` | ER Spin | | Off, On | On |
| `spin_rate` | ER Spin Rate | Hz | 0.07..1.3, log | 0.3 |
| `spin_amount` | ER Spin Amount | % | 0..100 | 25 |
| `shape` | ER Shape | % | 0..100 | 50 |
| `density` | Density | | Sparse, Low, Mid, High | High |
| `smooth` | Size Smoothing | | None, Slow, Fast | Slow |
| `size` | Room Size | size | 0.22..500, log (a bare number) | 100 |
| `stereo` | Stereo Image | ° | 0..120 | 100 |
| `lo_shelf` | Lo Shelf | | Off, On | On |
| `lo_freq` | Lo Shelf Freq | Hz | 20..15000, log | 90 |
| `lo_gain` | Lo Shelf Gain | % | 20..100 (of Decay) | 75 |
| `hi_filter` | Hi Filter | | Off, On | On |
| `hi_type` | Hi Filter Type | | Shelf, Low-pass | Shelf |
| `hi_freq` | Hi Filter Freq | Hz | 20..16000, log | 4500 |
| `hi_gain` | Hi Shelf Gain | % | 20..100 (of Decay) | 70 |
| `decay` | Decay Time | ms | 200..60000, log | 1200 |
| `freeze` | Freeze | | Off, On | Off |
| `flat` | Flat | | Off, On | On |
| `cut` | Cut | | Off, On | On |
| `diffusion` | Diffusion | % | 0..100 | 70 |
| `scale` | Scale | % | 0..100 | 50 |
| `chorus` | Chorus | | Off, On | On |
| `chorus_rate` | Chorus Rate | Hz | 0.01..8, log | 0.8 |
| `chorus_amount` | Chorus Amount | % | 0..100 | 20 |
| `reflect` | Reflect Level | dB | -30..6 | 0 |
| `diffuse` | Diffuse Level | dB | -30..6 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 40 |

- **Input filter**: a TPT state-variable high-pass (Lo Cut) at `freq / 2^(width/2)` then low-pass (Hi Cut) at
  `freq * 2^(width/2)`, both Butterworth, each held to 10 Hz..0.45 of the rate. Each switch cross-fades its section in
  or out; a section switched off (and done fading) is skipped. Its exact magnitude is `reverb::inputFilterDb`.
- **Size** scales every delay of the reverb (reflections, diffusers, lines) by `s = sqrt(size / 100)`: 0.047 at
  0.22 (tiny and metallic), 2.24 at 500. It glides at Smooth's pace (two one-poles of 2 ms, 350 ms or 50 ms: None,
  Slow, Fast), a sample at a time; with None the tail's pitch sweeps fast, but never steps. A change of Smooth glides
  the pace itself (in log, over about 50 ms), so a sweep under way speeds up or slows down without a corner.
- **Early reflections**: 12 taps of the filtered input from the predelay on, at 0..53.6 ms times `s` (the first at
  the predelay itself: Predelay is the time to the first reflection, as Live's), each with a sign and a pan
  (`reverb::kTapMs`, `kTapPan`, `kTapSign`; Sparse uses every other one), read with 4-point Hermite interpolation.
  Shape sets their envelope, `exp(-a t / t_last)` with `a = 0.4 + 5.6 shape`, the gains sharing an energy of 0.49
  (`earlyTaps`): low, they fade slowly; high, fast. Spin drifts each tap's time later (0 to 2 ms at 100 %, times
  `min(1, s)`: never before the predelay; `reverb::spinDriftMs`) and swings its pan, each tap a twelfth of the LFO's
  cycle on from the one before. The pan swings in the angle of an equal-power pan (left `cos t`, right `sin t`; at rest
  exactly `sqrt((1 -+ pan) / 2)`), by up to `asin(0.7) / 2` (a tap in the middle swings +-0.7), less near the field's
  edges, so no tap swings past one and its gains never turn a corner (`reverb::spinPan`). Reflect sets their level.
- **The network's input** is read from the same line Shape's onset after the predelay (`0.8 * 53.6 ms * s * shape`:
  the diffuse sound starts sooner when the reflections fade slowly, later when they fade fast), drifting with the
  first reflection (so Spin's motion reaches the tail, as Live's manual has it), then blurred by 2, 3 or 4 Schroeder
  all-passes in a row (Dattorro's lengths, 3.6..12.7 ms, times `s` and Scale's `0.25..1.75`; gains 0.75 / 0.625 times
  the square root of Diffusion, so its default blurs the echoes smooth by about 150 ms).
- **The network**: a feedback delay network of 4, 8 or 16 lines (Sparse, Low, Mid and High; 29..98 ms times `s`,
  about geometric and off exact ratios). Each line is read through a first-order Thiran all-pass (lossless, so
  every pass loses exactly what the design says at every frequency; linear or Hermite reads would dull the highs a
  little each pass), then through two TPT one-pole shelves: the high one passes `kHi` above its corner (or, as a
  low-pass, nothing), the low one `kLo` below its own. Each line's gain and shelves are worked out from its own
  length (`reverb::loop`), so every line loses the same dB per second in each band: the middle `-60 / Decay`, the
  shelved bands `-60 / (Decay * share)` at their far end. High adds a Schroeder all-pass in each loop (11 % of the
  line times Scale's factor). A Walsh-Hadamard transform, rotated by one line (a bare Hadamard is its own inverse:
  every second pass would undo the mixing), mixes them and writes them back with the input (signs per line). The
  tail's left and right are two orthogonal Hadamard rows of the lines' reads, times 1/4 whatever the Density (the
  sum carries all the network's energy however many lines share it, so every Density is as loud) and a trim of 1.3
  (`reverb::kDiffuseGain`: the tail about as loud as the reflections at the defaults), times Diffuse.
- **Stereo Image** (degrees, as Live's) scales the wet's side: 0 is mono, 120 the network's own two sides, independent
  of each other (late-tail correlation about 0), the default 100 a little narrower (`reverb::stereoWidth`: stereo /
  120).
- **Moving delays**: Chorus drifts each line's delay (up to +-2 ms, a quarter of the line at most; each line at its
  own rate, 0.885..1.115 of Chorus Rate, and phase); Size and Scale glide them. A Thiran all-pass's state was worked
  out for the delay a sample before, so it is moved with the delay (by the step times the input's slope there),
  which leaves an error of the step's square: a sweep reads about 20 times cleaner. When the whole number of samples
  changes, the state is worked out again for the new split, running the new all-pass over the last 4 samples, so the
  change leaves no transient.
- **Freeze, Cut, Flat**: Freeze glides the tail's decay to 1000 s with Cut (the input no longer reaches the network:
  new sound makes reflections only) and to 60 s without (the input keeps adding, and a held input settles at a level
  rather than growing). Flat glides the shelves flat while frozen, so every band holds; off, the shelves go on taking
  their bands away (the frozen tail darkens while its middle holds).
- **Guard**: while the tail's output (before Diffuse) peaks above 8 (+18 dBFS), the loops' gains are eased down 3 %
  a sub-chunk, at most to half, and back once it doesn't (`gain += 0.002 (1 - gain)`). Ordinary material never gets
  near it: full-scale noise into a frozen tail that takes its input settles about +0.5 dBFS RMS.
- **Density** crossfades from the network as it is to the new one over 20 ms, both running (the lines of either,
  read and filtered once; each network's own diffusers, loop all-passes, matrix, input signs and outputs). Each line is
  written the crossfade (cos and sin of a quarter turn, on an S-curve) of what the two networks write into it, kept at
  its power (the two writes' correlation, worked out at the switch, divided out), so the lines that stay are never
  faded through silence: a frozen tail keeps what they hold (High and Mid share every line; Low's are all High's).
  Lines, loop all-passes and diffusers that join start from silence (let go, as below) and hear their input ease in
  (one that heard a step would play it back a delay later); those that leave are faded out, taking what they hold. A
  loop all-pass's length moves into or out of the loops' gains over the crossfade. A change during one waits for it
  to end. The reflections glide to the new Density's taps.
- **Smoothing**: what moves a delay (Predelay, the onset, Size, Scale) and the levels on the audio (Reflect, Diffuse,
  Stereo, Dry/Wet, the input filter's switches) glide a sample at a time, two one-poles in a row (`dsp::Glide`; 25 ms,
  Smooth's, 50 ms, 5 ms), so nothing has a corner. Everything else glides every 32 samples (two one-poles of 20 or
  30 ms; the frequencies, times and LFO rates in log) and what it sets (the loops' and shelves' gains and corners, the
  input filter's corners, the all-passes' gains, the taps' gains and Spin's drift, Chorus's drift) is ramped linearly
  across the 32. Every glide lands on its target exactly once within a hair of it, so a settled control is exactly its
  target and nothing is worked out again. Dry/Wet is `dry (1 - mix) + wet mix`: exactly dry at 0, exactly wet at 1.
- **Sleeping**: once the input has been below -160 dB for longer than the input line reaches, and the tail and
  reflections are below -120 dB (not frozen, no Density change under way), it sleeps: the wet is exactly 0, the
  network is skipped, and the output of silence is exact zeros. The glides and LFOs go on (the Spin and Chorus
  displays keep stepping, or go to -1 if switched off meanwhile); what they set is snapped where it is when sound
  wakes it. Its buffers are let go as it falls asleep: they read as silence from then on, so a reverb that wakes
  reaching further back (Predelay, Size or Shape raised while it slept) hears silence, not what it held. No buffer is
  cleared whole at once (falling asleep, waking, a reset or a change of Density alike): each 32 samples clear only
  what their reads will reach (as far as the delays' glides go in those 32, worked out a sample at a time, plus a few
  samples: a jump of Size clears as its glide travels, not to its target at once), and asleep a slice of 16384 floats
  of the rest goes each 32 samples. States below 1e-20 are flushed after each sub-chunk.
- `reset()` lets every buffer go and clears every state, restarts the LFOs and snaps every glide: renders after it are
  the same every time. On one channel, that channel is the input and the wet's middle the output; channels past two
  are left alone.
- `latencySamples()` is 0. `tailSamples()`: the predelay, the onset and Spin's drift of it, the diffusers, a pass of
  the longest loop and Chorus's depth, 1.15 times Decay and 0.1 s (then the tail is 60 dB down); frozen, or at most, a
  minute.
- **Displays**, the 256-sample ones pushed together: `input` (the mono input's peak, dB, floor -90:
  `reverb::kMeterFloorDb`), `early` (the reflections' peak after Reflect), `diffuse` (the tail's RMS after Diffuse),
  `spin` and `chorus` (the LFOs' phases 0..1 after the value's last sample, so they step by exactly `rate * 256 / fs`;
  -1 while switched off and faded, asleep too; the chorus one is the first active line's), and per sample `signal`
  (the mono input, before the filter) and `tail` (the tail's mid after Diffuse). Asleep, the levels read -90 and the
  per-sample ones publish nothing.
- **Shared design**: `reverb::decaySeconds(settings, f, fs)` is the tail's decay time per frequency as the network
  plays it (the loop of a line of the active lines' mean length, through the same `rates`, `loop` and one-pole
  responses: in a network that mixes every line into every other each pass, energy decays at the lines' mean loss
  over their mean length); `inputFilterDb`, `earlyTaps`, `spinPan`, `onsetMs`, `stereoWidth` give the editor the
  band, the reflections, Spin's swing and Stereo's width. The application layer wraps them for the editor
  ([app/src/audio/ReverbResponse.h](../../app/src/audio/ReverbResponse.h)).
- It took about 1.4 % of one core at the defaults (High, Spin and Chorus on) at 48 kHz stereo on the machine it was
  written on (`builtin_devices_bench`), 1.8 % at its heaviest (Size 500, Predelay 250 ms, Decay 60 s, Spin and Chorus
  at 100 % and their fastest, Diffusion and Scale 100 %, Shape 100 %); Mid 1.4 %, Low 1.0 %, Sparse 0.65 %, Chorus off
  1.3 % (measured while the steady network still had one more multiply a line); about twice its cost
  for the 20 ms of a Density change; asleep 0.05 %. No block costs much more than its neighbours: at 192 kHz in blocks
  of 256, one that wakes it just after it fell asleep takes about what any other does (it took three times as long
  when the rest of the buffers were cleared then), also with Size jumping from its least to its most as it wakes
  (about 1.8 times as long when the clearing reached the glide's target at once); a Density change's first block
  about what the rest of its crossfade does (up to three times as long when the lines that join were cleared whole);
  so does a reset. Its buffers take about 1.4 MB at 48 kHz (6 MB at 192 kHz).

### Vintage Hall (`builtin:vintage_hall`, AudioEffect)

A 1970s digital concert hall: a clone of a classic plug-in's "Concert Hall" in its 1970s color, its structure
measured from renders of it. The numbers are in
[VintageHallDesign.h](../../engine/src/builtin/VintageHallDesign.h) (namespace `vintage_hall`). Stereo in, stereo out.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `mix` | Dry/Wet | % | 0..100 | 100 |
| `predelay` | Predelay | ms | 0..500 | 20 |
| `decay` | Decay | ms | 200..70000, log | 4000 |
| `size` | Size | % | 0..100 | 100 |
| `attack` | Attack | % | 0..100 | 50 |
| `bass_mult` | Bass Mult | x | 0.25..4, log | 1.5 |
| `bass_xover` | Bass Xover | Hz | 100..10000, log | 300 |
| `high_shelf` | High Shelf | dB | -24..0 | -24 |
| `high_freq` | High Freq | Hz | 100..20000, log | 6000 |
| `early_diffusion` | Early Diffusion | % | 0..100 | 100 |
| `late_diffusion` | Late Diffusion | % | 0..100 | 100 |
| `mod_rate` | Mod Rate | Hz | 0.1..10, log | 2.53 |
| `mod_depth` | Mod Depth | % | 0..100 | 38 |
| `high_cut` | High Cut | Hz | 100..20000, log | 8000 |
| `low_cut` | Low Cut | Hz | 10..1500, log | 10 |

- **Rates**: the reverb runs at the host's rate over `rateDivider()` (2 at 44.1/48 kHz, 4 at 88.2/96, 8 above):
  about 24 kHz, as the hardware it recalls did. Down and back up through a 12th-order elliptic low-pass (0.01 dB
  ripple, 90 dB down, its edge at 0.2495 of the rate it runs at; `kResampler`), once per halving each way: flat to
  12 kHz at 48 kHz, nothing above 13 kHz, and the original's ringing at the edge. The wet comes out a host sample
  later than the filters alone would give, as the original's does. Every length is in samples at 24 kHz
  (`kReferenceRate`) and scales to the rate the reverb runs at.
- **High Cut and Low Cut**, on the way in at the host's rate: a one-pole low-pass (bilinear, `highCut`) and a DC
  blocker `y = x - x[-1] + r y[-1]`, `r = exp(-2 pi f / fs)` (`lowCut`: it lifts the top a little, +0.8 dB at a
  quarter of the rate at 1.5 kHz, as the original's does).
- **Input**: times 0.9695, then each side's fixed delay (with Predelay) into **the ring**: one loop of two halves,
  the left coming in at its start and the right half way round, each just after the other half's loss and before
  its high shelf and two Schroeder all-passes (`v = x + g r`, `y = r - g v`; 168 and 288 samples on the left, 144
  and 240 on the right; gains 0.75 and 0.656 times Early Diffusion). Then, per half, two nested all-passes (A and B,
  C and D: the outermost's gain 0.5 times Late Diffusion, those inside 0.25 times it; B and D three levels deep),
  plain delays between them, and the loss: the decay's gain for the half's length along the loop
  (`halfGainDb`: -60 dB in Decay seconds) and the bass crossover, `y = x + (g_low - 1) lp(x)` with a one-pole
  low-pass `lp` at Bass Xover (`a = exp(-2 pi f / fs)`) and `g_low` what makes the band below decay in Decay times
  Bass Mult. The high shelf is `(G s + w) / (s + w)` at High Freq by the bilinear transform without prewarping, so it
  reaches High Shelf at half the reverb's rate (12 kHz). The whole loop is 13442 samples (560 ms) at Size 100 %.
- **Output taps**: each side hears three points of its own half (the first right after its all-passes) and one of
  the other half (L4 on the left, R1 on the right, negative). **Attack** sets their weights (`tapWeights`: straight
  lines between four sets at 0, 1/3, 2/3 and 1): at 0 the first tap leads, at 100 % only the third is left (243 ms
  of loop later), so the reverb swells. The weights glide in a straight line across each block.
- **Modulation**: the innermost delay of B and of D is longer by up to 2.67 ms times Mod Depth, wandering at a
  steady speed (the whole range in 1 / Mod Rate seconds) towards targets picked at random (`Wander`, a fixed seed
  per reset: a render from the same start repeats). It is read linearly between samples (as the original's 1970s
  mode does: the highs lose a little as it moves, up to 6 dB at 8 kHz a pass at half a sample).
- **Size** scales the nested all-passes from 0.2 to 1 (`ringScale`), the input all-passes from 0.4 to 1.2 below
  50 % and not at all above (`inputScale`), the fixed delays below 50 % (`fixedDelay`), and the plain delays as
  measured (`plainScale`: straight lines below and above 50 %, the loop as a whole always as `ringScale`). Each
  half's loss is worked out from its length as it is set. Size and Predelay glide every delay (a one-pole of 80 ms,
  read linearly between samples while it moves).
- **Controls** glide a step a block towards the parameters (30 ms); Dry/Wet over 20 ms, equal power: the dry by
  `cos`, the wet by `sin` of a quarter turn times the mix.
- **How close**: with the modulation off, its output and the original's differ by 30 to 60 dB below them up to
  10 kHz (with the input's and output's phase difference taken out: the original's resampler is not this elliptic,
  the same in level only), for every Decay, Attack, Bass, High Shelf, High and Low Cut, Predelay and Dry/Wet; Size
  below 100 % puts each echo within 0.1 ms (50 % and up) or 0.5 ms (below) of the original's. With the modulation
  on, the decay per octave and how far a held tone spreads in the tail match to about the original's own variation
  from one render to the next.
- **Displays** for its editor, one value per `kDisplaySamples` (256) host samples: `input` and `output` (the peak
  going into the reverb, after High and Low Cut, and of the wet; dB, floored at `kDisplayFloorDb`, -96), `l1`..`l4`
  and `r1`..`r4` (each output tap's peak, at the reverb's rate) and `mod_b`, `mod_d` (where B's and D's moving
  delays are, 0..1). `ringMap()` gives where each part of the loop starts for Size, as the lengths are set, for
  the editor to draw it ([VintageHallResponse.h](../../app/src/audio/VintageHallResponse.h)).
- **Silence**: once the input has been below -160 dB for longer than it takes to reach the ring (`inputReach`: the
  fixed delays with Predelay, and 10 ms), and the ring's taps and the wet have stayed below -120 dB for a whole lap
  (`lapReach`), it sleeps: everything it holds is cleared, the wet is exactly 0 and the reverb is skipped, the dry
  still going through Dry/Wet (its glide too), the displays at their floor. The other controls aren't followed
  while it sleeps; any sound over -160 dB wakes it in the block it comes, every control starting where it is, as
  after a reset. A tail is heard to its end: only below -120 dB does it stop.
- **Tail**: Decay times Bass Mult (at least 1) times 1.2, plus Predelay and a second. No latency. About 0.25 % of a
  core at 48 kHz stereo while it plays (Dry/Wet's gains worked out once a block while it doesn't move), 0.03 %
  asleep (`builtin_devices_bench --device vintage_hall --input sound|silence`).

### Disperser (`builtin:disperser`, AudioEffect)

Phase dispersion after Kilohearts' Disperser: identical second-order all-passes in a row
([DisperserDesign.h](../../engine/src/builtin/DisperserDesign.h)). Every frequency keeps its
level; what is around the stages' frequency comes out later (a transient becomes a chirp).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `amount` | Amount | stages | 0..64, 64 steps | 16 |
| `freq` | Frequency | Hz | 20..20000, log | 1000 |
| `pinch` | Pinch | Q | 0.1..10, log | 1 |
| `mix` | Dry/Wet | % | 0..100 | 100 |
| `bypass` | Bypass | | Off, On | Off |

- **A stage** is the bilinear transform of the analog all-pass (s² - s/Q + 1) / (s² + s/Q + 1),
  prewarped to the frequency (as the RBJ cookbook's): (A2 + A1 z⁻¹ + z⁻²) / (1 + A1 z⁻¹ + A2
  z⁻²), magnitude exactly 1. The frequency is kept below 0.45 of the sample rate
  (`stageFrequency`). Every stage and channel share the coefficients; each channel has its
  own states (in double).
- **It runs as a normalized lattice** (Gray and Markel): two rotations, by k1 = -cos w0 and
  k2 = (1 - a) / (1 + a) (a = sin w0 / 2Q), each with its c = sqrt(1 - k²). Rotations keep
  energy, so a stage's output and states hold exactly what went in however its coefficients
  move: modulating Frequency or Pinch never puts out more energy than the device was given.
  (A state-variable filter's band-pass state holds Q times the signal, and dropping Q while it
  rings throws that out: tried while choosing, one put out up to 40 times the energy it had been
  given, where the lattice put out none more.) The
  output is one multiply-add from the input, so the cascade's path through 64 stages is short.
- **Frequency and Pinch glide** in log, through two one-poles of 20 ms in a row (a jump eases
  in and out), and the coefficients follow sample by sample while they move. Swept fast through
  many narrow stages the glide is heard as the sweep itself (what the stages held catches up as
  the delay shrinks), never louder than what went in.
- **Amount** fades over 20 ms (an S-curve) to the output after the new number of stages: fewer,
  from the longer cascade's output to the tap after the stages kept; more, the stages added start
  from silence and their *input* fades in while the old output fades out (fading their output
  instead would let them smear the signal's abrupt start into a chirp longer than the fade). Stages
  no longer heard are cleared. A change during a fade starts when it is done. At 0 stages (and
  none coming) the input passes untouched, bit for bit.
- **Dry/Wet and Bypass** glide (two one-poles of 5 ms each: they can turn back halfway without a
  kink); what is heard of the stages' output is Dry/Wet's share times Bypass's (0 or 1), the rest
  the input. With no latency the two line up to the sample, so in between they add up as a
  phaser's do, |(1 - mix) + mix H|: notches where the stages turn the phase half a circle (all the
  way down at an even blend). The level is flat only fully wet. Fully dry or bypassed, once there,
  it passes the input bit for bit. The stages keep running meanwhile, so it comes back without a
  seam; switching the device off saves them.
- **Denormals**: states below 1e-20 are flushed to 0 after each stretch, so silence rings out to
  exact zeros. `reset()` clears every stage and snaps the glides and fades to the parameters;
  `prepare()` (a new sample rate) works out the glides and fades again and resets.
- `latencySamples()` is 0: the dispersion is the effect, and the engine doesn't delay the other
  tracks against it. `tailSamples()` (0 with no stages, bypassed or fully dry): until what is left of the impulse response is 60 dB down,
  1.25 times the largest group delay (`maxGroupDelaySamples`, which looks at the poles' angle too:
  high up the peak is narrower than any grid) plus 7 time constants of the slowest pole
  (`decayPerSample`), a bound measured over the parameters' range at 44.1 and 192 kHz; at most 60 s.
- **Group delay**: `disperser::groupDelayMs(stages, freq, pinch, sampleRate, frequency)` works it
  out from the stage's biquad coefficients (each polynomial's Re(Σ n cₙ z⁻ⁿ / Σ cₙ z⁻ⁿ)). The
  application layer's `disperserGroupDelayMs()`
  ([app/src/audio/DisperserResponse.h](../../app/src/audio/DisperserResponse.h)) wraps it for the
  editor's graph, so the curve drawn is the delay that plays. Its peak is about 2Q / (pi f) seconds
  per stage.
- 64 stages on two channels took about 1.5 % of one core at 48 kHz on the machine it was written on.

### EQ (`builtin:eq`, AudioEffect)

An equalizer after Pro-Q: 24 bands, then an output gain.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `b1_used` .. `b24_used` | Band n Used | | Off, On (hidden, not automatable) | Off |
| `bn_on` | Band n On | | Off, On | On |
| `bn_type` | Band n Type | | Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass, Tilt Shelf | Bell |
| `bn_freq` | Band n Freq | Hz | 10..22000, log | 1000 |
| `bn_gain` | Band n Gain | dB | -30..30 | 0 |
| `bn_q` | Band n Q | | 0.025..40, log | 1 |
| `bn_slope` | Band n Slope | | 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct (a list) | 12 |
| `bn_place` | Band n Placement | | Stereo, Left, Right, Mid, Side | Stereo |
| `output` | Output | dB | -36..36 | 0 |
| `scale` | Gain Scale | % | 0..200 | 100 |

- A band plays while it is `used` and `on`. Its gain is times `scale` / 100 (bells,
  shelves, tilt).
- **Design** ([EqDesign.h](../../engine/src/builtin/EqDesign.h), `eq::design`): each band
  is a cascade of analog sections: a bell, notch or band pass is one second-order
  section; a cut or shelf of slope `6n` dB/octave is of order `n` (one first-order
  section if odd, then pairs) with Butterworth Qs, the most resonant times `Q / 0.7071`;
  a shelf's gain is shared out over its sections; a tilt shelf is a high shelf down
  half its gain. Each section is made digital by matching (Vicanek, 2016): poles by
  impulse invariance (their frequency kept below 0.97 of Nyquist), the numerator so
  that the magnitude is the analog one's at 0 Hz, Nyquist and the section's frequency.
  Where no real numerator has all three (a resonant cut, a notch high up), Nyquist's
  gives way. So bells and shelves high up keep their analog shape, not the bilinear
  transform's squeezed one. The cuts' sections are the exception: the bilinear transform,
  pre-warped at the band's frequency (`eq::bilinear`), since matched poles overshoot as a
  cut's corner nears Nyquist (a Butterworth high cut at 22 kHz of 48 kHz rose 2 dB above its
  passband); so a cut stays maximally flat, -3 dB at its corner, its resonance as high at any
  frequency, and a high cut falls to nothing at Nyquist.
- **Processing**: per 32 samples, each band glides its frequency and Q (in log) and gain
  towards its parameters (a one-pole, about 15 ms) and is designed again while they
  move; a band switched on starts from its parameters, silent. Sections run in double,
  transposed direct form II; their states are flushed below 1e-20. Placement Mid or
  Side converts the chunk to mid and side and back around the band; on one channel,
  every band but Side's applies. The output gain is ramped over 20 ms.
- `tailSamples()`: 7 Q / (π f) for the narrowest band (at most 5 s).
- **Displays**: `input` and `output`, one value per sample, each summed to mono: the
  editor's analyzer.
- **Response**: `eq::responseDb(design, frequency, sampleRate)` gives a band's
  response in dB at a frequency, from the same `eq::design()`. The application layer's
  `eqResponseDb(type, freq, gain, q, slope, sampleRate, freqs)`
  ([app/src/audio/EqResponse.h](../../app/src/audio/EqResponse.h)) wraps it for the
  editor's curves, so the curve drawn is the sound.

### Sidechain (`builtin:sidechain`, AudioEffect, with a sidechain input)

Ducks its input along a curve from each hit: for a bass under a kick.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `trigger` | Trigger | | Sidechain, Every Bar, Every 1/2, Every 1/4, Every 1/8, Every 1/16 | Sidechain |
| `threshold` | Threshold | dB | -60..0 | -24 |
| `depth` | Depth | % | 0..100 | 100 |
| `sync` | Sync | | Off, On | Off |
| `length` | Length | ms | 10..2000, log | 250 |
| `rate` | Length (Synced) | | 1/32, 1/16, 1/8, 3/16, 1/4, 3/8, 1/2, 1 Bar | 1/4 |
| `smooth` | Smooth | ms | 0..30 | 1 |
| `lookahead` | Lookahead | ms | 0..20 (not automatable: it is the latency) | 0 |
| `range` | Range | | Full, Lows | Full |
| `crossover` | Crossover | Hz | 30..1000, log | 150 |
| `autofit` | Auto Fit | | Off, On (hidden, not automatable; the editor's) | Off |
| `character` | Fit Character | | Tight, Natural, Loose (hidden, not automatable; the editor's) | Natural |
| `p1_used` .. `p16_used` | Point n Used | | Off, On (hidden, not automatable, as are the three below) | On for 1..3 |
| `pn_x` | Point n Time | | 0..1 of the length | 0, 0.1, 1 |
| `pn_y` | Point n Level | | 0 (ducked by the depth)..1 (untouched) | 0, 0, 1 |
| `pn_curve` | Point n Curve | | -1..1: how the segment after it bends | 0, -0.45, 0 |

- **Hits**: with `trigger` Sidechain and a sidechain chosen, the first sample whose
  key (the louder channel) is at the threshold, once re-armed: the key's level (falling
  over 10 ms) has dropped 3 dB below the threshold, and 20 ms have passed. Without a
  sidechain, none (a ducker keyed by its own input would only duck its own notes). On
  the beat: at every multiple of the interval (a bar from the time signature) while
  playing, sample-accurately; a beat that rounds to both sides of a stretch's start
  hits once.
- **The curve**: the used points sorted by x; at `t` samples after a hit, x = t /
  length (in ms, or `rate` at the tempo with `sync`), evaluated as automation is
  (`automationShape`, a point's curve bending the segment after it); before the first
  point its y, after the last (and until the next hit, and before any) the last's.
  The gain is `1 - depth (1 - y)`, smoothed by a one-pole of `smooth` ms (none at 0).
- **Lookahead** delays the input (a delay line for 20 ms at most); `latencySamples()`
  reports it and `idle()` asks for the tracks to be realigned when it changes. The key
  isn't delayed, so the curve starts that much before the kick.
- **Range** Lows: the delayed input is split by a Linkwitz-Riley crossover (two
  Butterworth sections per band, in double), and only the low band is ducked
  (`high + gain * low`); the bands add up to the input's magnitude, flat.
- **Displays**: `key` (the sidechain summed to mono), `input` (the input before the
  lookahead, summed to mono) and `phase` (samples since the latest hit; -1 once its curve
  is over), one value per sample each, pushed together so they stay in step. The
  editor finds the hits (the phase's 0s) and fits the curve to the kick
  ([app/src/analysis/SidechainFit.h](../../app/src/analysis/SidechainFit.h)).

### Transient Shaper (`builtin:transient`, AudioEffect)

Turns a sound's onsets (Attack) and its tails (Sustain) up or down, whatever its level, by following its envelope
([TransientDesign.h](../../engine/src/builtin/TransientDesign.h)).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `attack` | Attack | % | -100..100 | 0 |
| `sustain` | Sustain | % | -100..100 | 0 |
| `attack_time` | Attack Time | ms | 1..100, log | 20 |
| `release` | Release | ms | 20..2000, log | 300 |
| `threshold` | Threshold | dB | -80..0 | -60 |
| `output` | Output | dB | -24..24 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 100 |
| `clip` | Clip | | Off, On | Off |
| `lookahead` | Lookahead | | 0 ms, 1 ms, 5 ms (not automatable: it is the latency) | 0 ms |

- **The envelope** is the magnitude of the input's analytic signal, each channel's (`transient::Hilbert`: two chains
  of four second-order all-passes 90° apart, Olli Niemitalo's design, each coefficient squared), the louder
  channel's for both (linked stereo). On a steady tone from 40 Hz to 20 kHz (at 48 kHz) it holds within 0.06 dB,
  where a rectified tone falls to zero twice a cycle: a bass note doesn't make the gain chatter.
- **Three followers** ride on it (`Detector`, `dsp::EnvelopeFollower`s): *fast* (rising over 0.15 ms, falling over
  20 ms), *slow* (rising over Attack Time, falling as the fast one) and *held* (rising as the fast one, falling over
  Release). Fast over slow, in dB, is the transient (≥ 0: only while the level rises); held over fast the sustain
  (≥ 0: only while it falls faster than Release).
- **The gain** in dB is Attack / 100 times the transient plus Sustain / 100 times the sustain, times the
  threshold's weight (an S-curve from 0, 12 dB under the threshold, to 1 at it, by the held level), held to
  -30..+24 dB (`gain`, which scales the two parts together so they still add up to it). So Attack +100 % doubles an
  onset's rise in dB and -100 % slows it to Attack Time's; Sustain +100 % lets a tail fall only as fast as Release,
  -100 % twice as fast as it does.
- **The output** is `(dry + mix (dry · gain - dry)) · output`, then with Clip a smooth bend above -6 dBFS towards
  full scale (`softClip`: a tanh, the line's slope at its knee; not oversampled). At its defaults the device passes
  its input bit for bit.
- **Lookahead** delays the audio (0, 1 or 5 ms) while the detector hears it as it comes, so the gain moves that much
  before an onset reaches the output; it is the latency (`idle()` reports a change), and a change crossfades from the
  old delay to the new over 10 ms (one during a crossfade starts when it is done).
- **Nothing jumps**: Attack, Sustain, Threshold, Output and Dry/Wet ramp over 20 ms, Clip fades over 10 ms; the
  times only change how fast the followers move. Automation is sample-accurate (the output is the same, bit for bit,
  whatever the block size).
- **Displays**, one value per 128 samples, pushed together: `input` and `output` (dB, the envelope's most over the
  stretch: smooth on a low tone, where sample peaks would beat against its cycles; `input` as it reaches the gain,
  after the lookahead, `output` that times the gain, Clip bending it), `attack` and `sustain` (dB, the gain's parts, the one furthest from 0 of each stretch)
  and `detect` (dB, the transient after the threshold's weight, the most of each stretch: what the editor's sparks and
  playheads go off on).
- **The editor's hit**: `hitResponse()` runs `hitEnvelope()` (1 ms up to -6 dBFS, then 60 dB down over 450 ms)
  through the same `Detector` and `gain` at 24 kHz, so the hit drawn is shaped as the device shapes one (the
  application layer's `transientHit()` wraps it).
- Its envelopes, followers and gain on two channels took about 0.5 % of one core at 48 kHz on the machine it was
  written on.

### Vocoder (`builtin:vocoder`, AudioEffect, with a sidechain)

Ableton's Vocoder: the input (the modulator) split into bands, each band's level shaping the same band of a carrier
([VocoderDesign.h](../../engine/src/builtin/VocoderDesign.h)).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `carrier` | Carrier | | Noise, External, Modulator, Pitch Tracking | Noise |
| `noise_rate` | Rate | Hz | 100..20000, log | 20000 |
| `crackle` | Crackle | % | 0..100 | 100 |
| `pitch` | Pitch | st | -24..24 | 0 |
| `wave` | Wave | | Saw, Pulse 50%, Pulse 25%, Pulse 12.5% | Saw |
| `track_low` | Low | Hz | 30..1000, log | 80 |
| `track_high` | High | Hz | 100..2000, log | 1000 |
| `enhance` | Enhance | | Off, On | Off |
| `unvoiced` | Unvoiced | % | 0..100 | 0 |
| `sensitivity` | Sens. | % | 0..100 | 50 |
| `speed` | Speed | | Fast, Slow | Fast |
| `bands` | Bands | | 4, 8, .. 40 | 20 |
| `low` | Low | Hz | 20..18000, log | 80 |
| `high` | High | Hz | 20..18000, log | 12000 |
| `bandwidth` | BW | % | 10..200 | 100 |
| `filter` | Filter | | Precise, Retro | Precise |
| `formant` | Formant | st | -12..12 | 0 |
| `gate` | Gate | dB | -96..0 (-96: none) | -96 |
| `depth` | Depth | % | 0..200 | 100 |
| `attack` | Attack | ms | 0.1..100, log | 5 |
| `release` | Release | ms | 1..3000, log | 150 |
| `channels` | Channels | | Mono, Stereo, L/R | Stereo |
| `level` | Level | dB | -24..24 | 0 |
| `mix` | Dry/Wet | % | 0..100 | 100 |

- **The bands** (`vocoder::layout`): Bands centres evenly in log frequency from Low to High, each as wide as the
  spacing times BW. Precise: two cookbook band-passes in a row at 0 dB, each wider by 1/0.6436 so the pair meets its
  neighbours at -3 dB; Retro: one section, narrower (1.4 to 0.6 of the width) and louder (0 to +9 dB) up the bank.
  Widths are prewarped, so a band near Nyquist is as wide as one low down. The carrier's sections are the
  modulator's moved by Formant; a band whose centre, or whose carrier's, is past 0.45 of the rate is left out.
- **The modulator's levels**: each band's mean square followed with Attack (rising) and Release (falling). Depth
  scales each band's distance in dB from the bands' mean, then all are scaled back to the modulator's power: 0 % is
  the carrier's spectrum, 200 % the modulator's peaks further out, at the same level. Gate silences a band under it
  (its gain smoothed over 2 ms). The gains (and Enhance's divisors) are worked out every 8 samples and ramped
  linearly between: the levels move far slower than that, and the logarithms per band would cost more than the
  filters. The carrier's levels are brought up to date with each sample's bands before they scale them, so a
  modulator or carrier starting after silence is never scaled by the silence's level.
- **The carrier**: Noise is xorshift noise held at Rate (a fractional sample-and-hold clock) and, under 100 % Crackle,
  only a sample in 1/Crackle² of them, as loud (so the power stays); External the sidechain (silence without one);
  Modulator the input; Pitch Tracking a PolyBLEP saw or pulse at the tracked pitch times 2^(Pitch/12), gliding over
  5 ms. The tracker (`vocoder::PitchTracker`) is YIN on the input brought to about 12 kHz (two low-pass sections,
  every n-th sample): a 25 ms window every 5 ms, the first dip of the cumulative mean normalised difference under
  0.2 between High's and Low's periods, refined by a parabola; an unvoiced frame keeps the last pitch.
- **Bringing the carrier to a steady level**: with Enhance each carrier band is divided by its own RMS (a 20 ms mean
  square); without, all by the carrier's power over its bands weighted by the modulator's gains (100 ms), so a
  carrier dark where the voice is dark isn't taken for louder than it is there. Either level is held at least at a
  tenth of its peaks (held as long): a carrier starting from silence is raised 10 dB at the most while its mean
  catches up. Enhance raises a band 60 dB over the carrier's level at the most.
- **The bank's sum**: neighbouring Precise bands meet about 130° apart in phase, so every other band's polarity is
  turned (they meet about 50° apart: they add) and the gains are divided by the bank's mean power gain across its
  range, worked out when the bands change: the output is within a couple of dB of the input's level whatever the
  carrier, Depth or band count.
- **Unvoiced**: the share of the summed modulator's power above 4 kHz (a two-section high-pass), followed over 3 ms
  (Fast) or 30 ms (Slow); over 0.6 × (1 - Sens.) it counts, fully 0.25 above that. White noise high-passed at 3 kHz,
  as loud as the modulator's highs times that and Unvoiced, is added to each carrier channel's output.
- **Channels**: Mono, one modulator and one carrier (each summed), the output on both sides; Stereo, the modulator
  summed, a carrier each side (Noise: one each); L/R, a modulator and carrier each side. One channel in: Mono.
- **Output**: `dry (1 - mix) + wet · Level · mix` (Level and Dry/Wet ramp over 20 ms).
- **Its cost**, at 48 kHz on the machine it was written on: 1.5 % of a core at its defaults (20 bands, Stereo,
  Noise); 7 % at the most (40 bands, L/R, Pitch Tracking, Enhance, Depth off 100 %).
- **Displays**, every 1024 samples: `bands` (64 values a time, the first channel's modulator band levels in dB from the
  lowest, the rest -96: the display's 8192 values hold whole frames), `pitch` (the pitch the oscillator plays, Hz; 0
  when not tracking or unvoiced) and `output` (the output's peak in dB).

### Vocal Rider (`builtin:vocal_rider`, AudioEffect, with a sidechain)

A slow gain that rides a vocal to a target level, as an engineer riding a fader (Waves' Vocal Rider's idea).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `target` | Target | dB | -50..0 | -18 |
| `min_gain` | Range Min | dB | -24..0 | -12 |
| `max_gain` | Range Max | dB | 0..24 | 12 |
| `speed` | Speed | ms | 20..2000, log | 250 |
| `sensitivity` | Vocal Sens. | dB | -80..0 | -50 |
| `music_sens` | Music Sens. | % | 0..100 | 0 |
| `weighting` | Weighting | | Vocal, Flat | Vocal |
| `output` | Output | dB | -24..24 | 0 |

- **Hearing the vocal**: the channels summed, with Vocal through BS.1770's K-weighting shelf (+4 dB from 1.68 kHz)
  and a 100 Hz high-pass (for the room's rumble, in place of its 38 Hz one). Its level is its mean square over
  0.4 × Speed (10 ms at the least), in dB.
- **Present or not**: a 10 ms mean square of the same signal over Vocal Sens. makes the vocal present, under it by
  6 dB absent. A phrase starting seeds the slow level from the 10 ms one, so a phrase after a pause isn't taken for
  quieter than it is (and ridden up at its start).
- **The ride**: while the vocal is present the gain (dB) follows `target - level`, held to Range Min..Range Max, with
  Speed as its time constant; while absent it holds (breaths, bleed, a phrase's tail aren't brought up). A range
  narrowed while holding holds the gain inside it.
- **Music Sens.**: with a sidechain, the music's level (the same weighting, 400 ms) moves the target to
  `target + Music Sens. × (music - target)` while the music is over -60 dB.
- **Output** ramps over 20 ms; the gain is applied to every channel alike.
- **Displays**, one value per 256 samples: `level` (the vocal's level, dB), `gain` (the ride, dB), `target` (the
  target Music Sens. makes) and `music` (the music's level; -96 without).

### Changeling (`builtin:changeling`, AudioEffect, with a MIDI input)

A pitch and formant shifter for a single voice: Soundtoys' Little AlterBoy's controls, with Polyverse Manipulator's on
its second page. Both were rendered headless through the engine's own hosting
([benchmarks/plugin_render.cpp](../../benchmarks/plugin_render.cpp)) on synthetic probes (pulse trains, glottal
vowels through formant filters, glides, steps and vibrato, MIDI notes, noise, a sawtooth with a 3 kHz probe whose
envelope traces the grains' windows) and on dry vocal phrases; what each control does here is what was measured
there, with the differences below. The maths is [builtin/ChangelingDesign.h](../../engine/src/builtin/ChangelingDesign.h).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `mode` | Mode | | Transpose, Quantize, Robot | Transpose |
| `pitch` | Pitch | st | -24..24 (shown to a hundredth) | 0 |
| `formant` | Formant | st | -24..24 (shown to a hundredth) | 0 |
| `link` | Link | | Off, On | Off |
| `glide` | Glide | ms | 1..1000, log | 20 |
| `key` | Key | | Song, C..B | Song |
| `scale` | Scale | | GigaTune's | Chromatic |
| `drive` | Drive | dial | 0..10 | 0 |
| `mix` | Mix | % | 0..100 | 100 |
| `output` | Output | dB | -24..24 | 0 |
| `harmonics` | Harmonics | | -4..4 (shown to a hundredth) | 0 |
| `twin` | Twin | % | 0..100 | 0 |
| `interval` | Interval | st | -24..24, whole | -12 |
| `detune` | Detune | ct | 0..50 | 0 |
| `width` | Width | % | 0..100 | 0 |
| `smear` | Smear | % | 0..100 | 0 |
| `quality` | Quality | | Live, Studio, High Quality (not automatable) | Studio |

- **The shifter** is GigaTune's corrector's TD-PSOLA (analysis marks a period apart, the first voiced one moved onto
  its pulse and the grains' centres following it, synthesis marks an output period apart, grains spanning their
  neighbours' centres, the windows' sum divided out past 1, grains raised where they fall short of their step,
  sqrt(ratio) for a flat spectrum's power) in four voices: each channel's main voice and its twin, sharing the
  analysis marks, each with its synthesis marks and grains. A ratio of 1 is the input, bit for bit. The raise for a
  grain read faster than it plays is held to 2 (a short burst each period, at Formant +24 on a low voice, would come out
  spiky). Measured: Little AlterBoy's grains are flat-topped, about 1.8 periods long and end just after their pulse
  (each carries the pulse before it: an octave down has a ghost of the original pitch), with ±13 % ripple even at 0;
  Manipulator's are classic two-period Hann grains on the pulses, unnormalised (+3 dB an octave up). Both leave
  unvoiced noise unshifted, as Changeling does.
- **Modes** (LAB's, measured): Transpose adds Pitch; Quantize snaps the pitch heard to the nearest of Key and Scale's
  notes (`gigatune::targetNote()`, its hysteresis) and adds Pitch, the vibrato gone; Robot holds `kRobotNote` (C4)
  plus Pitch. The note held glides (two one-poles, `dsp::Glide`, 10 to 90 % in Glide's time), and so does the
  transposition; in Quantize a note starting snaps. The output pitch is held within `kLowestOut`..`kHighestOut` and
  `kMaxShift` of the voice.
- **MIDI**, as LAB takes it: the last note-on is held (note-offs change nothing); Robot sings it plus Pitch; Transpose and
  Quantize transpose by its distance from C4. Notes are kept with their input time and a frame takes the note held at
  its own time, so a note acts where it was played once the engine has delayed the other tracks (LAB applies notes at
  its output: 50 ms early). Manipulator's MIDI sets the pitch an octave below the note's number.
- **Formant** moves each voiced grain's read speed (2^(Formant/12)); **Link** adds the frame's shift (out - heard) to it,
  so the envelope moves with the pitch (measured: LAB's Link ignores its Formant knob; here it adds). Read speeds are
  held within ±`kMaxRead`. LAB's Formant is ±12, Manipulator's ±24.
- **Harmonics** (Manipulator's harmonic shifter, measured: its partials stay harmonic, the lowest k vanish, k =
  8 (knob - 0.5), and its Ratio scales k): each voiced grain reads the input's analytic signal too (`Quadrature`,
  kept beside the input) and rotates it by e^(2πi h u / T), u output samples from the grain's centre and T the local
  output period, so its spectrum moves h harmonics while the grains still repeat at T. Below |h| = 0.5 it is mixed
  with the grain as it was (no step at 0).
- **Twin** (made from Manipulator's grain alternator, whose Octave 0 adds a voice that many octaves down read as a tape
  would): the twins' offset is Interval (and their side's detune) and their read speed moves by Interval as well; Twin
  is their level, the four voices kept as loud as the main ones (1 / sqrt(1 + Twin^2)); unvoiced grains aren't played by
  the twins. A twin starts from its main voice's committed marks and grain when Twin comes up, and stops once its last
  grain ends after Twin goes to 0: only running voices commit the shared marks, so with Twin at 0 a ratio of 1 stays
  bit-exact.
- **Detune**: the left voices `-Detune` cents, the right ones `+Detune`. **Width**: the left voices' odd grains and the
  right's even ones scaled by `1 - Width` (their windows still count in the sum, so the gaps stay), each side raised by
  sqrt(2 / (1 + (1 - Width)^2)). **Smear**: a voiced grain taken from up to `kSmearSeconds` before its analysis mark,
  at random, and each voiced synthesis step moved by up to `kSmearJitter` of itself (in proportion, up or down alike).
- **Quality**: Live, the tracker as GigaTune's Low Latency (21.3 ms at any rate); Studio, the same tracker deciding
  each frame `kStudioLag` frames later (48.6 ms at 48 kHz: LAB's is 51), so a note's start is decided voiced from its
  first periods; High Quality, SwiftF0 (about 316 ms here: a grain may reach further back, read at a quarter of the
  speed). Both pYIN trackers rescue short notes the path leaves unvoiced (`PitchTracker::setRescue()`). Measured on
  dry phrases, the share of each note's first 80 ms that comes out at the shifted pitch: Live 0.64-0.69, Studio
  0.67-0.78, High Quality 0.68-0.81, Little AlterBoy 0.58-0.81, Manipulator 0.55-0.63.
- **Drive** (`changeling::Drive`), LAB's, fitted from noise through it (which LAB passes without grains): LAB's colour
  (a first-order high-pass at 22 Hz, a high shelf +1.6 dB from 5.3 kHz, a low-pass at 22 kHz), then `y = A (s(B c x +
  b) - s(b))` with `s(u) = u / (1 + |u|^h)^(1/h)` and A, B, b, h fitted at each of 0..10 (interpolated between), A
  divided by LAB's gain at 0 (0.759); c = 1 / (1 + κ max(0, rms - 0.07)) past 6 (κ = 5 ((Drive - 6) / 4)^1.5, the RMS
  followed with 30 ms attack, 200 ms release), at twice the rate, a DC blocker after. Within 7 to 16 % (RMS) of LAB's
  output at every setting and level. From 0 to 1 it fades in from the input; at 0 it is a plain delay of the
  oversampling's 31 samples, the oversampler and the colour kept running so turning it up starts with their history.
- **Mix** mixes the dry input, delayed by the whole latency, with the wet (smoothed over 10 ms); **Output** after it.
  The latency is the Quality's plus the drive's.
- **Displays**, one value per 256 samples out: `pitch` (heard, MIDI; 0 unvoiced), `out` (the main voices' output pitch)
  and `note` (the note Quantize or Robot holds; -1 none).
- Costs (builtin_devices_bench, a core): 2.5 % at its defaults, 2.7 % shifting, 3.4 % with Drive, 3.8 % with the twin.

### LFO (`builtin:lfo`, Modulator)

A drawn curve played over and over; its value moves the parameters mapped to it (a modulator:
see [automation.md](automation.md#modulation)). It never touches the audio.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `rate` | Rate | Hz | 0.01..40, log | 1 |
| `sync` | Sync | | Off, On | On |
| `synced` | Rate (Synced) | | 1/64 .. 8 Bars (DspBlocks.h's `syncedDivisionLabels`) | 1/4 |
| `depth` | Depth | % | 0..100 | 100 |
| `offset` | Offset | % | -100..100 | 0 |
| `phase` | Phase | ° | 0..360 | 0 |
| `retrigger` | Retrigger | | Off, On | Off |
| `smooth` | Smooth | ms | 0..250 | 0 |
| `p1_used` .. `p32_curve` | Point n Used, Time, Level, Curve | | as the Sidechain's (hidden, not automatable) | a triangle (`modulator::trianglePoints`: 0, up to 1 at half the cycle, down to 0) |
| `random` | Random | | Off, On | Off |
| `follow_groove` | Follow Groove | | Off, On | Off |

- **The cycle**: 1 / Rate seconds, or the synced note value at the tempo. Synced while the song
  plays (and not retriggered) its position is the song's: frac(beat / cycle beats), exactly, so a
  1/4 LFO starts its cycle on every beat wherever playback starts or loops; stopped, or
  retriggered, it runs on from where it is at the tempo; free in Hz it always runs on (from 0
  after a reset, so offline renders repeat). **Retrigger**: a note-on (`startsNote()`) sets it to
  the cycle's start.
- **Follow Groove**: synced and following the song, with the project's groove (`ctx.groove`) and
  the tempo map, its position is that of the straight beat the groove puts where the song is:
  found back from `groove::place()` (key 60, the default `Amounts`; the warp only rises) by
  bisection every 32 samples (`kGrooveStep`), straight between. So a cycle point due at a beat
  comes where the groove moves that beat, and a 1/16 LFO swings with the hats. Without a project
  groove, as it is.
- **Its value**: the curve (`modulator::Curve`: automation's shapes, `kPoints` = 32 points) at
  the cycle's position plus Phase / 360 (wrapped), then 0.5 + (y − 0.5) · Depth + Offset / 2, held
  to 0..1, smoothed by a one-pole of Smooth ms (none at 0). Written into every frame of its first
  two channels (the renderer hands it buffers of its own: `Renderer::runModulators`).
- **Displays**, one value per 64 samples (`kDisplaySamples`), pushed together: `value` (its
  value), `phase` (the curve's x there) and `key` (1 where a note restarted it).

### Envelope (`builtin:envelope`, Modulator, with a sidechain input)

A drawn curve played once from each trigger, its value moving the parameters mapped to it.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `threshold` | Threshold | dB | -60..0 | -24 |
| `sync` | Sync | | Off, On | Off |
| `length` | Length | ms | 1..20000, log | 500 |
| `synced` | Length (Synced) | | 1/64 .. 8 Bars | 1/2 |
| `depth` | Depth | % | 0..100 | 100 |
| `velocity` | Velocity | % | 0..100 | 0 |
| `smooth` | Smooth | ms | 0..250 | 0 |
| `p1_used` .. `p32_curve` | Point n Used, Time, Level, Curve | | as the LFO's | a pluck (`modulator::pluckPoints`) |

- **Triggers**: what it is given decides, with no setting of its own: a note-on it hears (its
  track's notes, or another track's: `acceptsMidi()`), and once a sidechain is chosen
  (`sidechainConnected()`) a hit in the key, found as the Sidechain device finds one (the louder channel at
  the threshold, once re-armed: 3 dB below it, falling over 10 ms, and 20 ms on). Without a
  sidechain chosen, no hits from audio. A trigger while it plays starts it again.
- **Its value**: from a trigger, the curve at t / length over its length, then (and before any
  trigger) its last point's; times Depth, times 1 − Velocity · (1 − the note's velocity / 127) (a
  hit: full), held to 0..1, smoothed by Smooth; written into its first two channels, as the LFO's.
- **Displays**, one value per 64 samples, pushed together: `value`, `phase` (where along the curve
  it is, 0..1; -1 at rest) and `key` (the key's peak over the stretch; without a sidechain, 1
  where a note came).

### Racks

A rack is a `RackProcessor` ([Rack.h](../../engine/src/Rack.h), `typeId()` `"rack"`): only its place in a chain, its
on/off switch, its id and its latency (as the last snapshot worked it out, for the UI). It has no parameters, and its
`process()` is never called: the renderer runs its chains (`Renderer::processRack`). A Drum Rack is a rack whose
chains have keys (`Engine::setChainReceiveNote`): each hears only its key's notes, as C3. See
[routing.md](routing.md#racks).

## Built-in devices' own editors

A built-in device shows a knob per parameter (the device view's knob pages: a list for parameters with labels, log
knobs for log parameters) unless it has an editor of its own: a QML file in
[ui/qml/devices/editors/](../../ui/qml/devices/editors), named for the device's kind (its engine id) in the
`DeviceEditors` singleton's table ([DeviceEditors.qml](../../ui/qml/devices/editors/DeviceEditors.qml)):

```js
readonly property var editors: ({
    "amp": "AmpEditor.qml",
    "arpeggiator": "ArpeggiatorEditor.qml",
    "auto_filter": "AutoFilterEditor.qml",
    "bitcrusher": "BitcrusherEditor.qml",
    "chorus": "ChorusEditor.qml",
    "compressor": "CompressorEditor.qml",
    "delay": "DelayEditor.qml",
    "disperser": "DisperserEditor.qml",
    "envelope": "EnvelopeEditor.qml",
    "eq": "EqEditor.qml",
    "erosion": "ErosionEditor.qml",
    "gate": "GateEditor.qml",
    "gigatune": "GigaTuneEditor.qml",
    "groove": "GrooveEditor.qml",
    "glitch": "GlitchEditor.qml",
    "hue": "HueEditor.qml",
    "lfo": "LfoEditor.qml",
    "limiter": "LimiterEditor.qml",
    "multiband": "MultibandEditor.qml",
    "overdrive": "OverdriveEditor.qml",
    "phaser": "PhaserEditor.qml",
    "reverb": "ReverbEditor.qml",
    "sampler": "SamplerEditor.qml",
    "saturator": "SaturatorEditor.qml",
    "sidechain": "SidechainEditor.qml",
    "spectral": "SpectralEditor.qml",
    "supersaw": "SupersawEditor.qml",
    "tape_stop": "TapeStopEditor.qml",
    "transient": "TransientEditor.qml",
    "vintage_delay": "DelayEditor.qml",
    "vintage_hall": "VintageHallEditor.qml",
    "vital": "VitalEditor.qml",
    "vocal_rider": "VocalRiderEditor.qml",
    "vocoder": "VocoderEditor.qml"
})
```

- `DeviceEditors.editorFor(kind)` gives the editor's URL, or `""` for the knob pages.
- An editor sets parameters as the knobs do (through the application layer's `ProjectEditor::setDeviceParams()`), so
  undo, automation and saving work alike.
- It draws with [DeviceCanvas](../../ui/src/devices/DeviceCanvas.h) items: `refreshDisplays()` is called as the meters
  update (while the item is visible), and `readDisplay(id)` returns the values the device published since the last
  call (through `EngineBridge::readProcessorDisplay()`; the item keeps the position per processor and display).
- Today there are thirty-three. The LFO and the Envelope draw their curve to edit (`ModulatorGraph`), the
  playhead riding it from `phase`, what they put out (`value`) and, for the Envelope, the key against its
  threshold, and list what they move (`ModulatorTargets`); see [ui/device-view.md](../ui/device-view.md#modulators).
  The Compressor draws its gain reduction over the last 240 display values (about 1.3 s at
  48 kHz) and In/Out meters with the threshold marked; the Gate the last 2.5 s of `input`, `output`, `key` and `open`
  with its threshold and Return lines to drag, and in its sidechain section the key EQ's curve (from `gate::keyFilter`,
  the engine's own biquad); the Limiter the level over the last 1.5 s with the gain reduction hanging from the top,
  In, GR and Out meters, and the Ceiling (or with Maximize the Threshold) as a line to drag, Soft Clip's band under
  it (from `limiter::scales` and its knee); Multiband Dynamics a lane per band, its Above and Below regions with their
  thresholds and ratios to drag, and each band's level before and after its dynamics (`<band>_in`, `_out`, `_gain`;
  the static curve from `multiband::staticGainDb`); the Spectral Compressor the spectra of `input` and `output` with
  the threshold and Below lines (from `spectral::thresholdDb` and `belowDb`) and the Focus band to drag, the cut or
  lift at each frequency (`gain`) and where the levels compared (`key`) are over the threshold; the Saturator its
  curve (from `saturator::transfer`) lit by `in_peak`, and Color's EQ (from `saturator::colorResponseDb`) over spectra
  of `input` and `output`; the Amp its transfer and tone curves (from `amp::Transfer` and `amp::toneResponseDb`),
  its tubes glowing as hard as each stage is driven (`drive1`..`drive3`, `power`, `sag`), a pilot lamp and an output
  meter; Erosion its noise band (from `erosion::bandMagnitude`) on an X-Y field of Frequency and Amount over spectra
  of `input` and `output`, shimmering with `erosion`, and a scope of `mod_l` against `mod_r`; the Delay its filter
  over a spectrum of `input`, laid out as Ableton's with its Character (Swing, Drive, Duck) where Ableton's has its
  modulation; the Overdrive its band (from `overdrive::bandResponseDb`) with Tone dashed over it, over a spectrum of `input`, the band glowing with `output` and the Drive knob warming with `drive`; the Auto Filter its response (from `autofilter::responseDb`) at Frequency as it is now moved by the sweep the engine publishes (`sweep_l`, `sweep_r`, played back by a `DisplayPlayback`, so an edit is drawn at once) over a spectrum of `input`, glowing with `env`, the dot's halo breathing with `lfo`, and the modulation's reach along the bottom, laid out as Live 12.2's (the LFO's strip along the display's bottom, the envelope's Attack and Release in the Sidechain fold); the Bitcrusher a cycle of a sine crushed as its parameters say (worked out in the QML), running along while
  `level` comes; the Arpeggiator sixteen steps of its pattern for a three-note chord (the engine's `indexFor`,
  mirrored in the QML), the step `step` says lit; the Groove device the groove it plays (its source chosen above
  it: Swing, Project, the project's grooves) as its default lane's steps over its cycle, each where it plays and as
  tall as it is loud (`GrooveSteps`), the step `step` says lit, with Add to Grooves and Apply
  ([groove.md](../groove.md)); the Chorus-Ensemble each voice's delay as it moves (from `ChorusDesign.h`'s maths at
  the LFO's `phase`, glowing with `level`), dragged for the Rate and the Amount; the Phaser-Flanger its response (from
  `phaser::curve`) at its parameters as they are moved by the modulation the engine publishes (`mod`, `mod_r`), its notches marked, and
  the LFO's shape and phase; the Reverb its input filter's band over a spectrum of `signal`, its early reflections as
  particles Spin swings (`early`, `spin`), and its decay time per frequency (from `reverb::decaySeconds`) over the
  tail's spectrum (`tail`, `diffuse`), each with handles to drag; the Disperser its group delay in ms on a fixed log
  axis (from `disperser::groupDelayMs`, the engine's own stages); the Transient Shaper a hit before and after its
  shaping (from `transient::hitResponse`, the engine's own detector and gain) with handles for Attack, Sustain and
  their times and a playhead along it at each transient (`detect`), and the last 2 s of `input` and `output` glowing
  where `attack` and `sustain` shaped them, the gain in a lane under them, sparks at each transient; the Vintage Hall its loop as a ring (from `vintage_hall::ringMap`, every part where Size puts it), glowing
  with each tap's level (`l1`..`r4`) and wavering with its moving delays (`mod_b`, `mod_d`), a comet from each
  input for each onset (`input`) running round at the loop's speed and fading as Decay says, its taps weighted by
  Attack, and a bloom as loud as `output`; the EQ its bands' curves (from `eq::responseDb`)
  over an analyzer of `input` and `output`; the Vocoder its filter bank (from `vocoder::bandResponseDb`), each band
  a bar as loud as `bands` says, Formant's carrier bands marked, and the pitch tracked (`pitch`); the Vocal Rider the
  last 5 s of `level`, the output (`level` + `gain`) and `music`, the target as a line to drag (moved by `target`
  while Music Sens. moves it), and the ride in a lane under them; the Sidechain its curve, its
  playhead from `phase` and its fit to the kick (`key`, against `input`); the Sampler the sample's waveform as it plays
  with its markers, its loop, fades or slices (the engine's own, through the application layer's `sampleSlices`) and
  the playhead from `position` (drop or double-click to load a sample; loading is an undoable state change through
  `ProjectEditor::setDeviceState()`, the path kept in the device's state); Vital nine pages of its parameters and its
  modulation matrix; the Supersaw its three stacks, a lane each, every saw where the engine's layout puts it (from
  `supersaw::layout`, through the application layer's `supersawLayout`), glowing and shimmering at its beat with the
  `output` (see [ui/device-view.md](../ui/device-view.md#device-editors)).

The Arpeggiator's, the Delay's and the LFO's editors have a **Groove** button: Follow Groove.

See [ui/device-view.md](../ui/device-view.md#device-editors) for the editors and how to add one.

## Adding a built-in device, end to end

1. **Write the device**: `engine/src/builtin/devices/MyDevice.cpp`.

   ```cpp
   #include "builtin/BuiltinProcessor.h"
   #include "builtin/BuiltinRegistry.h"
   #include "rt/RtUtils.h"

   namespace sub {
   namespace {

   class MyProcessor final : public BuiltinProcessor {
   public:
       enum Param { Amount = 0, NumParams };
       MyProcessor() : BuiltinProcessor(infos()) {}   // + displays, if its editor draws any
       std::string typeId() const override { return "builtin:mydevice"; }
       std::string name() const override { return "My Device"; }
       void prepare(double sampleRate, int maxBlockSize) override { /* allocate, set up smoothing */ }
       void reset() override { /* clear filter state, voices */ }

   protected:
       void render(const ProcessContext& ctx, float* const* ch, int numChannels, int numFrames) override {
           const float amount = param(Amount);   // plain value, already automated for this stretch
           // process in place; no locks, no allocation
       }

   private:
       static const std::vector<ParamInfo>& infos() {
           static const std::vector<ParamInfo> kInfos = {
               {"amount", "Amount", "%", 0.f, 100.f, 50.f},
           };
           return kInfos;
       }
   };

   }  // namespace
   SUB_REGISTER_BUILTIN(MyProcessor, AudioEffect);
   }  // namespace sub
   ```

   - Parameter ids are saved in projects and automation keys (`device:<device id>:<param id>`): don't rename them
     once released. Use `logScale` for frequencies and times, `valueLabels` for lists, `steps` for whole numbers.
   - What other devices already do is in [builtin/Dsp.h](../../engine/src/builtin/Dsp.h) (filter sections,
     interpolation, envelopes, rendering between note events),
     [builtin/DspBlocks.h](../../engine/src/builtin/DspBlocks.h) (one-pole and DC filters, a delay line, an envelope
     follower, noise, a fast tanh, LFOs and synced rates, biquads, a crossover, a sliding maximum, oversampling) and
     [rt/RtUtils.h](../../engine/src/rt/RtUtils.h) (smoothing, dB, one-pole coefficients): use them rather than
     writing them again. They are inline, so they cost what the same code written out would.
   - An instrument writes its output (it is first on a MIDI track) and reads notes from `ctx.inEvents`; an effect
     processes in place.
   - A sidechain: override `hasSidechain()` and read `sidechain(c)` / `sidechainConnected()` in `render()`.
   - Latency: override `latencySamples()` (one that follows the settings: keep a `dsp::LatencyReport`, Dsp.h); tails: `tailSamples()`.
   - Displays: pass `{{"id", samplesPerValue}, ...}` to the constructor and `publish()` from `render()`.
   - State besides parameters: override `stateValues()` / `setStateValues()`; load files with `loadSource()`; hand
     anything big to the rendering thread without locks (see the Sampler).
2. **Build it**: `ninja -C build` ([building.md](../building.md)). The `CONFIGURE_DEPENDS` glob in
   [engine/CMakeLists.txt](../../engine/CMakeLists.txt) picks up a new file in `builtin/devices/` (and writes its
   anchor) on the next build; nothing needs listing.
3. **It appears**: the registry lists it, so `BuiltinRegistry::instance().devices()`, the application layer's
   `builtinDevices()` and the browser's *Built-in* category have it, with its parameters' defaults. Instruments go on
   MIDI tracks. The device view shows its knobs; automation and saving work.
4. **An editor of its own (optional)**: add `ui/qml/devices/editors/MyDeviceEditor.qml` and list it under
   `"mydevice"` (the id without `builtin:`) in [DeviceEditors.qml](../../ui/qml/devices/editors/DeviceEditors.qml); draw
   displays in a `DeviceCanvas` item's `refreshDisplays()`. See [ui/device-view.md](../ui/device-view.md#adding-an-editor).
5. **Tests**: render it offline (as [tests/engine/test_engine_render.cpp](../../tests/engine/test_engine_render.cpp)
   does for Utility and Over The Top, or a file of its own like
   [tests/engine/test_compressor_engine.cpp](../../tests/engine/test_compressor_engine.cpp); a block added to
   DspBlocks.h gets a case in [tests/engine/test_dsp_blocks.cpp](../../tests/engine/test_dsp_blocks.cpp)), and if it
   has an editor, give the editor a test file of its own, `tests/app/test_ui_device_editors_mydevice.cpp` (as
   [test_ui_device_editors_gate.cpp](../../tests/app/test_ui_device_editors_gate.cpp) is), on the host in
   [tests/app/support/EditorHarness.h](../../tests/app/support/EditorHarness.h) (each `test_*.cpp` in tests/app is a
   program of its own; nothing needs listing), and add its kind to `registry()` in
   [tests/app/test_ui_device_editors.cpp](../../tests/app/test_ui_device_editors.cpp), which checks the registry. See
   [testing.md](../testing.md).
6. **Docs**: add it to this page and to [guide/devices.md](../guide/devices.md).

## Invariants and real-time rules

- `render()` never locks, allocates or frees. Buffers are sized in `prepare()` (the Synth's per-sample filter
  coefficients and mix are `maxBlockSize` long; a block is never longer than `prepare()` said).
- Parameter values are atomics read with relaxed order; a stretch reads a consistent value per parameter because
  automation is applied between stretches on the same thread.
- `ParamInfo` lists are static and never change for a built-in device (unlike a plug-in's).
- `setStateValues()` may run on a pool thread at the same time as `render()` on the audio thread; anything it shares
  with `render()` goes through lock-free handoffs.

## Gotchas

- The id in `typeId()` must start with `builtin:`; the registry strips it to get the id the UI uses.
- `BuiltinRegistry::devices()` instantiates every device once to describe it: a constructor must be cheap and
  side-effect free.
- `setParam()` clamps to the range; automation goes through `fromNormalized()`, so discrete parameters land on steps.
- Devices are shared between live playback and offline renders; the engine resets them before and after an offline
  render (a flag the rendering thread acts on, plus `resetOffline()`).
- A device that is switched off is reset when it comes back on.

## Tests

The engine's tests are in [tests/engine](../../tests/engine) (one executable, `engine_tests`).

- [test_engine_render.cpp](../../tests/engine/test_engine_render.cpp): the Utility device, Over The Top, chains per
  strip, moving processors between chains.
- [test_dsp_blocks.cpp](../../tests/engine/test_dsp_blocks.cpp): the shared blocks of DspBlocks.h, each measured
  against what it is meant to do: a one-pole smoothing and the DC blocker removing DC, the delay line's reads (between
  samples too), the envelope follower's times, noise uniform and repeatable and `fastTanh` against tanh, the LFO's
  shapes and cycles, biquads responding as their magnitude says, the crossover's bands adding up flat (as its
  all-pass does), the sliding maximum over a changing window, oversampling transparent in band and delayed by its
  latency, oversampled saturation folding back far less, a low, narrow biquad ringing out to exact zeros, and the half-band decimator (its taps for 20 kHz at
  44.1 and 48 kHz, flat to its passband as a pure delay, 120 dB down where it would fold, unity at DC).
- [test_compressor_engine.cpp](../../tests/engine/test_compressor_engine.cpp): its curve (hard knee follows the ratio;
  nothing below the knee), attack holding on low notes, sidechain keying, and its displays.
- [test_gate_engine.cpp](../../tests/engine/test_gate_engine.cpp): its listing; opening at the threshold and closing
  below Return, after Hold, over Attack and Release, to the sample (and opening again from where a release had got
  to; a Hold changed while it counts applying at once); Floor (silence, no effect, in between) and Flip; lookahead as
  latency (opening early, the hold counted from the output, a new window holding what came before it, `idle()`, the
  engine lining the output up with the timeline); keying from a sidechain, its gain and mix; the key EQ (each type
  plays as `gate::keyFilterDb` draws it; a new type, or the EQ switched on, heard once its filter has caught up with
  the key, to the frame whatever the blocks, then fading between two settled outputs, with nothing to settle);
  listening; every control moving without a jump (the largest steps) and click-free (a 6th-difference measure against
  the same change made at once); automation to the sample whatever the block size, and through the engine; reset and
  a new rate; the extremes at 8 to 192 kHz; NaN, infinity and absurd levels in the input or on the sidechain (the
  output what a zero there gives, to the bit, with the EQ on or off, listening or not); stability, and silence
  ringing out to exact zeros (bounded by the key EQ's own poles); one channel (and back to two without the right's
  old delay); the displays.
- [test_limiter_engine.cpp](../../tests/engine/test_limiter_engine.cpp): its listing, latency and tail; below the
  ceiling the input, the lookahead late; no sample over the ceiling for every mode, routing and link and other settings
  (sines, noise, lone spikes, a square, Nyquist); a lone peak caught exactly, the gain falling only over the S - 1
  samples before it; the manual release's time constant; Auto's release, quick after a short peak, slow after sustained
  limiting, and a sustained bass note's distortion no worse than a manual 300 ms's; True Peak against a 32x reference
  meter of the test's own (a quarter of the rate at 45°, band-limited noise, short bursts up to 18 kHz at 12 sub-sample
  offsets) and a 4x meter from the device's own phases; the interpolator's flatness and the parabola; Soft Clip's peak,
  harmonics, reduction and curve sample by sample; Maximize (at its defaults changing nothing); L/R, M/S and Link (M/S
  at Link 100 % exactly L/R linked, in Standard and True Peak); every control changing without a click (a 6th-difference
  measure, against an unfaded 6 dB step; each change must change the output, the release's on a falling tone; Auto's
  20 ms blend) and the lookahead's dip; glides smooth in dB, and a moving ceiling leaving quiet material untouched;
  automation through the engine; reset and new rates; the extremes, and input that isn't audio (NaN, ±inf, beyond
  1e30, in either routing) leaving no trace; silence ringing out to exact zeros; one channel; latency compensated
  through the engine; its seven displays. The device runs alone on `harness/Standalone.h`.
- [test_multiband_engine.cpp](../../tests/engine/test_multiband_engine.cpp): its listing; flat doing nothing (any
  crossovers, Amount 0 bit for bit); each side's law (Above and Below, compressing and expanding, the +30 and −96 dB
  limits, the upward fade, both at once), the knee, Amount; attack, release and Time as Live defines them (timed on
  the displays); Peak and RMS and switching between them; Input and the Outputs; each band, switched off (into the mid
  band), bypassed by its activator (its split kept, its displays showing the level as it comes) and soloed; Listen
  (the input unkeyed; keyed, the key after S/C Gain and S/C Mix's blend); a sidechain keying each band by its own
  band (Gain, Mix, silence, one channel, connected again from silence, the displays reading the trigger); every
  control changing without a click (a 6th-difference measure, against a splice); automation through the engine, to
  the sample; reset and new rates; silence ringing out to exact zeros; NaNs and infinities in the input and in the
  key (Listen too), and the loudest input let in (1e30); the extremes at 44.1 and 192 kHz; one channel and linked
  stereo (RMS reading the louder channel's power on sides that differ); the tail; the displays; its cost (in the
  thread's CPU time); and an output that doesn't depend on how blocks are cut. Its device runs on
  [harness/Standalone.h](../../tests/engine/harness/Standalone.h), with a sidechain fed a block at a time.
- [test_hue_engine.cpp](../../tests/engine/test_hue_engine.cpp): its listing, its latency at
  every rate in both Qualities, transparency with no notes or all twelve (both), notes kept, taken out and moved,
  High Quality landing a moved note on its pitch, MIDI notes, changing notes without a click, extremes and silence,
  Quality switched while playing (no click, the same latency), and its editor's displays (`input` and `output`, mono,
  a value a sample; `notes`, the notes in use as bits every 1024 samples, the MIDI notes held with MIDI Notes on).
- [test_swiftf0_engine.cpp](../../tests/engine/test_swiftf0_engine.cpp): the port against swift-f0 under ONNX Runtime
  (a recorded reference: pitch within 0.01 cents, confidence within 1e-4, the same voicing); streamed at 44.1 to
  96 kHz (steady tones voiced, within a few cents, each frame final between its lookahead and `delay()` after its
  centre); silence unvoiced, and again after a reset.
- [test_gigatune_engine.cpp](../../tests/engine/test_gigatune_engine.cpp): its listing; keys, scales and the notes
  chosen; its tracker against fastpitch's offline pYIN frame for frame (with the whole signal, and decided 32 ms on;
  decided as the frames come, all but 1 %) and its refined pitch within 0.02 cents at 44.1-96 kHz; Strength 0
  passing voice, noise and silence bit for bit,
  `latency()` later, at any block size; notes landing within 0.01 cents of their frequency, in a key, a vibrato held;
  Strength, Pitch, Retune Speed as a time constant (and a vibrato kept when slow); Formant moving the envelope;
  the level whatever the phase of a pulse train; no grain late at the extremes (44.1-192 kHz; Low Latency: under 3 %
  of the samples squeezed, a C2 voice an octave down); silence and noise;
  the song's key through the device; the editor's displays (voiced from when the voice comes out, the pitch heard,
  Strength's share of the correction and the note); refinement, pass-through, notes landing and late grains at both
  Qualities, and the Quality switched while playing (its latency, the engine told). With `GIGATUNE_VOCALS` (a folder of dry phrases named `..._<key>_dry.wav`) it
  also corrects real vocals to their keys (`GIGATUNE_OUT`: writing what it made).
- [test_changeling_engine.cpp](../../tests/engine/test_changeling_engine.cpp): its listing and latencies (Live GigaTune's Low Latency's, Studio `kStudioLag` hops more, High Quality's about 0.3 s, at 44.1-192 kHz, the device's with its drive's 31 samples); nothing to do passing voice, noise and silence bit for bit, `latency()` later, at every quality, rate and block size, one channel or two, and through the device; Transpose within 0.01 cents of its frequency (whole and fractional semitones, every quality); Quantize on the scale's nearest note, the vibrato gone, plus Pitch, and Robot on C4 plus Pitch; MIDI notes (Robot's note, Transpose's distance from C4, held, at their input time) and Glide's 10-90 % time; Formant and Link moving the envelope; Harmonics taking the lowest k harmonics 40 dB down and keeping the rest within a dB, no line between them; the twin's line and the level held, noise not doubled; Detune's cents each side and Width's halves; Smear; silence and noise; every control at its ends finite and bounded (High Quality never late); the Quality switched while playing; Mix; the drive (its 0 the input exactly, its small-signal gain, ceiling and lopsidedness); the quadrature pair's 90 degrees; the editor's displays.
- [test_spectral_engine.cpp](../../tests/engine/test_spectral_engine.cpp): its listing, the defaults gentle on pink
  noise at a mix's level, and the shared maths (frame sizes, calibration, the knee, the gain computer's curve rising
  everywhere, Below held at the threshold, the Focus's weights); its latency, the frame and a hop, at every rate and an
  impulse coming out that late; transparent under the threshold and with Range 0, and Dry/Wet 0 % the input delayed bit
  for bit; pink noise reading its own level at 44.1, 48 and 96 kHz; downward compression, Range, upward compression
  under Below (held at the threshold, nothing under the upward floor), Tilt following a spectrum both ways, Smoothing
  spreading a tone's gain, the Focus band and its edges, Delta as the difference, all with numbers; attack and release
  in time against the magnitude envelopes' formulas (which a follower of power would miss by a factor of two to four);
  Stereo Link (linked, each its own, halfway in dB); keying by a sidechain (alone, mono, silent, not connected, and
  through an engine); every control changing without a click (a 6th-difference measure, the ramped ones against a
  spliced switch); automation to the sample, alone and through the engine, in step with the audio; reset; silence
  ringing out to exact zeros; stability at the extremes; NaN and infinity in the input and the key playing exactly as
  zeros there, keyed or not, and absurd levels (+600 dBFS) leaving everything finite and the sound as it was 2 s on, and
  +360 dBFS reading at most +300 dB in every display; one channel the left of two; the displays (counts, whole frames
  for a reader that fell behind, published three hops late to the hop, the meters, the output display with Delta); the
  engine lining other tracks up with it; and bounds on its cost in the thread's CPU time, in all and per audio callback
  (no callback carries a whole frame).
- [test_saturator_engine.cpp](../../tests/engine/test_saturator_engine.cpp): its listing; quiet audio untouched at
  the defaults, bit for bit; each curve played as its editor draws it (eight types, four drives, the Bass Shaper's
  thresholds, the Waveshaper's settings) and the curves' numbers; the Waveshaper's controls (no effect at WS Drive 0,
  Lin's slope, Curve's third harmonic, Damp's gate, Depth's and Period's ripples); odd harmonics only; Post Clip under
  the Output level at any Dry/Wet (a hot input's dry share too); Output and Dry/Wet; Color leaving a clean sound
  alone (also while every control glides, with and without Hi-Quality, and with Hi-Quality switched off mid-glide)
  and moving the saturation, its design's exact inverse; DC; Hi-Quality's latency (and `idle()`), its aliasing (the
  curves' and Post Clip's), the tracks lined up through the engine, and its pre-roll (bit for bit as if on all
  along); every control changing without a click (a 6th-difference measure, against the settings either side and
  those a glide crosses, and an unfaded splice; Hi-Quality with Color and with Post Clip too); automation through the
  engine, to the sample; reset and a new rate; silence ringing out to exact zeros without flush-to-zero (slow Color
  sections at 1x and 4x too); the tail; extremes at 44.1 to 192 kHz (Hard Clip's ceiling at any Dry/Wet, within the
  4x filters' worst gain with Hi-Quality); NaN and infinity in the input coming out as zeros there would (DC and
  Color running, at 4x, DC switched on after); one channel, and channels independent; its displays (also in blocks
  that split the meters' 128 samples).
- [test_amp_engine.cpp](../../tests/engine/test_amp_engine.cpp): its listing; the design (each stack's make-up and its
  digital response against the analog one, the curve, its anti-aliasing exact at rest and when clipped, the morph's
  ends, the transfer's parts); each model's character and level-matched defaults; Gain, Volume (the power stage it
  drives, and the distortion it adds on Blues and Heavy) and the sag with its recovery; NaN, infinity and absurd
  levels in its input playing as zeros would, bit for bit (Mono, Dual and dry), and a +600 dBFS sample blocking it for
  under a second; the tone curve as played; the transfer as played (its slope, and a 1 kHz tone's peaks at −26 and −12
  dBFS on every model: within 0.25 dB, 1 dB with Gain, Presence and Volume at 10); the tone controls driving V3; Mono
  and Dual; the Output switch and every model morph click-free (a 6th-difference measure against an unfaded splice, at
  −12 dBFS and at −80, where the tone is pure) and the morphs level-matched; Rock's and Lead's dials click-free at −12
  dBFS and every model's every dial at −80; automation through the engine to the sample (against an amp alone: a
  sample off fails); Dry/Wet with the dry delayed; latency (a small tone's phase on every model, and the engine's
  compensation exact); the tail and silence ringing out to exact zeros (the slowest stack too); reset and a new rate;
  extremes at 22 to 192 kHz and +12 dBFS; no DC; aliasing (Gain 10, noon, and Gain, Presence and Volume at 10); the
  displays; its cost (the thread's CPU time, Release only; and a model change's first block against a steady one's); a
  model change's levels a cell at a time (the old model bit for bit until the morph sets off, at the same sample in
  any blocks); model changes faster than a morph, and a change in a morph, level-matched while a dial moves; blocks of
  1 to 1024 frames and slices bit for bit (soft onsets, silence and sleep in Dual too); sleep (its cost, Release only)
  and waking as a fresh amp.
- [test_erosion_engine.cpp](../../tests/engine/test_erosion_engine.cpp): its listing and 2 ms latency at any rate;
  Amount 0 a clean delay of its latency, bit for bit, and transparent through the engine (its modulators running on
  as at any Amount); a sine's sidebands as Bessel functions of the modulation index (within 0.1 %), Frequency moving
  them, Width doing nothing to the sine, Stereo putting the sides a quarter cycle apart; the noise's RMS at any
  Frequency and Width (and its closed-form power gain against the impulse response's), and while the band moves a
  long way; two devices set alike modulating apart; Stereo decorrelating it, highs eroded before lows, Width
  spreading the sidebands; the same output however the block is split; every control changing without a click (a
  6th-difference measure, against a spliced switch) and landing where it was turned; automation through the engine,
  to the chunk; reset and a new rate; stability at the extremes and under random automation; silence as exact
  zeros; NaN and infinity in the input as silence (bit for bit as a 0 there); one channel the left of two (and two
  again); the displays; the band the editor draws against the filter's impulse response.
- [test_overdrive_engine.cpp](../../tests/engine/test_overdrive_engine.cpp): its listing; a quiet tone through the
  band and Tone as the design draws them (with ADAA's half sample); Drive's harmonics, even ones too, the level held;
  Dynamics (quiet parts quieter, never more than came in); Dry/Wet 0 % bit for bit; no DC; silence ringing out to
  exact zeros; every control changing without a click; blocks not mattering; the displays.
- [test_auto_filter_engine.cpp](../../tests/engine/test_auto_filter_engine.cpp): its listing; each type and slope at
  two resonances within 0.1 dB of `autofilter::responseDb`, and the design's figures (Butterworth at no resonance,
  unity band-pass, Morph's quarters, Q); Morph as played; the envelope opening and closing it, and keyed by a
  sidechain (silent when connected but quiet); the LFO's rate, depth, Phase, Spin and sync to the song; the circuits
  bounded at the extremes, Clean ignoring Drive, a driven circuit's harmonics; Dry/Wet bit for bit and Output;
  switching type, slope, resonance and circuit without a click; silence to exact zeros; blocks not mattering.
- [test_delay_engine.cpp](../../tests/engine/test_delay_engine.cpp): synced and free times, offset, link,
  feedback, ping pong, freeze, the filter, the modes (with automation changing the time), and its display; swing
  (alone and in ping pong), drive and duck.
- [test_bitcrusher_engine.cpp](../../tests/engine/test_bitcrusher_engine.cpp): its listing, the bits' levels, the
  rate's held samples, fully dry, and drive, dither and tone in range.
- [test_arpeggiator_engine.cpp](../../tests/engine/test_arpeggiator_engine.cpp): a MIDI effect after the synth in
  its chain playing it: the styles, octaves, gate, swing, a free rate, Hold, switched off, in a rack's chain.
- [test_groove_engine.cpp](../../tests/engine/test_groove_engine.cpp) (`engine_tests groove`, with the maths'
  [test_groove.cpp](../../tests/engine/test_groove.cpp)): the Groove device's listing; swing moving the offbeats
  later, their note-offs with them, and nothing at 50 %; a note's bends following it by its id; velocity and Length
  from the groove's steps; on a Drum Rack's pad only that pad's notes (heard as C3); Timing automated taking effect
  note by note; switched off mid-note, no note hanging; stopping dropping the held notes that haven't sounded. Early
  notes: a clip's note grooved early playing early, to the sample, across a chunk's end in chunks of several sizes
  (and on its time with Allow Early off); never more than half a step early; round the loop, before the loop's start,
  each once; to a pad's Groove device (the rack's other chain hearing every note in its time, once); stopping and
  locating dropping a note handed early that hasn't sounded. Follow Groove: the Arpeggiator's steps, the Delay's swing
  and a synced LFO's phase following the project's groove, and not when it is off.
- [test_chorus_engine.cpp](../../tests/engine/test_chorus_engine.cpp): its listing and the design's figures (the
  warmth's curve, its antiderivative and its stage among them); passing the input bit for bit fully dry (with the
  high-pass on, the crossover's all-pass); each layout a plain delay of its centre at Amount 0 (the impulses after
  the lines' fade-in, as the tests' sounds that must reach the delays whole);
  the delays, sample by sample on a ramp, in every mode (Auto's centre following Amount, two taps and Ensemble's three
  averaging out, Vibrato's Offset and triangle, the fixed Times, fractional delays at 44.1 kHz, and at 96 kHz); Ensemble
  beating; the detune; feedback's echoes, Invert, no feedback in Vibrato (Feedback and Invert changing nothing
  there), the limiter holding a tone on the loop's
  resonance and DC not building up; Warmth's gentle distortion and level at -6 and 0 dBFS, its low-pass and its
  aliasing at 5, 9 and 13 kHz; the high-pass split (to 1e-5), the lows unchorused and, half wet, the lows below it
  whole; Width (ignored in Vibrato); one channel as the left of two, a third untouched; Output and Dry/Wet; every
  control changing without a click, both ways for the continuous ones (a 6th-difference measure, against an unfaded
  switch); switched on in the middle of a 440 Hz tone (reset there, its output faded in as the renderer does) with
  no step larger than the tone's or the device's always on, in every mode and at the longest delays; NaN, infinity
  and 3e38 in the input as silence, bit for bit; automation through the engine, to the sample; any block size;
  reset, repeatability and a
  new rate; stability at the extremes at 44.1 to 192 kHz, under random automation of every control and with Mode
  turning round every 64 samples; silence ringing out to exact zeros; the tail; the displays (the level after
  Output).
- [test_phaser_engine.cpp](../../tests/engine/test_phaser_engine.cpp): its listing; the notches where the design puts
  them (in closed form, against the stages' own phase and the measured response), Spread moving them apart, the LFO
  sweeping them, stereo Phase and Spin; the flanger's comb and its feedback (and Ø) to the sample, the doubler's copy;
  the delays swept; the envelope follower, Safe Bass, Warmth, Output and Dry/Wet (bit for bit fully dry); a change of
  mode or of Notches landing exactly on the new setting; every control changing without a click, each where it is
  heard, and a fast sweep into the stages' limits (a 6th-difference measure, against unfaded switches); every LFO
  shape at Duty 0 and ±80 % as its design draws it, LFO 2 alone, synced and half mixed in, Triangle Analog's level
  following its rate; Warmth's aliases far down; automation through the engine, to the sample; synced LFOs following
  the song (and Spin automated deep into one), random shapes repeating, an LFO's jumps crossfading; reset and a new
  rate; silence ringing out to exact zeros (no denormal output on the way, gains gliding to 0 too); the tail;
  stability at the extremes and at 22.05 to 192 kHz; one channel playing as either of two; the displays; its cost (in
  the thread's CPU time, so a busy machine doesn't fail it); the editor's curve (`phaser::curve`: notches drawn at
  their depth, a dense comb as a steady band).
- [test_reverb_engine.cpp](../../tests/engine/test_reverb_engine.cpp): its listing; exact silence for silence; fully
  dry, the input untouched bit for bit; the decay per band (125 Hz, 1 kHz, 8 kHz; three Decays, shelf and low-pass;
  every Density; both shelves off) against `reverb::decaySeconds`, by Schroeder integration in steep bands; the shelves
  damping their bands; the reflections placed, signed and weighted as `earlyTaps` says at four predelays and sizes, the
  first at the predelay; Shape moving the diffuse onset and the reflections' envelope; the input filter as
  `inputFilterDb` draws it; mono in; Stereo from mono to two independent sides; one channel; Reflect, Diffuse and the
  overall level; Freeze holding (where the same unfrozen dies away), Cut keeping new sound out (and without Cut letting
  it in), the release decaying at Decay's pace; Flat; the guard (left alone by full-scale noise, holding a +12 dBFS
  input); each Density's decay and echo density, a change of it keeping a frozen tail, and what joins starting from
  silence (after holding a loud tail); Spin swinging and drifting each reflection as `spinPan` and `spinDriftMs` say
  (Doppler included), and reaching the tail; Chorus spreading a tone, Diffusion blurring the echoes sooner, Scale
  setting the diffusers' lengths; no metallic ringing; every control and switch changing without a click (a
  6th-difference measure, against the steady render and an unfaded gap; Smooth switched mid-glide too); automation
  through the engine, to the sample; reset and a new rate (44.1 to 192 kHz; Size's glide at the new rate); extremes;
  silence ringing out to exact zeros, denormal input, waking as a fresh device (also with Predelay, Size and Shape
  raised while it slept, and woken just after it fell asleep, the delays jumping as it wakes at each Smooth); a NaN, an
  infinity or 1e31 in its input playing exactly as a 0 there would (at the defaults, fully dry, and frozen with the
  input feeding the tail), and the loudest input it takes staying finite; its tail; its displays (levels, the signal
  sample for sample, the tail's level falling at Decay's pace, the LFOs' phases however blocks fall, and when switched
  while it sleeps); the design's helpers for the editor; what it costs (in the thread's CPU time).
- [test_disperser_engine.cpp](../../tests/engine/test_disperser_engine.cpp): its listing; passing through
  untouched (no stages, bypassed, fully dry); Dry/Wet's blend, to the sample, and its notches; a flat magnitude (every bin within 0.01 dB, all its energy) and the
  design's group delay, at the extremes (20 s at 20 Hz), at 8 to 192 kHz and kept below Nyquist;
  stability; never more energy out than in, however Frequency and Pinch jump; each channel its own;
  Amount landing exactly on the new number of stages; every control changing without a click (a
  6th-difference measure, against an unfaded switch); automation through the engine, to the sample;
  reset and a new rate; silence ringing out to exact zeros; the tail; no latency (the other tracks not
  delayed, the dispersion where it should be).
- [test_transient_engine.cpp](../../tests/engine/test_transient_engine.cpp): its listing; its input bit for bit at
  its defaults (whatever the block size); its envelope within 0.1 dB on a tone from 40 Hz to 20 kHz; Attack turning
  onsets up and down and Sustain tails, each leaving what is steady alone (Attack Time and Release mattering as they
  should); the gain within its range; what is under the threshold untouched; Output, Dry/Wet (to the sample) and Clip
  (never over full scale, untouched under its knee); lookahead as latency, the gain applied exactly that much early;
  every control changing without a click (a 6th-difference measure, against the same settings unchanged); automation
  the same bit for bit whatever the block size; reset and a new rate; NaN and infinity as silence, the extremes at 8
  to 192 kHz; ringing out to exact zeros; one channel; the displays; the hit the editor draws, shaped as the device
  shapes one.
- [test_vocoder_engine.cpp](../../tests/engine/test_vocoder_engine.cpp): its listing; the bands (evenly in log
  frequency, each at 0 dB at its centre and -3 dB half a spacing out, Retro narrower and louder up, Formant's shift);
  the noise carrier speaking with the modulator's spectrum at about its level (Enhance or not); silence to exact
  zeros, and a sound stopping ringing out to them (a short Release, or Gate); a modulator (and a carrier) starting
  after silence without a spike; Dry/Wet 0 % the input bit for bit; External from the sidechain (its harmonics; silence without one);
  Modulator; Pitch Tracking at the input's pitch, moved by Pitch, and the tracker alone; Formant; Depth (0 % flat and
  as loud, 200 % deeper) and Gate; Unvoiced on a hiss and not on a pitched input; Mono, Stereo, L/R and one channel;
  every band count and filter at 44.1 to 192 kHz; its displays.
- [test_vocal_rider_engine.cpp](../../tests/engine/test_vocal_rider_engine.cpp): its listing; quiet and loud phrases
  ridden to the target (within 0.5 dB) and one there already left alone; Range Min and Max; holding between phrases
  (a noise floor not brought up); Speed; Music Sens. (0, 50 and 100 %, and a silent sidechain); Output; the Vocal
  weighting; its displays.
- [test_eq_engine.cpp](../../tests/engine/test_eq_engine.cpp): each band type plays as `eq::responseDb` draws it,
  matching the analog filters, placement (left, mid, side), output gain and gain scale, extremes, the displays.
- [test_modulators_engine.cpp](../../tests/engine/test_modulators_engine.cpp): the LFO and the Envelope on their
  own (listing; the curve at the rate, free and synced following the song or running on stopped; the sine; Depth,
  Offset, Phase; Retrigger; Smooth; the Envelope from notes and sidechain hits, re-armed, at rest, its length,
  Depth and Velocity; Random, from seeds, each cycle and trigger; block sizes), then modulation through the
  engine: a parameter moved every 64 samples, over a range and inverted, a modulator after its target, synced to the song, ahead of automation (which plays again
  after), into and out of racks, a modulator moving a modulator, one switched off holding, routes the engine
  refuses, an Envelope keyed by another track.
- [test_sidechain_device_engine.cpp](../../tests/engine/test_sidechain_device_engine.cpp): hits to the sample, the
  curve sample by sample (straight and bent), depth and smoothing, the threshold and re-arming, lookahead (as
  latency), Lows Only keeping the highs, hits on the beat, the synced length, the displays, extremes.
- [test_sampler_engine.cpp](../../tests/engine/test_sampler_engine.cpp): listing and parameters (old projects' defaults),
  pitch from key, root and tuning at any file rate, start, end and loop, velocity, its state's text (escaping), a
  missing file, unknown values, swapping samples while notes play, and the position display; then transients found
  where hits start and slices at each sensitivity, beats and regions, snapping; Slice (by region, beat and transient,
  Mono, Poly, Thru), 1-Shot (Trigger, Gate, fades, one note at a time), Classic's Loop Start and Loop Fade, reverse,
  snap, gain and pan, the filter's four types and slopes and resonance, the LFO (tremolo, vibrato, synced, restarted,
  pan, filter), voices and legato glide, warping (Re-Pitch, every stretching mode, keys, tempo, repeatable renders,
  resampled past 4 times as fast, also a note already playing), the display of a reversed note, a resonant filter ringing out after the last note,
  and a random LFO restarted by notes rendering the same every time.
- [test_vital_engine.cpp](../../tests/engine/test_vital_engine.cpp): Vital's listing (every parameter of its engine's,
  the hidden and unautomatable ones, lists), its texts as Vital shows them; silent until a note, a note at its pitch
  and its offset in any block, released; a note's bend moving it alone, 48 semitones either way (and at its note-on's
  sample); MIDI pitch bend, the sustain pedal; a modulation heard (an LFO swinging the level), its state kept, a state
  it can't read refused; a new patch taking over under a held note without a click; oversampling and rates rebuilding
  the engine (the pitch holding); sleeping to exact zeros and waking; reset; presets written and read back, older ones
  upgraded, others refused; the sources and destinations; wavetables of shapes, from wavetable files (with their `clm `
  chunk), vocoded and pitched, from `.vitaltable` files; LFO shapes heard; through the engine.
- [test_supersaw_engine.cpp](../../tests/engine/test_supersaw_engine.cpp): the Supersaw's listing and latency at every
  rate; a saw jumping where its phase says, as late as the device reports, to a thousandth of a sample (timed by its
  fundamental's phase); no alias of a saw within 130 dB of its fundamental at any pitch, at 44.1, 48 and 96 kHz; the
  band-limited step (its ends, its symmetry); a stack's layout (places, Curve, pans, Blend, unit power, the pan law) and
  each saw playing where it says, as loud; a stack as loud whatever its count; Width (0: both sides the same, to the bit;
  wider, less alike, as loud); Focus (a wide, detuned stack's fundamental steady and in the middle, its harmonics above
  as they were; a single saw as loud at every harmonic, the sub kept; switched and moved under a note without a jump,
  the same to the bit however blocks are cut); Tilt (each saw where Tilt puts it below and above Tilt At, both ways
  and half way, following the note; a stack that is off not counting; moved and taken away under a chord without a
  jump, the same to the bit however blocks are cut); Transpose, Fine and three stacks; the sub; the envelopes, the filter's types, resonance,
  key tracking and envelope; Phase and Random Phase; Drift; glide in Mono, Legato and Poly, going back to a key still
  held; a new Mode taking the next note, asleep or not (even one in the same period); a note's bend and the wheel; the pedal; stealing; Saws changed mid-note, faded in; silence as zeros and
  sleeping; the same output to the bit whatever the blocks (chords, bends, the wheel, automation, Mono); through the
  engine, the same twice and on the beat; every rate; its cost.
- [test_midi_engine.cpp](../../tests/engine/test_midi_engine.cpp): the Synth plays the right pitch and level.
- [test_note_bends_engine.cpp](../../tests/engine/test_note_bends_engine.cpp): the Synth and the Sampler play a
  note's bend at its bent pitch, and the Synth bends only the note a bend names.
- [test_automation_engine.cpp](../../tests/engine/test_automation_engine.cpp): built-in blocks split where automation
  changes values.

In the application's tests ([tests/app](../../tests/app)):
[test_ui_device_editors.cpp](../../tests/app/test_ui_device_editors.cpp) checks the editor registry and drives the
first six editors (the Compressor's graph, the Sampler's loading, undo, playhead, markers, drop and saving, the Delay,
the Disperser, the EQ, the Sidechain) and what the editors share (`EditorKnob` and `DeviceParamMap`, `MeterBallistics`,
`Eased` and `dbToY`); each of the others has a file of its own, `test_ui_device_editors_<kind>.cpp`, on the same host
([support/EditorHarness.h](../../tests/app/support/EditorHarness.h)):
[the Gate's](../../tests/app/test_ui_device_editors_gate.cpp),
[the Limiter's](../../tests/app/test_ui_device_editors_limiter.cpp),
[Multiband Dynamics'](../../tests/app/test_ui_device_editors_multiband.cpp),
[the Spectral Compressor's](../../tests/app/test_ui_device_editors_spectral.cpp),
[the Saturator's](../../tests/app/test_ui_device_editors_saturator.cpp),
[the Amp's](../../tests/app/test_ui_device_editors_amp.cpp),
[Erosion's](../../tests/app/test_ui_device_editors_erosion.cpp),
[the Chorus-Ensemble's](../../tests/app/test_ui_device_editors_chorus.cpp),
[the Phaser-Flanger's](../../tests/app/test_ui_device_editors_phaser.cpp),
[the Reverb's](../../tests/app/test_ui_device_editors_reverb.cpp) and
[the Groove device's](../../tests/app/test_ui_device_editors_groove.cpp); and
[test_sidechain_fit.cpp](../../tests/app/test_sidechain_fit.cpp) the Sidechain's fit.
