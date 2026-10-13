#pragma once
// GigaTune's notes, as its editor draws them: the pitch classes it corrects to, worked out by the engine's own
// maths (engine/src/builtin/GigaTuneDesign.h), so the notes lit are the notes it corrects to.

namespace sub::app {

// Bit c for pitch class c (C = 0). `key` is the Key choice (0: the song's tonic, 1-12: C to B), `scale` the Scale
// choice's index (0: the song's); `songTonic` the song's key's (-1: none known) and `songMinor` its mode. Song (in
// Key or Scale) with no song key known: all twelve.
int gigatuneScaleNotes(int key, int scale, int songTonic, bool songMinor);

}  // namespace sub::app
