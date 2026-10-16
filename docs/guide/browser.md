# Browser

The browser, on the left of the window, lists built-in devices, plug-ins and the audio
files in your folders. Search it, preview files, and drag or double-click items into
the project. **Ctrl+Alt+B** (*View › Browser*) shows or hides it.

## Categories and places

The sidebar has two sections:

- **Categories**: *All* (built-in devices, plug-ins, presets and samples at once),
  *Samples* (the audio files under your places, and under it what they sound like: *Kicks*,
  *Snares*, *Claps*, *Hats*, *Cymbals*, *Toms*, *Percussion*, *Bass*, *Synths*, *Keys*,
  *Guitars*, *Vocals*, *Loops*; [below](#what-sounds-are)), *Built-in* (*Instruments*, *Audio
  Effects*), *Plug-ins* (*Instruments*, *Audio Effects*; see [plugins.md](plugins.md))
  *Presets* (by device; see [Presets](#presets)) and, while you have hidden anything,
  *Hidden* ([below](#hiding-from-search)). The arrow at the right of an entry
  collapses or expands what is under it; *Samples* starts collapsed, and the browser
  remembers what you open or close.
- **Places**: folders of your own, which you browse as a folder tree. The browser starts
  with your Music folder as a place. Add more with *Add Folder…* (at the end of the
  list, or in the right-click menu); right-click a place › *Remove from Places* to take
  it away.

The files listed are WAV, FLAC and MP3 files.

## Search

- Searching is instant. **Ctrl+F** (*Edit › Find in Browser*) searches everything
  (*All*); **Enter** (or Down) in the search field selects the first result, and Enter
  again adds it.
- An item matches when every word you type is in its name or its detail (folder,
  vendor, category), ignoring case, or, for a sample, when it *sounds like* the word:
  "snare" finds the snares however they are named ([below](#describing-a-sound)).
- Searching 200 000 files takes about 10 ms and never holds up the window or the audio;
  see [benchmarks/README.md](../../benchmarks/README.md).

## Sorting

Lists sort by *Rank* or *Name*, chosen next to the search field (and by *Similarity*
while the list shows similar sounds: [below](#find-similar-sounds)):

- *Rank* (the first time, the sort) puts what you have used most lately first: how many
  times a week you added it, on average over the last four weeks, so what you use now
  soon passes what you used a lot long ago. Then come the best matches of what you
  typed (the exact name, then names that start with it, then those with a word that
  does), and last the samples that only sound like it. An item counts as used when it
  is added to the project from the browser, by double-click, Enter or a drag that is
  dropped somewhere; its tooltip says how many times in all. The counts are kept in
  `%LOCALAPPDATA%\SUBstation\library.json` (on Linux
  `~/.local/share/SUBstation/library.json`).
- *Name* is alphabetical.

## Hiding from search

Right-click anything in the list (a sample, a plug-in, a built-in device, a preset) ›
*Hide from Search* to leave it out of every list and search from then on; right-click
one of several selected items to hide them all. *Hidden*, under *Categories*, lists what
you have hidden while there is some: right-click an item there › *Show in Search* to
bring it back. What is hidden is kept with the counts, in `library.json`.

## What sounds are

Every sample in your places is listened to in the background (with Find Similar's
analysis, at no extra cost) and told what it is: a kick, a snare, a clap, a closed or open
hat, a crash, a ride, a tom, a rim, a cowbell, a shaker, a tambourine, other percussion,
bass, a synth (a lead, a pad, a pluck, a stab), keys, organ, guitar, mallets, strings, brass,
woodwinds or vocals; whether it is a
loop or a one-shot; its note, if it has one; and how it sounds. A sample's row says it on
the right, small and dimmed: "Snare · Punchy · Bright", "Bass · C2 · Synth", "Loop · Techno". Hover
it for more: what else it might be, and everything it is heard as. A sample it isn't sure
of says only how it sounds; what isn't analysed yet says nothing.

*Samples › Kicks* (and the other sounds under *Samples*: open it with its arrow) lists
every sample that sounds like one, named "Kick" or not.

### Describing a sound

Type what you want as you would say it: **hard trap snare**, **low chill kick**, **long
crash**, **dubstep ride**, **acoustic kick**, **vocal chop**, **emo guitar**, **bright 808**,
**c#2 bass**. Each word is read as one of:

| Words | Find |
|---|---|
| a kind: kick, snare, clap, hat (hi-hat, hh), closed, open, crash, ride, cymbal, tom, rim, cowbell, shaker, tambourine, perc (conga, bongo, clave...), drums, bass, keys (piano, rhodes), organ, guitar, mallet (bell, marimba), strings (violin, cello), brass (trumpet, horn), woodwind (flute, sax), vocal (vox, voice), synths (lead, pad, pluck, stab, arp) | what sounds like it, plurals too |
| loop, break, groove; one-shot, hit, chop | loops; single hits |
| a note: c2, f#, a#1, eb3 | its pitch (a tonal sound's, a tuned kick's or tom's), the octave if you give one |
| bright, dark, hard, punchy, snappy, soft, long, short, low, deep, high, fat, heavy, distorted, dirty, clean, wet, reverb, dry, acoustic, real, electric, synth, electronic, analog, pitched, noisy | how it sounds, **among its own kind**: a "bright kick" is one of the brighter kicks, a "long crash" one of the longer crashes |
| vintage, lo-fi, metallic, wooden, glitch, industrial, processed | what people call sounds like it |
| genres: house, techno, trance, dubstep, dnb (jungle), hip-hop, trap, edm, rock, 808, 909 | what people tag with the genre |
| moods: chill, emo, sad, happy, cinematic, epic, ambient, dreamy, angry | the sounds most like the mood first; a mood never hides a sound |

A sample matches when every word is in its name or folder, or heard in it. Names still
count most: a file named "Trap Snare 3.wav" comes before an unnamed one that sounds just as
trappy. Words it doesn't know (a pack's name, "Missme") match names and folders only, as
before.

How sounds are told apart was learned from open sample libraries: 70 drum machines, 9 500
Freesound drum hits with the words their uploaders tagged them with, orchestral and
synthesized notes, sung and spoken vocals, and synths, basses and drums synthesized as
producers make them. The descriptions and genres are what people
mean by them on average; moods were never labelled anywhere, so they are only read as how
they usually sound (chill: soft, darker, clean). See [../intelligence.md](../intelligence.md#sound-tagging-what-a-sound-is-and-the-words-for-it)
for how well it works.

## Find Similar Sounds

- Right-click an audio file in the browser › *Find Similar Sounds*, or an audio clip in
  the arrangement › *Find Similar Sounds*: the browser lists the sounds of your places
  most like it, the most similar first (the *Similarity* sort). For a clip, it is the
  part of the file the clip plays that counts: a kick cut out of a drum loop finds kicks.
- A bar over the list says like what (its full path under the mouse); its ✕, choosing
  *Rank* or *Name*, or a list that isn't samples (*All*, *Built-in*, *Plug-ins*,
  *Presets*) goes back to the list as it was.
- Typing filters the similar sounds and keeps their order: "808" for the 808s most like
  your kick. Clicking a place lists only its similar sounds (not its folder tree).
- The sound itself comes first when it is in your places. A sound from anywhere else (a
  recording, a file in the project's folder) works too.
- It compares how they sound (with the audio descriptors of Essentia, a library made
  for this): timbre, how it moves from the attack to the tail and how much; brightness
  and noisiness, band by band; sub-bass and air; how metallic or harmonic it is; how fast
  it starts and how long it rings; pitch, for tonal sounds, down to an 808's; and whether
  it is a one-shot or a loop. Neither the level nor the sample rate matters.
- The Sampler and the Drum Rack swap in similar sounds too, from the arrows under the
  Sampler's waveform and beside the Drum Rack's pads ([devices.md](devices.md#drum-rack)).
- Nothing needs indexing by hand: every file the browser finds is analysed in the
  background (at low priority: playback is never held up), and only once; new and
  changed files are analysed when they show up. A 5 000-file library takes about half a
  minute the first time (and once again after updating to a version that analyses
  differently). While the first analysis runs, Find Similar works with what is
  analysed so far, the footer says how far it got, and the list fills in as it goes.
- The analysis is kept in `%LOCALAPPDATA%\SUBstation\sound-index.bin` (on Linux
  `~/.local/share/SUBstation/sound-index.bin`).

## Hot swap

While a hot swap runs (an audio clip's right-click menu › *Hot-Swap Sample*, or the
File Manager), a bar over the list says what it swaps, and the list starts with the
sounds most like it. Each audio file you click (or reach with the arrow keys) plays in
its place in the song at once; double-click one (or press **Enter**) to keep it, **Esc**
or the bar's **✕** end it, and so does a drag out of it or any other edit (a click
elsewhere doesn't). The bar's *Similar* lists the sounds like the one swapped in now.
See [file-manager.md](file-manager.md#hot-swap).

## Preview

Click a file to preview it. The headphones button in the browser's footer turns preview
on or off. A preview stops when you click anywhere outside the browser.

## Adding to the project

- Drag and drop items, or double-click them (or press Enter), to add them: audio files
  become clips (set up from their names; see
  [audio-clips.md](audio-clips.md#tempo-and-key-from-file-names)), devices go on the
  selected track.
- Instruments (built-in or plug-in) go on a MIDI track, replacing its instrument. With
  no MIDI track selected, double-clicking one or dropping it below the tracks makes one.

## Presets

- *Presets* lists the presets saved with devices' save buttons (see
  [devices.md](devices.md#presets)), with a sub-entry per device they are for (a
  plug-in's name, a built-in device's, *Audio Effect Rack*, *Instrument Rack*).
- They are the `.gilpreset` files in `Documents\SUBstation\Presets` (a folder per
  device; presets put straight in that folder are listed under *Other*; default presets,
  in its `Defaults` folder, aren't listed). Changes made there in Explorer (or any file
  manager) show up while the program runs.
- Drag a preset onto the device view or a track, or double-click it, to add it as a new
  device; drop it onto a device of its kind to load it into that device.
- Right-click a preset › *Rename…*, *Delete* (to the Recycle Bin, or the trash on Linux,
  after asking) or *Show in Folder*; an audio file › *Find Similar Sounds*, *Show in
  Folder* or *Hide Tags* (*Show Tags* brings them back; remembered from one session to
  the next). Right-click *Presets* › *Show in Folder* opens the
  library.

## The index

- The files under the places are indexed in the background and the index is kept
  (`%LOCALAPPDATA%\SUBstation\browser-index.bin`; on Linux
  `~/.local/share/SUBstation/browser-index.bin`), so the next start shows it at once and
  only looks again at folders that changed.
- Files added, removed or renamed in a place show up while the program runs. On Windows
  only the first 63 places are watched like this; on Linux every folder takes one of the
  system's watches (`fs.inotify.max_user_watches`), and folders past that limit aren't
  watched. Where they aren't, changes show after a *Rescan* (or the next start).
- Results show while a first scan is still going.
- Right-click › *Rescan* reads every folder again (for drives that don't report
  changes).
- The index goes 16 folders deep and up to 300 000 files a place, and skips names
  starting with `.` or `$`; it follows junctions but not symbolic links to folders. On
  Linux names that differ only in case are different files, with use counts of their
  own.

---

For developers: [../browser.md](../browser.md).
