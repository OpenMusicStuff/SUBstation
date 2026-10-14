#pragma once
// The arrangement at a glance, for agents (get_arrangement_map, the
// substation://arrangement resource): a line of characters a track, one a bar
// (or a few bars), saying where it plays, and a guess at the song's sections
// from where tracks come in and drop out. The project keeps no markers, so
// this is all an agent has of "the chorus" or "the drop".
//
// Bars are the project's time signature's (one for the whole song), counted
// from 1: bar b is beats (b - 1) * beatsPerBar up to b * beatsPerBar.

#include <QString>
#include <QStringList>

#include <vector>

namespace sub::app {
class Project;
}

namespace sub::app::mcp {

// What a cell shows.
inline constexpr QChar kCellPlays = u'#';   // something is heard: an audio clip, a MIDI note
inline constexpr QChar kCellSilent = u'-';  // a clip is there, but nothing of it is heard (deactivated, no notes there)
inline constexpr QChar kCellEmpty = u'.';   // no clip

// At most this many cells a line, unless asked for fewer bars a cell.
inline constexpr int kDefaultMaxCells = 128;
inline constexpr int kMaxCells = 512;

struct ArrangementMap {
    double beatsPerBar = 4.0;
    int firstBar = 1;  // the first bar shown
    int lastBar = 1;   // the last shown (inclusive)
    int barsPerCell = 1;

    struct Row {
        QString id;
        QString name;
        QString kind;  // "audio", "midi", "group"
        int depth = 0;  // how many groups it is in
        bool muted = false;  // its or a group's of it (not heard: left out of the sections)
        QString cells;
    };
    // The tracks with clips in those bars, and groups holding any, in the arrangement's order.
    std::vector<Row> rows;
    int emptyTracks = 0;  // tracks with nothing in those bars, not in rows

    // A stretch of bars where the same tracks play: its name ("A"; again for a
    // stretch where nearly the same play as before; "silent" with none).
    struct Section {
        QString name;
        int firstBar = 1;
        int lastBar = 1;
        QStringList trackIds;  // those heard in at least half of it
    };
    std::vector<Section> sections;

    int cellCount() const { return rows.empty() ? 0 : static_cast<int>(rows.front().cells.size()); }
    // The first bar of a cell.
    int cellBar(int cell) const { return firstBar + cell * barsPerCell; }
};

// The map of bars `firstBar` to `lastBar` (0: to the song's last bar),
// `barsPerCell` bars a character (0: as few as keep it to kDefaultMaxCells; at
// most kMaxCells cells whatever is asked).
ArrangementMap arrangementMap(const Project& project, int firstBar = 1, int lastBar = 0, int barsPerCell = 0);

}  // namespace sub::app::mcp
