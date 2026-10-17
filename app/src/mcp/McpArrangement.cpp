#include "mcp/McpArrangement.h"

#include "model/Project.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace sub::app::mcp {

namespace {

// How alike two sets of tracks are (1: the same; both empty: the same).
double jaccard(const QSet<int>& a, const QSet<int>& b) {
    if (a.isEmpty() && b.isEmpty()) return 1.0;
    const qsizetype both = QSet<int>(a).intersect(b).size();
    return static_cast<double>(both) / static_cast<double>(a.size() + b.size() - both);
}

// Below this two neighbouring cells are different parts of the song;
// sections this alike are the same part again.
constexpr double kNewSection = 0.6;
constexpr double kSameSection = 0.75;

// Raises `cells`' characters over [from, to) to `mark`: in bars from the song's start (barNumberAt() less
// one: bars of any length), cells `cellBars` long from bar `origin`.
void mark(QString& cells, double origin, double cellBars, double from, double to, QChar mark) {
    constexpr double kEps = 1e-9;
    if (to <= from) return;
    const int count = static_cast<int>(cells.size());
    const int first = std::max(0, static_cast<int>(std::floor((from - origin) / cellBars + kEps)));
    const int last = std::min(count - 1, static_cast<int>(std::ceil((to - origin) / cellBars - kEps)) - 1);
    for (int i = first; i <= last; ++i) {
        // '#' wins over '-', which wins over '.'.
        if (cells[i] == kCellPlays || (cells[i] == kCellSilent && mark == kCellEmpty)) continue;
        cells[i] = mark;
    }
}

// A group's line: what is heard in any of its tracks.
void merge(QString& into, const QString& from) {
    for (qsizetype i = 0; i < into.size() && i < from.size(); ++i) {
        if (from[i] == kCellPlays || (from[i] == kCellSilent && into[i] == kCellEmpty)) into[i] = from[i];
    }
}

bool heard(const Project& project, const Track& track) {
    for (const Track* t = &track; t != nullptr; t = t->parent ? project.findTrack(*t->parent) : nullptr) {
        if (t->mute) return false;
    }
    return true;
}

}  // namespace

