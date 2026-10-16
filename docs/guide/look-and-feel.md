# Look and Feel

*Options › Preferences › Look and Feel* (**Ctrl+,**, then the *Look and Feel* tab)
chooses SUBstation's theme, and how often the displays refresh
([below](#maximum-display-frame-rate)). A theme changes colours only: everything stays where it is
and the same size. The one chosen applies at once, everywhere (no restart), and is kept
for the next time SUBstation starts.

| Theme | What it looks like |
|---|---|
| *Default* | SUBstation's own: dark greys with an orange accent. |
| *Disableton* | Ableton Live 12's default theme: its greys, its pale text, orange for what is on, cyan knobs and start marker, the teal title bar of a selected device. |
| *Flashbang* | Light: white controls on light grey, dark text. |
| *Gay* | Dark, purple-black, with the rainbow over what lights up: red record, orange track activators, a yellow playhead, green loop and play, cyan knobs and markers, blue solo, violet selections, a pink accent. |

Track and clip colours follow the theme: each theme has its own 14 track colours, and a
track keeps its place among them (the first colour stays the first). *Default* softens them
to one brightness that sits with its greys; *Disableton* shows Live's own; *Flashbang*
deepens them for its light lanes; *Gay* makes them vivid. Colours from outside the
palette (a Live set's other colours) are brought to the theme's brightness, their hue
kept. The project keeps the same colours whatever the theme. The displays of the EQ, the
Sidechain and the clash view stay dark in every theme, as a screen on a device.

## Maximum display frame rate

*Maximum display frame rate*, on the same page, sets how often what moves on screen
refreshes: the playhead, the meters, a recording's waveform, the scope in the transport
bar and the devices' displays (spectra, meters, curves, the modulators' playheads). The
choices are 30, 60, 120, 144 and 240 Hz; 60 Hz is the default. On a high refresh rate
screen, 120, 144 or 240 Hz makes them move more smoothly. They never refresh faster
than the screen the window is on: under the box the page says what they refresh at
(on a 75 Hz screen with 120 Hz chosen: 75 Hz). Things fall and glide at the same speed
at any rate, only in more steps. A faster rate costs more of the computer's time:
the devices' spectra (the EQ's especially) are worked out at each refresh. Hue's
analysis of the notes stays at 30 a second. The choice is kept for the next time
SUBstation starts.

For how themes work in the code, see [../ui/README.md](../ui/README.md#themes); for the
display rate, [../app/session.md](../app/session.md#preferences).
