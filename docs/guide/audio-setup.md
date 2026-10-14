# Audio setup

*Options › Preferences* (**Ctrl+,**, or click the audio device's name at the right of
the transport bar) chooses the audio device, the MIDI inputs and the plug-in folders
(and the theme: [look-and-feel.md](look-and-feel.md)).
SUBstation plays through ASIO® drivers, or WASAPI shared or exclusive, with the sample
rate and buffer size you choose. On Linux it plays through PipeWire, or the *System*
driver where PipeWire doesn't run (see [below](#on-linux)).

## First start

The first launch uses the system's default output device (WASAPI; PipeWire on Linux).
Change it here.

## The Audio page

| Setting | What it does |
|---|---|
| *Driver Type* | WASAPI or ASIO (on Linux: PipeWire or *System*). ASIO is greyed out when SUBstation was built without the ASIO SDK (see [../building.md](../building.md)) |
| *Audio Device* | The WASAPI (PipeWire, *System*) device (*System Default* or one by name), or the ASIO driver, with its **Hardware Setup** button |
| *Output Channels* | ASIO and PipeWire: which pair of the device's outputs the master plays on |
| *Sample Rate* | The rates the device offers |
| *Buffer Size* | The buffer sizes the device offers, in samples |
| *Exclusive mode* | WASAPI only: lower latency; other apps are silenced |
| *Audio Threads* | How many threads render tracks at once |
| *Constrain Latency Over* | The latency over which Constrain Delay Compensation bypasses a device ([below](#playing-live-in-a-project-full-of-latency)) |

Changes there apply at once, because what a driver offers is only known while it runs.
The line below the settings shows the device running, its sample rate, buffer size and
latency, or the error if it didn't open, and in red how many times the audio has dropped
out since the device opened (the buffer ran out before it was filled, or the device lost
samples): a larger buffer may help.

If the device goes away (unplugged, or the sound server restarted), the status line says
so, and SUBstation opens it again by itself when it is back.

### Audio threads

*Audio Threads* sets how many threads render (one per core but one by default; 1 = no
workers: every track renders on the audio thread). Offline renders and exports use the
same threads.

The threads beside the driver's own run at real-time priority, as the driver's thread
does: on Windows through the Multimedia Class Scheduler ("Pro Audio"), on Linux as
`SCHED_FIFO` (or `SCHED_RR`, through rtkit), on macOS with the time-constraint policy (and in the audio device's
workgroup). If the system refuses, they run at normal priority, where anything else
running can hold them up and the audio drops out more easily at small buffer sizes; the
line below the settings says so in red. On Linux, install rtkit (most desktops have it)
or give your user a real-time priority limit in `/etc/security/limits.conf`, for example
for the `audio` group:

```
@audio - rtprio 95
```

and log in again.

### Playing live in a project full of latency

What you hear when you play is the driver's buffer (the smaller the sooner: try 64 or 128
on ASIO, or WASAPI in exclusive mode), plus the latency of the devices your sound goes
through. Plug-ins with lookahead or linear-phase processing can add tens of milliseconds,
and delay compensation holds every track back to line up with the slowest one.

The stopwatch in the transport bar turns on **Constrain Delay Compensation** (as in
Cubase). While a track is monitored (armed, or Monitor set to In), every device with
more latency than *Constrain Latency Over* (3 ms by default) on its way to the speakers
is bypassed: on the track itself, on the groups and returns it goes into, and on the
master. Those devices stay switched on; their titles are dimmed and their tooltips say
why, and they play again once nothing is monitored or the stopwatch is off. The armed
track, and the groups it goes through, also stop waiting for the other tracks' delay
compensation, whether the song plays or not (what plays back on them then comes that much
early). Exports and freezing always hear every device.

A MIDI track's name tooltip shows what a note you play on it is heard after, and why:
*Played live: 9.3 ms = MIDI input 1.3 + devices 2.0 + delay compensation 0.0 + output 6.0*.
The output part is what the driver reports (the buffer and the converters).

## ASIO

### Choosing a driver

Set *Driver Type* to ASIO and pick the driver. The *Sample Rate* and *Buffer Size* lists
show what it supports, and *Output Channels* which pair of its outputs the master plays
on (1/2, 3/4, ...; an odd last one plays in mono). A driver that sets its buffer size
itself only offers that one: change it in Hardware Setup.

- **Hardware Setup** opens the driver's own settings (buffer size, clock, routing...).
  When they change there (or in the driver's own app, or its clock follows another
  device), the driver asks for a reset and SUBstation opens it again with the new
  buffer size and sample rate, keeping the outputs.
- The status line shows the input and output latency the driver reports.

### Next time

The program opens the same driver next time. If it can't run as saved (another clock,
fewer outputs), it opens with the driver's own settings; if the driver is gone, with the
system's default output (WASAPI; *System* on Linux). If nothing opens, it runs without
audio and says so in the status line.