ArrangementMap arrangementMap(const Project& project, int firstBar, int lastBar, int barsPerCell) {
    ArrangementMap map;
    map.beatsPerBar = project.timeSignature().beatsPerBar();  // (the song's first bars')
    const MeterMap& meter = project.meter();
    const auto barOf = [&](double beat) { return barNumberAt(beat, meter) - 1.0; };  // (from 0)
    const int songLastBar = std::max(1, static_cast<int>(std::ceil(barOf(project.endBeat()) - 1e-9)));
    map.firstBar = std::max(1, firstBar);
    map.lastBar = std::max(map.firstBar, lastBar > 0 ? lastBar : songLastBar);
    const int bars = map.lastBar - map.firstBar + 1;
    map.barsPerCell = barsPerCell > 0 ? barsPerCell : (bars + kDefaultMaxCells - 1) / kDefaultMaxCells;
    map.barsPerCell = std::max(map.barsPerCell, (bars + kMaxCells - 1) / kMaxCells);
    const int cells = (bars + map.barsPerCell - 1) / map.barsPerCell;
    const double origin = map.firstBar - 1;  // (in bars)
    const double cellBars = map.barsPerCell;
    const double end = beatOfBarNumber(map.lastBar + 1, meter);
    const double originBeat = beatOfBarNumber(map.firstBar, meter);
    const Tempo& tempo = project.tempoMap();

    // Each track's line; groups' after, from what they hold.
    const std::vector<const Track*> tracks = project.allTracks();
    QHash<QString, QString> lines;
    for (const Track* track : tracks) {
        if (!track->hasClips()) continue;
        QString line(cells, kCellEmpty);
        for (const Clip& clip : track->clips) {
            const double from = std::max(clip.startBeat, originBeat);
            const double to = std::min(clip.endBeat(tempo), end);
            if (to <= from) continue;
            mark(line, origin, cellBars, barOf(from), barOf(to), kCellSilent);
            if (!clip.plays()) continue;
            if (clip.isAudio()) {
                mark(line, origin, cellBars, barOf(from), barOf(to), kCellPlays);
                continue;
            }
            for (const PlayedNote& note : clip.heardNotes()) {
                mark(line, origin, cellBars, barOf(std::max(note.start, from)), barOf(std::min(note.end, to)),
                     kCellPlays);
            }
        }
        lines.insert(track->id, line);
    }
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) {  // (what a group holds comes after it)
        const Track* track = *it;
        if (!track->parent || !lines.contains(track->id)) continue;
        auto parent = lines.find(*track->parent);
        if (parent == lines.end()) parent = lines.insert(*track->parent, QString(cells, kCellEmpty));
        merge(*parent, lines.value(track->id));
    }

    std::vector<int> leaves;  // rows heard, for the sections
    for (const Track* track : tracks) {
        if (!track->hasClips() && !track->isGroup()) continue;
        const QString line = lines.value(track->id);
        if (line.isEmpty() || std::all_of(line.begin(), line.end(), [](QChar c) { return c == kCellEmpty; })) {
            if (track->hasClips()) ++map.emptyTracks;
            continue;
        }
        const bool muted = !heard(project, *track);
        if (track->hasClips() && !muted) leaves.push_back(static_cast<int>(map.rows.size()));
        map.rows.push_back({track->id, track->name, track->kind, project.depth(track->id), muted, line});
    }
    if (map.rows.empty()) return map;

    // Sections: runs of cells where much the same tracks are heard.
    std::vector<QSet<int>> heardIn(static_cast<size_t>(cells));
    for (int row : leaves) {
        const QString& line = map.rows[static_cast<size_t>(row)].cells;
        for (int i = 0; i < cells; ++i) {
            if (line[i] == kCellPlays) heardIn[static_cast<size_t>(i)].insert(row);
        }
    }
    struct Run {
        int first;
        int last;
    };
    std::vector<Run> runs;
    for (int i = 0; i < cells; ++i) {
        if (i == 0 || jaccard(heardIn[static_cast<size_t>(i)], heardIn[static_cast<size_t>(i - 1)]) < kNewSection) {
            runs.push_back({i, i});
        } else {
            runs.back().last = i;
        }
    }
    // A cell on its own (a fill, a break) goes with the run before it (or after it, first).
    for (size_t i = 0; i < runs.size() && runs.size() > 1;) {
        if (runs[i].last > runs[i].first) {
            ++i;
            continue;
        }
        if (i > 0) {
            runs[i - 1].last = runs[i].last;
        } else {
            runs[1].first = runs[0].first;
        }
        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(i));
        if (i > 0) --i;
    }
    // What plays in a run: the tracks heard in at least half its cells.
    const auto playing = [&](const Run& run) {
        QHash<int, int> counts;
        for (int i = run.first; i <= run.last; ++i) {
            for (int row : heardIn[static_cast<size_t>(i)]) ++counts[row];
        }
        QSet<int> out;
        const int length = run.last - run.first + 1;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            if (it.value() * 2 >= length) out.insert(it.key());
        }
        return out;
    };
    // Neighbours that turned out alike are one.
    std::vector<std::pair<Run, QSet<int>>> sections;
    for (const Run& run : runs) {
        const QSet<int> set = playing(run);
        if (!sections.empty() && jaccard(sections.back().second, set) >= kSameSection) {
            sections.back().first.last = run.last;
            sections.back().second = playing(sections.back().first);
        } else {
            sections.emplace_back(run, set);
        }
    }
    // Named by letter, a part like an earlier one by its letter.
    std::vector<std::pair<QString, QSet<int>>> named;
    for (const auto& [run, set] : sections) {
        QString name;
        if (set.isEmpty()) {
            name = QStringLiteral("silent");
        } else {
            double best = kSameSection;
            for (const auto& [earlier, earlierSet] : named) {
                const double alike = jaccard(set, earlierSet);
                if (alike >= best) {
                    best = alike;
                    name = earlier;
                }
            }
            if (name.isEmpty()) {
                const int index = static_cast<int>(named.size());
                name = index < 26 ? QString(QChar(u'A' + index)) : QStringLiteral("S%1").arg(index + 1);
                named.emplace_back(name, set);
            }
        }
        ArrangementMap::Section section{name, map.cellBar(run.first),
                                        std::min(map.lastBar, map.cellBar(run.last + 1) - 1), {}};
        for (int row : leaves) {
            if (set.contains(row)) section.trackIds.append(map.rows[static_cast<size_t>(row)].id);
        }
        map.sections.push_back(section);
    }
    return map;
}

}  // namespace sub::app::mcp
