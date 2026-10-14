# Agents

AI agents such as [Claude Code](https://claude.com/claude-code) can read and edit the project you have open in
SUBstation: its tracks and what each one is, their devices and every parameter of them (a plug-in's too), the clips
and their notes, and the song's chords and key. Ask one which track plays the horns, what the chords of the chorus
are, or what Serum's filter is set to, and it looks for itself. Ask it to darken the pad, pull the drums down 2 dB,
or write a better piano part over the bars you selected, and it does it while you watch.

What an agent changes isn't in **Edit › Undo**: each change it makes is listed apart, in **View › Agent Changes**,
where you can rewind it on its own (and restore it), unless you, or a later change of the agent's still in effect,
changed the same thing since ([what comes first](#rewinding-what-an-agent-did)).

## Turning it on

SUBstation talks to agents through an *MCP server* (the [Model Context Protocol](https://modelcontextprotocol.io),
which agents speak). It is off until you turn it on:

- **Options › MCP Server**, or
- **Options › Preferences… › Agents**, *Let agents read the project*.

The status line says where it is: `MCP server on at http://127.0.0.1:7381/mcp`. It stays on (or off) the next time
you start SUBstation.

Agents may edit the project unless you turn off *Let agents edit the project* there: then they only read, and tell
you what they would change.

## Connecting Claude Code

*Preferences › Agents* shows the command that adds SUBstation to Claude Code; **Copy** it and run it in a terminal:

```
claude mcp add --transport http substation http://127.0.0.1:7381/mcp
```

Then, in Claude Code, ask about your song: "What's on the tracks in my SUBstation project?", "Which track is the
pad?", "What chords does the song play from bar 9?", "Make the lead brighter", "Select bars 17 to 24 of the piano
and ask: write a better piano part here". Other agents that speak MCP over HTTP connect to the same address.

## What agents can see

| | |
|---|---|
| The project | its name, tempo, time signature, key, its style as its sounds suggest, loop, length, the tracks you have selected, your time selection (its bars, its tracks and the clips in it), and what you did last (your last few steps in **Edit › Undo**, so "undo that" or "like I just did" makes sense to it) |
| The arrangement | where each track plays, bar by bar, at a glance, and the song's sections guessed from it: where tracks come in and drop out, and which parts come back ("A B A C"). The project has no markers, so this is how an agent finds "the chorus" or "the drop" |
| The tracks | every track, return and the master: its name, [what it is](arrangement.md#what-a-track-is) (its label, as the tooltip over its name says), its mixer, devices and clips |
| One track | its instrument and preset, its effects and their amounts, what a rack's macros and a modulator move, a sidechain's source, its clips (where they play; an audio clip's file and what it sounds like, its gain, warp and fades; how many notes), its sends, where its output goes, and what is automated |
| Automation | each envelope's points, in the parameter's own units (dB, Hz, %) |
| A device's parameters | each one's name, its value (as the device shows it: "1.2 kHz", "Saw"), its range, whether it is automated or moved by a rack's macro. A plug-in's are there once it has loaded |
| The notes | what a MIDI track plays, where, how loud, which notes are deactivated and their pitch bends and vibrato |
| What you selected | in one go: the stretch you selected, each track in it with its notes there, and the chords there, so a request about "this part" needs no looking around |
| Finding tracks | by name or by what they are, as [Q](arrangement.md#finding-a-track) finds them |
| The harmony | the song's key and chords, as the piano roll shows them |

They see the project as it is now, your unsaved changes too. Places in the song are in bars as well as beats, so
"bars 17 to 24" means the same to you and to the agent.

## Ready-made requests

SUBstation offers agents a few requests you can pick instead of typing them out. In Claude Code they are slash
commands:

| | |
|---|---|
| `/mcp__substation__write_part` | write something over the bars you selected ("a funkier bass line"), on a copy of the track |
| `/mcp__substation__describe_song` | the song's sections, what plays in each, its key and chords, what stands out, changing nothing |
| `/mcp__substation__mix_review` | suggestions for the mix (optionally about one thing: "the low end"), made only once you say which |

The arrangement at a glance can also go into a request as `@substation:substation://arrangement`.

## What agents can change

| | |
|---|---|
| A device's parameters | as you would turn its knobs: one or several at once, its on/off switch, a rack's macros. A parameter that is automated plays the new value until you re-enable its automation (**Edit › Re-Enable Automation**), as when you turn it yourself |
| A track's mixer | volume, pan, its activator (mute) and its sends |
| A track's name | |
| The song | its tempo and key |
| Notes | a new MIDI clip over a stretch of a MIDI track, playing the notes the agent writes (deactivated notes, pitch bends and vibrato too). It replaces what was there on that track, as a clip you place does; if what it replaced had bends it doesn't keep, it is told so |
| A copy of a track | as Ctrl+D makes one: agents can write a new part on a copy and leave yours as it is, for you to compare |
| The notes that are there | as the piano roll's tools change them: transpose, move, quantize, humanize (timing, or velocities as a player would), stretch, legato, lengths, velocities, deactivate or delete, some pitches only ("the kick", "the hi-hats") or a stretch of bars. Bends and vibrato stay with their notes |
| Automation | drawn as points, a ramp (a fade, a filter sweep) or an LFO (sine, triangle, saw or square: a wobble, a tremolo) on any parameter, the mixer or a send, over a stretch of bars; or taken away |
| Tracks | added (a MIDI track with an instrument, an audio track, a return), deleted, moved, grouped and ungrouped, colored |
| Modulation | an LFO or an Envelope moving other devices' parameters on its track, each over a range (a filter from 200 Hz to 2 kHz), with its shape picked by name (Sine, Saw Down, Pluck...) or drawn: a wobble that keeps time with the song, rather than drawn automation |
| Devices | added from what SUBstation has, the plug-ins installed and your presets (an instrument replaces the one on its track, as when you drop one there), removed, moved on a track or to another |

The status line says each change as it happens (`Agent: Set Cutoff of Serum 2 on 4 Bass to 1.2 kHz`).

## Rewinding what an agent did

**View › Agent Changes** (Ctrl+Alt+A) lists every change agents made since the project opened, in the order made.
**Rewind** takes one back, leaving everything else as it is: your own edits, and the agent's other changes. A change
rewound is struck through; **Restore** puts it back. **Rewind All** takes back every change still in effect. The agent
can rewind its own changes too, when you ask it to.

The two histories never undo each other's work:

- **Edit › Undo** doesn't take back what an agent changed. And if an agent changed the same thing after your last
  step (you panned the bass, then the agent panned it again), Undo would put your "before" back over the agent's
  change, so it doesn't: the status line says which change of the agent's is in the way. Rewind that change, and
  Undo goes on.
- Likewise a change of the agent's can't be rewound over what you changed of the same since (the status line says
  so): your work comes first. Undo your edit, if you want the agent's change rewound.
- Changes of the agent's to the same thing go back the latest first: one can't be rewound while a later one to the
  same thing is in effect (the status line names it). Rewind that one first, or **Rewind All**, which goes from the
  latest.
- What doesn't touch the same thing goes either way: the agent's changes to one track don't stand in the way of
  undoing what you did to another.

Agents' changes are saved with the project like any other (the title shows `*` until you save), but the list of them
isn't: opening a project starts it empty.

## Port and privacy

The server is reachable from this computer only, never from the network, and web pages can't reach it. Any program
running on this computer can read the project while it is on, and edit it unless edits are off, so turn it off when
you don't need it.

It uses port 7381. If another program has it, the status line (and *Preferences › Agents*) says the server couldn't
start: choose another **Port** there, and add SUBstation to your agent again with the new address.