### Formats and limits

- Every sample format drivers use is converted (16, 24 and 32-bit integers in any
  alignment, 32 and 64-bit floats, either byte order); DSD drivers aren't supported.
  Integer formats are clipped at full scale.
- Only one ASIO driver can be open in a program (the ASIO callbacks have no way to tell
  drivers apart).

### Inputs

A track's input opens the driver's inputs it needs; every buffer of them reaches the
engine, which meters, monitors and records them (see [recording.md](recording.md)).
WASAPI devices (and the *System* driver on Linux) open outputs only, so recording from a
microphone or line input needs an ASIO driver (PipeWire on Linux); resampling works with
any.

## MIDI

The *MIDI* page lists the MIDI inputs connected, each with a check box. Every input is
used unless turned off here; tracks hear the inputs that are on (all of them, or the
one they choose). **Refresh** finds inputs plugged in (or taken out) since. An input
that could not be opened shows in red, with the reason in its tooltip. See
[midi.md](midi.md#midi-input). On Linux the inputs are ALSA's sequencer ports (hardware,
and software such as a virtual keyboard; PipeWire bridges them): an input unplugged and
plugged in again plays again by itself.

MIDI input plays one audio buffer after it arrives, so every note is delayed alike.
*Play MIDI input at once* plays each note as the next buffer starts instead: up to a
buffer sooner (half of one on average), but its timing jitters by up to a buffer.
Recorded notes are placed half a buffer earlier to make up.

While the MIDI page shows, it measures how late the system hands each input's notes over
after its driver took them in. A few milliseconds is normal. Much more points at the
device's driver or a hub, not at SUBstation.

## Plug-ins

The *Plug-ins* page lists the VST3 folders (the standard ones, always searched, and
your own: *Add Folder…*, *Remove*) and has the *Rescan Plug-ins* button with the scan's
progress and results. See [plugins.md](plugins.md#finding-them).

## Changing devices while recording

Changing the audio device (or its sample rate, or a reset by the driver) ends a
recording; what was recorded so far is kept.

## On Linux

- *Driver Type* has *PipeWire*, the default: the outputs PipeWire has (sinks, and duplex
  devices) under *Audio Device*, *System Default* following whichever is the default
  output (when it changes, SUBstation moves to it). Its inputs are the device's own, or
  those of the same sound card (else the default input). It joins PipeWire's graph,
  which runs every application at one sample rate and buffer size: the buffer size you
  choose is the most it asks for (another application asking for less makes it smaller,
  and the status line shows it), and the sample rate only changes if PipeWire allows it
  (`default.clock.allowed-rates` in its configuration). No session manager links
  SUBstation; it links itself, so it shows in patchbays (qpwgraph, Helvum) as
  "SUBstation".
- *System*, after it: the system's default sound backend through miniaudio (PulseAudio,
  ALSA, JACK...), outputs only, with no exclusive mode (the check box isn't shown). It is
  what SUBstation opens where PipeWire doesn't run.
- With no sound server and no sound device, nothing opens: SUBstation runs without audio
  and says so.

---

ASIO is a registered trademark of Steinberg Media Technologies GmbH.

For developers: [../engine/audio-devices.md](../engine/audio-devices.md),
[../app/engine-bridge.md](../app/engine-bridge.md) (`AudioSettings`),
[../app/session.md](../app/session.md) (`AudioPreferences`, `MidiPreferences`),
[../ui/README.md](../ui/README.md#dialogs) (the Preferences dialog).
