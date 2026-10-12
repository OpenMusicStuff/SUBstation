# Agents

AI agents such as [Claude Code](https://claude.com/claude-code) can read the project you have open in SUBstation:
its tracks and what each one is, their devices and clips, and the song's chords and key. Ask one which track plays
the horns, what the chords of the chorus are, or what is on the bass, and it looks for itself. They read only: an
agent changes nothing in your project.

## Turning it on

SUBstation talks to agents through an *MCP server* (the [Model Context Protocol](https://modelcontextprotocol.io),
which agents speak). It is off until you turn it on:

- **Options › MCP Server**, or
- **Options › Preferences… › Agents**, *Let agents read the project*.

The status line says where it is: `MCP server on at http://127.0.0.1:7381/mcp`. It stays on (or off) the next time
you start SUBstation.

## Connecting Claude Code

*Preferences › Agents* shows the command that adds SUBstation to Claude Code; **Copy** it and run it in a terminal:

```
claude mcp add --transport http substation http://127.0.0.1:7381/mcp
```

Then, in Claude Code, ask about your song: "What's on the tracks in my SUBstation project?", "Which track is the
pad?", "What chords does the song play from bar 9?". Other agents that speak MCP over HTTP connect to the same
address.

## What agents can see

| | |
|---|---|
| The project | its name, tempo, time signature, key, loop, length, and which tracks you have selected |
| The tracks | every track, return and the master: its name, [what it is](arrangement.md#what-a-track-is) (its label, as the tooltip over its name says), its mixer, devices and clips |
| One track | its instrument and preset, its effects and their amounts, its clips (where they play, their files, how many notes), its sends and where its output goes |
| Finding tracks | by name or by what they are, as [Q](arrangement.md#finding-a-track) finds them |
| The harmony | the song's key and chords, as the piano roll shows them |

They see the project as it is now, your unsaved changes too.

## Port and privacy

The server is reachable from this computer only, never from the network, and web pages can't reach it. Any program
running on this computer can read the project while it is on, so turn it off when you don't need it.

It uses port 7381. If another program has it, the status line (and *Preferences › Agents*) says the server couldn't
start: choose another **Port** there, and add SUBstation to your agent again with the new address.
