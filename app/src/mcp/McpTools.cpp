#include "mcp/McpTools.h"

#include "audio/EngineBridge.h"
#include "editor/AgentHistory.h"
#include "editor/ProjectEditor.h"
#include "intelligence/Harmony.h"
#include "intelligence/SoundSimilarity.h"
#include "intelligence/SoundSuggestions.h"
#include "intelligence/TrackLabels.h"
#include "intelligence/TrackSearch.h"
#include "mcp/McpArrangement.h"
#include "mcp/McpEdits.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpSupport.h"
#include "mcp/McpServer.h"
#include "model/Automation.h"
#include "model/Devices.h"
#include "model/Edits.h"
#include "model/Errors.h"
#include "model/Ids.h"
#include "model/Keys.h"
#include "model/Notes.h"
#include "model/ParamSpec.h"
#include "model/Project.h"
#include "session/Selection.h"
#include "session/Session.h"
#include "tagging/SoundWords.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace sub::app::mcp {

namespace labels = intelligence::labels;

namespace {

// What each row of notes holds (get_notes, get_selection_context; write_midi_clip takes them so too).
const QJsonArray kNoteFields{QStringLiteral("startBeat"), QStringLiteral("length"), QStringLiteral("pitch"),
                             QStringLiteral("velocity")};

// How notes are given: rows with MIDI numbers, rows with names ("C3"), or objects (with bends).
enum class NoteFormat { Rows, Names, Objects };

std::optional<NoteFormat> noteFormat(const QJsonValue& value) {
    const QString text = value.toString(QStringLiteral("rows"));
    if (text == u"rows") return NoteFormat::Rows;
    if (text == u"names") return NoteFormat::Names;
    if (text == u"objects") return NoteFormat::Objects;
    return std::nullopt;
}

// A note's bend as the model reads it: [time, semitones, curve] rows, times in beats from the note's start.
QJsonArray bendJson(const std::vector<BendPoint>& points) {
    QJsonArray out;
    for (const BendPoint& p : points) out.append(QJsonArray{beat(p.time), rounded(p.semitones), rounded(p.curve)});
    return out;
}

QJsonArray vibratoJson(const std::vector<Vibrato>& vibratos) {
    QJsonArray out;
    for (const Vibrato& v : vibratos) {
        out.append(QJsonObject{{QStringLiteral("start"), beat(v.start)},
                               {QStringLiteral("length"), beat(v.length)},
                               {QStringLiteral("depth"), rounded(v.depth)},
                               {QStringLiteral("rate"), rounded(v.rate)},
                               {QStringLiteral("fade"), rounded(v.fade)}});
    }
    return out;
}

QJsonObject output(const Output& out) {
    switch (out.to) {
    case Output::To::Group: return {{QStringLiteral("to"), QStringLiteral("group")}};  // (its group, else the master)
    case Output::To::Master: return {{QStringLiteral("to"), QStringLiteral("master")}};
    case Output::To::Track: return {{QStringLiteral("to"), QStringLiteral("track")}, {QStringLiteral("trackId"), out.id}};
    case Output::To::Sidechain:
        return {{QStringLiteral("to"), QStringLiteral("sidechain")}, {QStringLiteral("deviceId"), out.id}};
    case Output::To::None: return {{QStringLiteral("to"), QStringLiteral("none")}};
    }
    return {};
}

class Tools : public ToolContext {
public:
    using ToolContext::ToolContext;

    // What every track says of itself, in a list or alone. What is as it
    // usually is (not muted, not soloed, in the middle, in no group) is left out.
    QJsonObject summary(const Track& track) const {
        const labels::TrackLabel* label = session_.trackLabels()->find(track.id);
        QJsonArray devices;
        for (const Device& device : track.devices) devices.append(deviceName(device));
        QJsonObject out{
            {QStringLiteral("id"), track.id},
            {QStringLiteral("name"), track.name},
            {QStringLiteral("kind"), track.kind},
            {QStringLiteral("volumeDb"), rounded(track.volumeDb)},
            {QStringLiteral("devices"), devices},
        };
        if (track.parent) out.insert(QStringLiteral("parent"), *track.parent);
        if (label) {
            if (!label->label.empty()) out.insert(QStringLiteral("label"), QString::fromStdString(label->label));
            if (!label->family.empty()) out.insert(QStringLiteral("family"), QString::fromStdString(label->family));
            if (!label->role.empty()) out.insert(QStringLiteral("role"), QString::fromStdString(label->role));
            if (!label->traits.empty()) out.insert(QStringLiteral("traits"), strings(label->traits));
        }
        if (rounded(track.pan) != 0.0) out.insert(QStringLiteral("pan"), rounded(track.pan));
        if (track.mute) out.insert(QStringLiteral("mute"), true);
        if (track.solo) out.insert(QStringLiteral("solo"), true);
        if (track.frozen) out.insert(QStringLiteral("frozen"), true);
        if (track.hasClips()) out.insert(QStringLiteral("clipCount"), static_cast<int>(track.clips.size()));
        return out;
    }

    QJsonObject device(const QString& trackId, const Device& device) const {
        QJsonObject out{{QStringLiteral("id"), device.id},
                        {QStringLiteral("kind"), device.kind},
                        {QStringLiteral("name"), deviceName(device)}};
        if (!device.enabled) out.insert(QStringLiteral("enabled"), false);
        if (device.isPlugin() && device.plugin) {
            out.insert(QStringLiteral("plugin"), QJsonObject{{QStringLiteral("name"), device.plugin->name},
                                                             {QStringLiteral("vendor"), device.plugin->vendor},
                                                             {QStringLiteral("instrument"), device.plugin->instrument}});
            const std::optional<QString> preset =
                session_.bridge() ? session_.bridge()->pluginPresetName(trackId, device.id) : std::nullopt;
            out.insert(QStringLiteral("loaded"), preset.has_value());
            if (preset && !preset->isEmpty()) out.insert(QStringLiteral("preset"), *preset);
        }
        // What it hears or plays from elsewhere.
        if (device.sidechain) {
            const Track* source = project().findTrack(device.sidechain->trackId);
            out.insert(QStringLiteral("sidechain"),
                       QJsonObject{{QStringLiteral("trackId"), device.sidechain->trackId},
                                   {QStringLiteral("trackName"), source ? source->name : QString()},
                                   {QStringLiteral("tap"), device.sidechain->tap}});
        }
        if (!device.midiFrom.isEmpty()) out.insert(QStringLiteral("midiFrom"), device.midiFrom);
        // What it moves: a modulator's parameters, a rack's macros'.
        if (!device.modulations.empty()) {
            QJsonArray modulations;
            for (const ModulationMapping& m : device.modulations) {
                QJsonObject mapping{{QStringLiteral("deviceId"), m.deviceId},
                                    {QStringLiteral("paramId"), m.paramId},
                                    {QStringLiteral("low"), rounded(m.low)},
                                    {QStringLiteral("high"), rounded(m.high)}};
                if (m.invert) mapping.insert(QStringLiteral("invert"), true);
                modulations.append(mapping);
            }
            out.insert(QStringLiteral("modulates"), modulations);
        }
        if (device.isRack()) {
            QJsonArray macros;
            for (const MacroMapping& m : device.macros) {
                macros.append(QJsonObject{{QStringLiteral("macro"), macroParam(m.macro)},
                                          {QStringLiteral("name"), macroName(device, m.macro)},
                                          {QStringLiteral("deviceId"), m.deviceId},
                                          {QStringLiteral("paramId"), m.paramId},
                                          {QStringLiteral("low"), rounded(m.low)},
                                          {QStringLiteral("high"), rounded(m.high)}});
            }
            if (!macros.isEmpty()) out.insert(QStringLiteral("macros"), macros);
            QJsonArray chains;
            for (const Chain& chain : device.chains) {
                QJsonArray devices;
                for (const Device& inner : chain.devices) devices.append(this->device(trackId, inner));
                chains.append(QJsonObject{{QStringLiteral("id"), chain.id},
                                          {QStringLiteral("name"), chain.name},
                                          {QStringLiteral("devices"), devices}});
            }
            out.insert(QStringLiteral("chains"), chains);
        }
        return out;
    }

    // An automation target as the model reads it: "Volume", "Delay: Feedback".
    QString targetName(const QString& owner, const QString& key) const {
        const std::optional<ParamSpec> spec = session_.bridge() ? session_.bridge()->paramSpec(owner, key) : std::nullopt;
        if (!spec) return key;
        return spec->group.isEmpty() || spec->group == u"Mixer" ? spec->name : spec->group + QStringLiteral(": ") + spec->name;
    }

    // A clip as get_track gives it: where it plays, and what is set on it that isn't as it was placed.
    QJsonObject clipJson(const Clip& clip) const {
        const double tempo = project().tempo();
        QJsonObject c{{QStringLiteral("id"), clip.id},
                      {QStringLiteral("kind"), clip.isMidi() ? QStringLiteral("midi") : QStringLiteral("audio")},
                      {QStringLiteral("startBeat"), beat(clip.startBeat)},
                      {QStringLiteral("endBeat"), beat(clip.endBeat(tempo))},
                      {QStringLiteral("startBar"), bar(clip.startBeat)},
                      {QStringLiteral("endBar"), bar(clip.endBeat(tempo))}};
        if (!clip.plays()) c.insert(QStringLiteral("active"), false);
        if (!clip.name.isEmpty()) c.insert(QStringLiteral("name"), clip.name);
        if (clip.isMidi()) {
            c.insert(QStringLiteral("noteCount"), static_cast<int>(clip.notes.size()));
            return c;
        }
        c.insert(QStringLiteral("file"), clip.path);
        if (session_.similarity()) {
            // What the file sounds like, as the browser tags it ("Punchy Kick"), once it is analysed.
            const QString source = clip.reversedFrom.isEmpty() ? clip.path : clip.reversedFrom;
            if (const auto tags = session_.similarity()->tagsOf(source)) {
                const std::string sound = intelligence::tagging::label(*tags);
                if (!sound.empty()) c.insert(QStringLiteral("sound"), QString::fromStdString(sound));
            }
        }
        if (rounded(clip.gainDb) != 0.0) c.insert(QStringLiteral("gainDb"), rounded(clip.gainDb));
        if (rounded(clip.pan) != 0.0) c.insert(QStringLiteral("pan"), rounded(clip.pan));
        if (clip.transpose != 0) c.insert(QStringLiteral("transpose"), clip.transpose);
        if (rounded(clip.detune) != 0.0) c.insert(QStringLiteral("detuneCents"), rounded(clip.detune));
        if (clip.warp) c.insert(QStringLiteral("warp"), clip.warpMode);
        if (!clip.reversedFrom.isEmpty()) c.insert(QStringLiteral("reversed"), true);
        if (clip.fadeInSec > 0.0) c.insert(QStringLiteral("fadeInSec"), rounded(clip.fadeInSec));
        if (clip.fadeOutSec > 0.0) c.insert(QStringLiteral("fadeOutSec"), rounded(clip.fadeOutSec));
        return c;
    }

    QJsonObject detail(const Track& track) const {
        QJsonObject out = summary(track);
        const labels::TrackLabel* label = session_.trackLabels()->find(track.id);
        out.insert(QStringLiteral("details"), label ? strings(label->details) : QJsonArray());
        QJsonArray devices;
        for (const Device& d : track.devices) devices.append(device(track.id, d));
        out.insert(QStringLiteral("devices"), devices);
        if (track.hasClips()) {
            QJsonArray clips;
            for (const Clip& clip : track.clips) clips.append(clipJson(clip));
            out.insert(QStringLiteral("clips"), clips);
        }
        QJsonArray sends;
        for (auto it = track.sends.constBegin(); it != track.sends.constEnd(); ++it) {
            const Track* ret = project().findTrack(it.key());
            QJsonObject send{{QStringLiteral("returnId"), it.key()},
                             {QStringLiteral("returnName"), ret ? ret->name : QString()},
                             {QStringLiteral("levelDb"), rounded(it->levelDb)}};
            if (it->preFader) send.insert(QStringLiteral("preFader"), true);
            sends.append(send);
        }
        out.insert(QStringLiteral("sends"), sends);
        if (!track.isMaster()) out.insert(QStringLiteral("output"), output(track.output));
        // What is automated (get_automation gives the points).
        QJsonArray automated;
        for (const auto& [key, envelope] : project().automation(track.id)) {
            if (envelope.empty()) continue;
            automated.append(QJsonObject{{QStringLiteral("key"), key},
                                         {QStringLiteral("target"), targetName(track.id, key)},
                                         {QStringLiteral("points"), static_cast<int>(envelope.size())}});
        }
        if (!automated.isEmpty()) out.insert(QStringLiteral("automation"), automated);
        return out;
    }

    ToolResult getProject() const {
        const Project& p = project();
        const Harmony& harmony = *session_.harmony();
        const std::optional<Key> key = harmony.key();
        const Selection& selection = *session_.selection();
        QJsonObject out{
            {QStringLiteral("name"), p.path().isEmpty() ? QStringLiteral("Untitled") : QFileInfo(p.path()).completeBaseName()},
            {QStringLiteral("path"), orNull(p.path())},
            {QStringLiteral("tempo"), rounded(p.tempo())},
            {QStringLiteral("timeSignature"), p.timeSignatureText()},
            {QStringLiteral("beatsPerBar"), beatsPerBar()},
            {QStringLiteral("key"), key ? QJsonValue(key->label()) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("keyInferred"), key.has_value() && harmony.keyInferred()},
            {QStringLiteral("loop"), QJsonObject{{QStringLiteral("enabled"), p.loopEnabled()},
                                                {QStringLiteral("startBeat"), beat(p.loopStart())},
                                                {QStringLiteral("endBeat"), beat(p.loopEnd())},
                                                {QStringLiteral("startBar"), bar(p.loopStart())},
                                                {QStringLiteral("endBar"), bar(p.loopEnd())}}},
            {QStringLiteral("endBeat"), beat(p.endBeat())},
            {QStringLiteral("endBar"), bar(p.endBeat())},
            {QStringLiteral("trackCount"), static_cast<int>(p.tracks().size())},
            {QStringLiteral("returnCount"), static_cast<int>(p.returns().size())},
            {QStringLiteral("selectedTrackId"), orNull(selection.trackId())},
            {QStringLiteral("selectedTrackIds"), QJsonArray::fromStringList(selection.trackIds())},
            {QStringLiteral("insertBeat"), beat(selection.insertBeat())},
            {QStringLiteral("insertBar"), bar(selection.insertBeat())},
        };
        // What it sounds like, as the browser's Best sort weighs it ("Techno, Punchy · A Minor").
        if (session_.suggestions() && !session_.suggestions()->summary().isEmpty()) {
            out.insert(QStringLiteral("style"), session_.suggestions()->summary());
        }
        // The time selection (a stretch of beats over tracks the user selected), and the clips in it.
        if (const auto& range = selection.timeRange()) {
            QJsonArray clips;
            for (const ClipRef& ref : selection.clips()) {
                clips.append(QJsonObject{{QStringLiteral("trackId"), ref.trackId}, {QStringLiteral("clipId"), ref.clipId}});
            }
            out.insert(QStringLiteral("timeSelection"),
                       QJsonObject{{QStringLiteral("startBeat"), beat(range->start)},
                                   {QStringLiteral("endBeat"), beat(range->end)},
                                   {QStringLiteral("startBar"), bar(range->start)},
                                   {QStringLiteral("endBar"), bar(range->end)},
                                   {QStringLiteral("trackIds"), QJsonArray::fromStringList(selection.rangeTrackIds())},
                                   {QStringLiteral("clips"), clips}});
        } else {
            out.insert(QStringLiteral("timeSelection"), QJsonValue(QJsonValue::Null));
        }
        // What the user did last (their undo stack, the latest first): what "that" and "this" are.
        QJsonArray recent;
        if (const QUndoStack* stack = session_.undoStack()) {
            for (int i = stack->index() - 1; i >= 0 && recent.size() < kRecentEdits; --i) {
                recent.append(stack->text(i));
            }
        }
        out.insert(QStringLiteral("recentEdits"), recent);
        return {out, {}, false};
    }

    // Every track, or those `query` finds (the find bar's way: TrackSearch), the best first.
    ToolResult listTracks(const QJsonObject& arguments) const {
        const QString query = arguments.value(QStringLiteral("query")).toString().trimmed();
        QJsonArray tracks;
        if (query.isEmpty()) {
            for (const Track* track : project().allTracks()) tracks.append(summary(*track));
        } else {
            for (const QString& id : TrackSearch::find(project(), *session_.trackLabels(), query)) {
                tracks.append(summary(project().track(id)));
            }
        }
        return {QJsonObject{{QStringLiteral("tracks"), tracks}}, {}, false};
    }

    ToolResult getTrack(const QJsonObject& arguments) const {
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        if (id.isEmpty()) return ToolResult::failure(QStringLiteral("track_id is required: a track's id from list_tracks."));
        const Track* track = project().findTrack(id);
        if (!track) {
            return ToolResult::failure(
                QStringLiteral("No track has the id \"%1\": list_tracks gives the ids.").arg(id));
        }
        return {detail(*track), {}, false};
    }

    // The key and the chords between two beats; with `withTracks`, the tracks
    // they were heard from (id and name: what an agent rarely needs, so asked for).
    QJsonObject harmonyJson(double from, double to, bool withTracks) const {
        const Harmony& harmony = *session_.harmony();
        QJsonArray chords;
        for (const Harmony::ChordSpan& span : harmony.chords()) {
            if (span.end <= from || span.start >= to) continue;
            chords.append(QJsonObject{{QStringLiteral("startBeat"), beat(span.start)},
                                      {QStringLiteral("endBeat"), beat(span.end)},
                                      {QStringLiteral("startBar"), bar(span.start)},
                                      {QStringLiteral("chord"), QString::fromStdString(span.chord.name())}});
        }
        const std::optional<Key> key = harmony.key();
        QJsonObject out{{QStringLiteral("key"), key ? QJsonValue(key->label()) : QJsonValue(QJsonValue::Null)},
                        {QStringLiteral("keyInferred"), key.has_value() && harmony.keyInferred()},
                        {QStringLiteral("chords"), chords}};
        if (withTracks) {
            QJsonArray tracks;
            for (const QString& id : Harmony::heardTracks(project())) {
                const Track* track = project().findTrack(id);
                tracks.append(QJsonObject{{QStringLiteral("id"), id},
                                          {QStringLiteral("name"), track ? track->name : QString()}});
            }
            out.insert(QStringLiteral("tracks"), tracks);
        }
        return out;
    }

    ToolResult getHarmony(const QJsonObject& arguments) const {
        const Span at = span(arguments);
        if (!at.error.isEmpty()) return ToolResult::failure(at.error);
        return {harmonyJson(at.from, at.to, arguments.value(QStringLiteral("include_tracks")).toBool(false)), {}, false};
    }

    // --- The arrangement at a glance ---

    // The map as the model reads it: a grid, a line a track, then the sections.
    QString arrangementText(const ArrangementMap& map) const {
        if (map.rows.empty()) return QStringLiteral("The arrangement is empty: no track has clips there.");
        QString out = QStringLiteral("Bars %1 to %2 (%3, %4 beats a bar), %5: %6 plays, %7 a clip that is silent "
                                     "there, %8 nothing. A group's line is what its tracks play.\n")
                          .arg(map.firstBar)
                          .arg(map.lastBar)
                          .arg(project().timeSignatureText(), number(map.beatsPerBar),
                               map.barsPerCell == 1 ? QStringLiteral("a character a bar")
                                                    : QStringLiteral("a character every %1 bars").arg(map.barsPerCell))
                          .arg(kCellPlays)
                          .arg(kCellSilent)
                          .arg(kCellEmpty);
        constexpr int kNameWidth = 24;
        const auto padded = [](QString text, int width) {
            if (text.size() > width) text = text.left(width - 1) + u'~';
            return text.leftJustified(width);
        };
        // A ruler: the bar each eighth cell starts.
        QString ruler(map.cellCount(), u' ');
        for (int cell = 0; cell < map.cellCount(); cell += 8) {
            const QString number = QString::number(map.cellBar(cell));
            for (qsizetype i = 0; i < number.size() && cell + i < ruler.size(); ++i) ruler[cell + i] = number[i];
        }
        out += QString(kNameWidth + 1, u' ') + ruler.trimmed() + u'\n';
        for (const ArrangementMap::Row& row : map.rows) {
            const QString name = QString(row.depth * 2, u' ') + row.name;
            QString line = padded(name, kNameWidth) + u' ' + row.cells + QStringLiteral("  ") + row.id;
            if (row.kind == kGroupKind) line += QStringLiteral(" group");
            if (row.muted) line += QStringLiteral(" muted");
            out += line + u'\n';
        }
        if (map.emptyTracks > 0) {
            out += map.emptyTracks == 1 ? QStringLiteral("(1 more track has nothing there.)\n")
                                        : QStringLiteral("(%1 more tracks have nothing there.)\n").arg(map.emptyTracks);
        }
        out += QStringLiteral("\nSections, guessed from which tracks play (a letter again: much the same tracks again):\n");
        for (const ArrangementMap::Section& section : map.sections) {
            QStringList names;
            for (const QString& id : section.trackIds) {
                if (names.size() == kSectionNames) {
                    names << QStringLiteral("+%1 more").arg(section.trackIds.size() - kSectionNames);
                    break;
                }
                names << project().track(id).name;
            }
            out += QStringLiteral("%1 bars %2-%3: %4\n")
                       .arg(padded(section.name, 6))
                       .arg(section.firstBar)
                       .arg(section.lastBar)
                       .arg(names.isEmpty() ? QStringLiteral("(nothing)") : names.join(QStringLiteral(", ")));
        }
        return out;
    }

    QJsonObject arrangementJson(const ArrangementMap& map) const {
        QJsonArray rows;
        for (const ArrangementMap::Row& row : map.rows) {
            QJsonObject r{{QStringLiteral("id"), row.id},
                          {QStringLiteral("name"), row.name},
                          {QStringLiteral("kind"), row.kind},
                          {QStringLiteral("cells"), row.cells}};
            if (row.muted) r.insert(QStringLiteral("muted"), true);
            rows.append(r);
        }
        QJsonArray sections;
        for (const ArrangementMap::Section& section : map.sections) {
            sections.append(QJsonObject{{QStringLiteral("name"), section.name},
                                        {QStringLiteral("firstBar"), section.firstBar},
                                        {QStringLiteral("lastBar"), section.lastBar},
                                        {QStringLiteral("trackIds"), QJsonArray::fromStringList(section.trackIds)}});
        }
        return {{QStringLiteral("firstBar"), map.firstBar},
                {QStringLiteral("lastBar"), map.lastBar},
                {QStringLiteral("barsPerCell"), map.barsPerCell},
                {QStringLiteral("beatsPerBar"), map.beatsPerBar},
                {QStringLiteral("tracks"), rows},
                {QStringLiteral("emptyTracks"), map.emptyTracks},
                {QStringLiteral("sections"), sections}};
    }

    ToolResult getArrangementMap(const QJsonObject& arguments) const {
        const QJsonValue start = arguments.value(QStringLiteral("first_bar"));
        const QJsonValue end = arguments.value(QStringLiteral("last_bar"));
        const QJsonValue perCell = arguments.value(QStringLiteral("bars_per_cell"));
        for (const QJsonValue& value : {start, end, perCell}) {
            // (Whole numbers: toInt() would make 17.5 its default, mapping other bars without a word.)
            const double number = value.toDouble();
            if (!value.isUndefined() && !(value.isDouble() && number >= 1.0 && number == std::floor(number) &&
                                          number <= std::numeric_limits<int>::max())) {
                return ToolResult::failure(QStringLiteral("first_bar, last_bar and bars_per_cell are whole numbers from 1."));
            }
        }
        if (start.isDouble() && end.isDouble() && end.toInt() < start.toInt()) {
            return ToolResult::failure(QStringLiteral("last_bar must not come before first_bar."));
        }
        const ArrangementMap map = arrangementMap(project(), start.toInt(1), end.toInt(0), perCell.toInt(0));
        return {arrangementJson(map), arrangementText(map), false};
    }

    // --- Automation ---

    ToolResult getAutomation(const QJsonObject& arguments) const {
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        const Track* track = id.isEmpty() ? nullptr : project().findTrack(id);
        if (!track) {
            return ToolResult::failure(
                QStringLiteral("track_id is required: a track's id from list_tracks (\"master\" for the master)."));
        }
        const Span at = span(arguments);
        if (!at.error.isEmpty()) return ToolResult::failure(at.error);
        const QString wanted = arguments.value(QStringLiteral("key")).toString();
        if (!wanted.isEmpty() && project().envelope(track->id, wanted).empty()) {
            return ToolResult::failure(QStringLiteral("%1 has no automation \"%2\": get_track lists what is automated "
                                                      "(automation).").arg(track->name, wanted));
        }
        QJsonArray envelopes;
        int total = 0;
        bool truncated = false;
        for (const auto& [key, envelope] : project().automation(track->id)) {
            if (envelope.empty() || (!wanted.isEmpty() && key != wanted)) continue;
            const std::optional<ParamSpec> spec =
                session_.bridge() ? session_.bridge()->paramSpec(track->id, key) : std::nullopt;
            QJsonArray points;
            for (const AutomationPoint& point : envelope) {
                if (point.beat < at.from || point.beat > at.to) continue;
                if (total >= kMaxAutomationPoints) {
                    truncated = true;
                    break;
                }
                ++total;
                // In the target's units, as get_device_parameters gives values (normalized without its spec).
                const double value = spec ? spec->fromNormalized(point.value) : point.value;
                QJsonArray row{beat(point.beat), spec ? rounded(value) : beat(value)};
                if (point.curve != 0.0) row.append(rounded(point.curve));
                points.append(row);
            }
            QJsonObject e{{QStringLiteral("key"), key},
                          {QStringLiteral("target"), targetName(track->id, key)},
                          {QStringLiteral("points"), points}};
            if (spec) {
                e.insert(QStringLiteral("min"), spec->minimum);
                e.insert(QStringLiteral("max"), spec->maximum);
                if (!spec->unit.isEmpty()) e.insert(QStringLiteral("unit"), spec->unit);
                // Where it is at the start of the stretch, as the device shows it.
                if (const auto value = automation::valueAt(envelope, std::isfinite(at.from) ? at.from : 0.0)) {
                    e.insert(QStringLiteral("startText"), spec->format(spec->fromNormalized(*value)));
                }
            } else {
                e.insert(QStringLiteral("normalized"), true);
            }
            if (session_.bridge() && session_.bridge()->isOverridden(track->id, key)) {
                e.insert(QStringLiteral("overridden"), true);  // (a change by hand plays over it until re-enabled)
            }
            envelopes.append(e);
        }
        QJsonObject out{{QStringLiteral("trackId"), track->id},
                        {QStringLiteral("fields"), QJsonArray{QStringLiteral("beat"), QStringLiteral("value"),
                                                              QStringLiteral("curve")}},
                        {QStringLiteral("envelopes"), envelopes}};
        if (truncated) {
            out.insert(QStringLiteral("truncated"),
                       QStringLiteral("Only the first %1 points: ask for one key, or a shorter stretch.").arg(kMaxAutomationPoints));
        }
        return {out, {}, false};
    }

    // --- Device parameters ---

    // The track and device the arguments name (racks' devices too), copied; none and why not.
    struct Located {
        std::optional<Track> track;
        std::optional<Device> device;
        QString error;
    };

    Located locate(const QJsonObject& arguments) const {
        const QString trackId = arguments.value(QStringLiteral("track_id")).toString();
        const QString deviceId = arguments.value(QStringLiteral("device_id")).toString();
        if (trackId.isEmpty()) return {{}, {}, QStringLiteral("track_id is required: a track's id from list_tracks.")};
        if (deviceId.isEmpty()) return {{}, {}, QStringLiteral("device_id is required: a device's id from get_track.")};
        const Track* track = project().findTrack(trackId);
        if (!track) {
            return {{}, {}, QStringLiteral("No track has the id \"%1\": list_tracks gives the ids.").arg(trackId)};
        }
        const Device* device = sub::app::findDevice(track->devices, deviceId);
        if (!device) {
            return {*track, {}, QStringLiteral("%1 has no device \"%2\": get_track lists its devices.").arg(track->name, deviceId)};
        }
        return {*track, *device, {}};
    }

    QJsonObject paramJson(const QString& trackId, const Param& param) const {
        const ParamSpec& spec = param.spec;
        QJsonObject out{{QStringLiteral("id"), param.id}, {QStringLiteral("name"), spec.name}};
        const std::optional<double> value = session_.bridge()->ownValue(trackId, spec.key);
        if (value) {
            out.insert(QStringLiteral("value"), *value);
            out.insert(QStringLiteral("text"), spec.format(*value));
            out.insert(QStringLiteral("normalized"), spec.toNormalized(*value));
        }
        out.insert(QStringLiteral("min"), spec.minimum);
        out.insert(QStringLiteral("max"), spec.maximum);
        out.insert(QStringLiteral("default"), spec.defaultValue);
        if (!spec.unit.isEmpty()) out.insert(QStringLiteral("unit"), spec.unit);
        if (spec.discrete()) out.insert(QStringLiteral("steps"), spec.steps);
        if (!spec.labels.isEmpty() && spec.labels.size() <= kMaxChoices) {
            out.insert(QStringLiteral("choices"), QJsonArray::fromStringList(spec.labels));
        }
        if (!project().envelope(trackId, spec.key).empty()) {
            // Its automation plays over the value (until a change by hand overrides it).
            out.insert(QStringLiteral("automated"), true);
            out.insert(QStringLiteral("overridden"), session_.bridge()->isOverridden(trackId, spec.key));
        }
        if (const auto deviceId = automation::keyDevice(spec.key)) {
            if (const auto macro = session_.editor()->macroOf(trackId, *deviceId, param.id)) {
                const Device* rack = project().findDevice(trackId, macro->first);
                out.insert(QStringLiteral("macro"),
                           QJsonObject{{QStringLiteral("rackId"), macro->first},
                                       {QStringLiteral("name"), rack ? macroName(*rack, macro->second) : QString()}});
            }
        }
        return out;
    }

    ToolResult getDeviceParameters(const QJsonObject& arguments) const {
        const Located at = locate(arguments);
        if (!at.error.isEmpty()) return ToolResult::failure(at.error);
        const Track& track = *at.track;
        const Device& device = *at.device;
        const QStringList words = arguments.value(QStringLiteral("query")).toString().split(u' ', Qt::SkipEmptyParts);
        const int offset = std::max(0, arguments.value(QStringLiteral("offset")).toInt(0));
        const int limit = std::clamp(arguments.value(QStringLiteral("limit")).toInt(kDefaultParams), 1, kMaxParams);
        std::vector<Param> found;
        for (Param& param : params(track.id, device)) {
            if (matches(param.spec.name + u' ' + param.id, words)) found.push_back(std::move(param));
        }
        QJsonArray list;
        for (int i = offset; i < static_cast<int>(found.size()) && list.size() < limit; ++i) {
            list.append(paramJson(track.id, found[static_cast<size_t>(i)]));
        }
        QJsonObject out{{QStringLiteral("trackId"), track.id},
                        {QStringLiteral("deviceId"), device.id},
                        {QStringLiteral("device"), deviceName(device)},
                        {QStringLiteral("total"), static_cast<int>(found.size())},
                        {QStringLiteral("offset"), offset},
                        {QStringLiteral("parameters"), list}};
        if (device.isPlugin()) {
            const bool loaded = session_.bridge()->pluginPresetName(track.id, device.id).has_value();
            out.insert(QStringLiteral("loaded"), loaded);
            if (!loaded) {
                out.insert(QStringLiteral("note"),
                           session_.bridge()->pluginPending(device.id)
                               ? QStringLiteral("The plug-in is still loading: its parameters come once it has.")
                               : QStringLiteral("The plug-in isn't loaded (missing, or it failed): only its on/off "
                                                "switch can be read."));
            }
        }
        if (offset + list.size() < static_cast<int>(found.size())) {
            out.insert(QStringLiteral("nextOffset"), offset + static_cast<int>(list.size()));
        }
        return {out, {}, false};
    }

    // --- Edits: each tool call one change in the agents' history ---

    ToolResult setDeviceParameters(const QJsonObject& arguments) {
        const Located at = locate(arguments);
        if (!at.error.isEmpty()) return ToolResult::failure(at.error);
        const Track& track = *at.track;
        const Device& device = *at.device;
        const QJsonArray items = arguments.value(QStringLiteral("parameters")).toArray();
        if (items.isEmpty()) {
            return ToolResult::failure(QStringLiteral("parameters is required: [{\"id\": ..., \"value\": ...}], the ids "
                                                      "from get_device_parameters."));
        }
        const std::vector<Param> all = params(track.id, device);
        struct Setting {
            Param param;
            double value;
        };
        std::vector<Setting> settings;
        for (const QJsonValue& entry : items) {
            const QJsonObject item = entry.toObject();
            const QString id = item.value(QStringLiteral("id")).toString();
            const auto it = std::find_if(all.begin(), all.end(), [&](const Param& p) { return p.id == id; });
            if (it == all.end()) {
                return ToolResult::failure(QStringLiteral("%1 has no parameter \"%2\": get_device_parameters lists them%3.")
                                               .arg(deviceName(device), id,
                                                    device.isPlugin() && all.size() <= 1
                                                        ? QStringLiteral(" (its plug-in isn't loaded)")
                                                        : QString()));
            }
            const auto [value, why] = wanted(*it, item);
            if (!value) return ToolResult::failure(why);
            settings.push_back({*it, *value});
        }
        const QString name = deviceName(device);
        const QString text = settings.size() == 1
                                 ? QStringLiteral("Set %1 of %2 on %3 to %4")
                                       .arg(settings[0].param.spec.name, name, track.name,
                                            settings[0].param.spec.format(settings[0].value))
                                 : QStringLiteral("Set %1 parameters of %2 on %3")
                                       .arg(QString::number(settings.size()), name, track.name);
        QJsonArray set;
        for (const Setting& s : settings) {
            QJsonObject p{{QStringLiteral("id"), s.param.id},
                          {QStringLiteral("name"), s.param.spec.name},
                          {QStringLiteral("value"), s.value},
                          {QStringLiteral("text"), s.param.spec.format(s.value)}};
            if (!project().envelope(track.id, s.param.spec.key).empty()) {
                p.insert(QStringLiteral("note"), QStringLiteral("It is automated: this overrides its automation until "
                                                                "the user re-enables it."));
            }
            set.append(p);
        }
        const QString trackId = track.id;
        const QString deviceId = device.id;
        const bool rack = device.isRack();
        EngineBridge* bridge = session_.bridge();
        return change(text, QStringLiteral("set_device_parameters"), {{QStringLiteral("parameters"), set}},
                      [&](ProjectEditor& editor) {
                          for (const Setting& s : settings) {
                              const QString& key = s.param.spec.key;
                              if (s.param.id == automation::kDeviceOn) {
                                  editor.setDeviceEnabled(trackId, deviceId, s.value >= 0.5);
                              } else if (const auto macro = macroIndex(s.param.id); macro && rack) {
                                  editor.setMacro(trackId, deviceId, *macro, s.param.spec.toNormalized(s.value));
                              } else if (const auto control = automation::keyChainControl(key)) {
                                  const ChainField field = control->control == automation::kChainVolume
                                                               ? ChainField::VolumeDb
                                                               : ChainField::Pan;
                                  editor.setChainParam(trackId, control->chainId, field, s.value);
                              } else {
                                  // (Its value before, for the change to go back to: a plug-in's is in the plug-in.)
                                  editor.setDeviceParam(trackId, deviceId, s.param.id, s.value, {},
                                                        bridge->ownValue(trackId, key));
                              }
                          }
                      });
    }

    // --- Edits of tracks and the project ---

    // A track's name, color and mixer: what is given.
    ToolResult setTrack(const QJsonObject& arguments) {
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        const Track* track = id.isEmpty() ? nullptr : project().findTrack(id);
        if (!track) {
            return ToolResult::failure(
                QStringLiteral("track_id is required: a track's id from list_tracks (\"master\" for the master)."));
        }
        const QJsonValue volume = arguments.value(QStringLiteral("volume_db"));
        const QJsonValue pan = arguments.value(QStringLiteral("pan"));
        const QJsonValue mute = arguments.value(QStringLiteral("mute"));
        const QJsonArray sends = arguments.value(QStringLiteral("sends")).toArray();
        const QJsonValue nameValue = arguments.value(QStringLiteral("name"));
        const QJsonValue colorValue = arguments.value(QStringLiteral("color"));
        if (volume.isUndefined() && pan.isUndefined() && mute.isUndefined() && sends.isEmpty() && nameValue.isUndefined() &&
            colorValue.isUndefined()) {
            return ToolResult::failure(QStringLiteral("Give name, color, volume_db, pan, mute or sends: what to change."));
        }
        const QString name = nameValue.toString().trimmed();
        if (!nameValue.isUndefined() && (name.isEmpty() || track->isMaster())) {
            return ToolResult::failure(track->isMaster() ? QStringLiteral("The master's name is always Master.")
                                                         : QStringLiteral("name is a track's new name (not empty)."));
        }
        const QString color = colorValue.toString().trimmed().toLower();
        static const QRegularExpression hex(QStringLiteral("^#[0-9a-f]{6}$"));
        if (!colorValue.isUndefined() && !hex.match(color).hasMatch()) {
            return ToolResult::failure(QStringLiteral("color is \"#rrggbb\"; SUBstation's own are %1.")
                                           .arg(kTrackColors.join(QStringLiteral(", "))));
        }
        if (!volume.isUndefined() && !(volume.isDouble() && volume.toDouble() <= automation::kMaxVolumeDb)) {
            return ToolResult::failure(QStringLiteral("volume_db is a number up to +%1 dB (%2 or lower is silence).")
                                           .arg(number(automation::kMaxVolumeDb), number(automation::kMinVolumeDb)));
        }
        if (!pan.isUndefined() && !(pan.isDouble() && std::abs(pan.toDouble()) <= 1.0)) {
            return ToolResult::failure(QStringLiteral("pan is from -1 (left) to 1 (right)."));
        }
        if (!mute.isUndefined() && !mute.isBool()) return ToolResult::failure(QStringLiteral("mute is true or false."));
        std::vector<std::pair<QString, double>> levels;
        for (const QJsonValue& entry : sends) {
            const QJsonObject send = entry.toObject();
            const QString returnId = send.value(QStringLiteral("return_id")).toString();
            const QJsonValue level = send.value(QStringLiteral("level_db"));
            if (!project().hasReturn(returnId)) {
                return ToolResult::failure(
                    QStringLiteral("No return track has the id \"%1\": list_tracks lists the returns.").arg(returnId));
            }
            if (!(level.isDouble() && level.toDouble() <= automation::kMaxVolumeDb)) {
                return ToolResult::failure(
                    QStringLiteral("A send's level_db is a number up to +%1 dB.").arg(number(automation::kMaxVolumeDb)));
            }
            levels.emplace_back(returnId, std::max(level.toDouble(), automation::kMinVolumeDb));
        }
        QStringList what;
        if (!nameValue.isUndefined()) what << QStringLiteral("name to %1").arg(name);
        if (!colorValue.isUndefined()) what << QStringLiteral("color to %1").arg(color);
        if (!volume.isUndefined()) what << QStringLiteral("volume to %1 dB").arg(number(volume.toDouble(), 1));
        if (!pan.isUndefined()) what << QStringLiteral("pan to %1").arg(number(pan.toDouble()));
        if (!mute.isUndefined()) what << (mute.toBool() ? QStringLiteral("mute") : QStringLiteral("unmute"));
        for (const auto& [returnId, level] : levels) {
            what << QStringLiteral("send to %1 to %2 dB").arg(project().track(returnId).name, number(level, 1));
        }
        const QString trackId = track->id;
        const QString text = QStringLiteral("Set %1 of %2").arg(what.join(QStringLiteral(", ")), track->name);
        return change(text, QStringLiteral("set_track"), {}, [&](ProjectEditor& editor) {
            if (!nameValue.isUndefined()) editor.renameTrack(trackId, name);
            if (!colorValue.isUndefined()) editor.setTrackColor(trackId, color);
            if (!volume.isUndefined()) {
                editor.setTrackParam(trackId, TrackField::VolumeDb, std::max(volume.toDouble(), automation::kMinVolumeDb));
            }
            if (!pan.isUndefined()) editor.setTrackParam(trackId, TrackField::Pan, pan.toDouble());
            if (!mute.isUndefined()) editor.setTrackParam(trackId, TrackField::Mute, mute.toBool() ? 1.0 : 0.0);
            for (const auto& [returnId, level] : levels) editor.setSend(trackId, returnId, level);
        });
    }

    ToolResult setProject(const QJsonObject& arguments) {
        const QJsonValue tempo = arguments.value(QStringLiteral("tempo"));
        const QJsonValue keyName = arguments.value(QStringLiteral("key"));
        if (tempo.isUndefined() && keyName.isUndefined()) {
            return ToolResult::failure(QStringLiteral("Give tempo or key: what to change."));
        }
        if (!tempo.isUndefined() && !(tempo.isDouble() && tempo.toDouble() >= 20.0 && tempo.toDouble() <= 999.0)) {
            return ToolResult::failure(QStringLiteral("tempo is 20 to 999 BPM."));
        }
        std::optional<Key> key;
        if (!keyName.isUndefined()) {
            const QString text = keyName.toString().trimmed();
            if (!text.isEmpty()) {
                key = keyFromName(text);
                if (!key) {
                    return ToolResult::failure(
                        QStringLiteral("\"%1\" isn't a key: write it as \"F#m\", \"Bb\" or \"C\" (\"\": none).").arg(text));
                }
            }
        }
        QStringList what;
        if (!tempo.isUndefined()) what << QStringLiteral("Tempo to %1 BPM").arg(number(tempo.toDouble()));
        if (!keyName.isUndefined()) what << (key ? QStringLiteral("Key to %1").arg(key->label()) : QStringLiteral("No Key"));
        return change(QStringLiteral("Set %1").arg(what.join(QStringLiteral(", "))), QStringLiteral("set_project"), {},
                      [&](ProjectEditor& editor) {
                          if (!tempo.isUndefined()) editor.setTempo(tempo.toDouble());
                          if (!keyName.isUndefined()) editor.setKey(key);
                      });
    }

    // --- Notes ---

    // A MIDI track's notes as played between two beats, and its clips there.
    // As rows ([startBeat, length, pitch, velocity] each, "fields" naming
    // them, "muted" listing the rows of muted ones, "bent" those with a pitch
    // bend or vibrato: a fifth of the size), with names for pitches ("C3"), or
    // as objects with each note's name, and its bend and vibrato.
    QJsonObject notesJson(const Track& track, double from, double to, NoteFormat format) const {
        QJsonArray notes;
        QJsonArray clips;
        QJsonArray muted;
        QJsonArray bent;
        bool truncated = false;
        for (const Clip& clip : track.clips) {
            if (!clip.isMidi() || clip.endBeat(0.0) <= from || clip.startBeat >= to) continue;
            QJsonObject c{{QStringLiteral("id"), clip.id},
                          {QStringLiteral("startBeat"), beat(clip.startBeat)},
                          {QStringLiteral("endBeat"), beat(clip.endBeat(0.0))},
                          {QStringLiteral("startBar"), bar(clip.startBeat)},
                          {QStringLiteral("endBar"), bar(clip.endBeat(0.0))}};
            if (!clip.plays()) c.insert(QStringLiteral("active"), false);
            clips.append(c);
            for (const PlayedNote& played : clip.playedNotes()) {
                if (played.end <= from || played.start >= to) continue;
                if (notes.size() >= kMaxNotes) {
                    truncated = true;
                    break;
                }
                const bool silent = played.note.muted || !clip.plays();
                if (format != NoteFormat::Objects) {
                    if (silent) muted.append(static_cast<int>(notes.size()));
                    if (played.note.bent()) bent.append(static_cast<int>(notes.size()));
                    const QJsonValue pitch = format == NoteFormat::Names ? QJsonValue(notes::noteName(played.note.pitch))
                                                                         : QJsonValue(played.note.pitch);
                    notes.append(QJsonArray{beat(played.start), beat(played.end - played.start), pitch, played.note.velocity});
                    continue;
                }
                QJsonObject note{{QStringLiteral("pitch"), played.note.pitch},
                                 {QStringLiteral("name"), notes::noteName(played.note.pitch)},
                                 {QStringLiteral("startBeat"), beat(played.start)},
                                 {QStringLiteral("length"), beat(played.end - played.start)},
                                 {QStringLiteral("velocity"), played.note.velocity}};
                if (silent) note.insert(QStringLiteral("muted"), true);
                if (!played.note.bend.empty()) note.insert(QStringLiteral("bend"), bendJson(played.note.bend));
                if (!played.note.vibrato.empty()) note.insert(QStringLiteral("vibrato"), vibratoJson(played.note.vibrato));
                notes.append(note);
            }
        }
        QJsonObject out{{QStringLiteral("clips"), clips}};
        if (format != NoteFormat::Objects) out.insert(QStringLiteral("fields"), kNoteFields);
        out.insert(QStringLiteral("notes"), notes);
        if (!muted.isEmpty()) out.insert(QStringLiteral("muted"), muted);
        if (!bent.isEmpty()) {
            out.insert(QStringLiteral("bent"), bent);
            out.insert(QStringLiteral("bendNote"),
                       QStringLiteral("The rows in bent have pitch bends or vibrato, which rows leave out: format "
                                      "\"objects\" gives them, and write_midi_clip takes them so. Rewriting these notes "
                                      "as rows drops their bends."));
        }
        if (truncated) {
            out.insert(QStringLiteral("truncated"),
                       QStringLiteral("Only the first %1 notes: ask for a shorter stretch (start_bar, end_bar).").arg(kMaxNotes));
        }
        return out;
    }

    ToolResult getNotes(const QJsonObject& arguments) const {
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        const Track* track = id.isEmpty() ? nullptr : project().findTrack(id);
        if (!track) return ToolResult::failure(QStringLiteral("track_id is required: a MIDI track's id from list_tracks."));
        if (!track->isMidi()) return ToolResult::failure(QStringLiteral("%1 isn't a MIDI track: it has no notes.").arg(track->name));
        const Span at = span(arguments);
        if (!at.error.isEmpty()) return ToolResult::failure(at.error);
        const auto format = noteFormat(arguments.value(QStringLiteral("format")));
        if (!format) return ToolResult::failure(QStringLiteral("format is \"rows\", \"names\" or \"objects\"."));
        QJsonObject out = notesJson(*track, at.from, at.to, *format);
        out.insert(QStringLiteral("trackId"), track->id);
        return {out, {}, false};
    }

    // --- The selection, with what a part written there needs ---

    ToolResult getSelectionContext(const QJsonObject& arguments) const {
        const auto format = noteFormat(arguments.value(QStringLiteral("format")));
        if (!format || *format == NoteFormat::Objects) {
            return ToolResult::failure(QStringLiteral("format is \"rows\" or \"names\" (get_notes gives objects)."));
        }
        const Selection& selection = *session_.selection();
        const auto& range = selection.timeRange();
        if (!range) {
            return ToolResult::failure(
                QStringLiteral("Nothing is selected in time: ask the user to select a stretch of the arrangement (click a "
                               "clip, or drag over the tracks)%1.")
                    .arg(selection.trackIds().isEmpty()
                             ? QString()
                             : QStringLiteral("; the selected tracks are %1").arg(selection.trackIds().join(QStringLiteral(", ")))));
        }
        QJsonArray clips;
        for (const ClipRef& ref : selection.clips()) {
            clips.append(QJsonObject{{QStringLiteral("trackId"), ref.trackId}, {QStringLiteral("clipId"), ref.clipId}});
        }
        QJsonArray tracks;
        int skipped = 0;
        for (const QString& id : selection.rangeTrackIds()) {
            const Track* track = project().findTrack(id);
            if (!track || track->isGroup()) continue;  // (what is in a group is listed itself)
            if (tracks.size() >= kMaxContextTracks) {
                ++skipped;
                continue;
            }
            QJsonObject out = summary(*track);
            const labels::TrackLabel* label = session_.trackLabels()->find(track->id);
            out.insert(QStringLiteral("details"), label ? strings(label->details) : QJsonArray());
            if (track->isMidi()) {
                const QJsonObject notes = notesJson(*track, range->start, range->end, *format);
                for (auto it = notes.constBegin(); it != notes.constEnd(); ++it) out.insert(it.key(), it.value());
            }
            tracks.append(out);
        }
        QJsonObject out{{QStringLiteral("startBeat"), beat(range->start)},
                        {QStringLiteral("endBeat"), beat(range->end)},
                        {QStringLiteral("startBar"), bar(range->start)},
                        {QStringLiteral("endBar"), bar(range->end)},
                        {QStringLiteral("selectedClips"), clips},
                        {QStringLiteral("tracks"), tracks},
                        {QStringLiteral("harmony"), harmonyJson(range->start, range->end, false)},
                        {QStringLiteral("tempo"), rounded(project().tempo())},
                        {QStringLiteral("timeSignature"), project().timeSignatureText()},
                        {QStringLiteral("beatsPerBar"), beatsPerBar()}};
        if (skipped > 0) {
            out.insert(QStringLiteral("moreTracks"),
                       QStringLiteral("%1 more tracks are in the selection: get_track and get_notes give them.").arg(skipped));
        }
        return {out, {}, false};
    }

    // A note's bend and vibrato as given (objects: "bend" [[time, semitones, curve]], "vibrato" [{...}]); why not.
    static QString bendsOf(const QJsonObject& item, Note& note) {
        for (const QJsonValue& value : item.value(QStringLiteral("bend")).toArray()) {
            const QJsonArray row = value.toArray();
            if (row.size() < 2 || row.size() > 3 || !row.at(0).isDouble() || !row.at(1).isDouble()) {
                return QStringLiteral("A bend point is [time, semitones, curve]: time in beats from the note's start, "
                                      "curve -1..1 (may be left out).");
            }
            BendPoint point{row.at(0).toDouble(), row.at(1).toDouble(), row.size() > 2 ? row.at(2).toDouble() : 0.0};
            if (point.time < 0.0 || point.time > note.length + notes::kEps) {
                return QStringLiteral("A bend point's time is 0 to the note's length (beats from its start).");
            }
            if (std::abs(point.semitones) > notes::kMaxBendSemitones || std::abs(point.curve) > 1.0) {
                return QStringLiteral("A bend is at most %1 semitones either way, its curve -1 to 1.")
                    .arg(number(notes::kMaxBendSemitones));
            }
            note.bend.push_back(point);
        }
        std::stable_sort(note.bend.begin(), note.bend.end(),
                         [](const BendPoint& a, const BendPoint& b) { return a.time < b.time; });
        for (const QJsonValue& value : item.value(QStringLiteral("vibrato")).toArray()) {
            const QJsonObject v = value.toObject();
            Vibrato vibrato;
            vibrato.start = v.value(QStringLiteral("start")).toDouble(0.0);
            vibrato.length = v.value(QStringLiteral("length")).toDouble(note.length - vibrato.start);
            vibrato.depth = v.value(QStringLiteral("depth")).toDouble(notes::kDefaultVibratoDepth);
            vibrato.rate = v.value(QStringLiteral("rate")).toDouble(notes::kDefaultVibratoRate);
            vibrato.fade = v.value(QStringLiteral("fade")).toDouble(notes::kDefaultVibratoFade);
            if (vibrato.start < 0.0 || vibrato.length < notes::kMinVibratoBeats ||
                vibrato.start + vibrato.length > note.length + notes::kEps || vibrato.depth < 0.0 ||
                vibrato.depth > notes::kMaxBendSemitones || vibrato.rate < notes::kMinVibratoRate ||
                vibrato.rate > notes::kMaxVibratoRate || vibrato.fade < 0.0 || vibrato.fade > 1.0) {
                return QStringLiteral("A vibrato is {start, length} beats inside the note (from its start), depth in "
                                      "semitones, rate %1 to %2 a second, fade 0 to 1.")
                    .arg(number(notes::kMinVibratoRate), number(notes::kMaxVibratoRate));
            }
            note.vibrato.push_back(vibrato);
        }
        std::sort(note.vibrato.begin(), note.vibrato.end(),
                  [](const Vibrato& a, const Vibrato& b) { return a.start < b.start; });
        for (size_t i = 1; i < note.vibrato.size(); ++i) {
            if (note.vibrato[i].start < note.vibrato[i - 1].start + note.vibrato[i - 1].length - notes::kEps) {
                return QStringLiteral("A note's vibratos don't overlap.");
            }
        }
        return {};
    }

    ToolResult writeMidiClip(const QJsonObject& arguments) {
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        const Track* track = id.isEmpty() ? nullptr : project().findTrack(id);
        if (!track) return ToolResult::failure(QStringLiteral("track_id is required: a MIDI track's id from list_tracks."));
        if (!track->isMidi()) {
            return ToolResult::failure(QStringLiteral("%1 isn't a MIDI track: duplicate_track a MIDI track, or pick one.").arg(track->name));
        }
        const Span at = span(arguments, true);
        if (!at.error.isEmpty()) return ToolResult::failure(at.error);
        const double start = at.from;
        const double end = at.to;
        if (start < 0.0 || end - start < edits::kMinMidiClipBeats) {
            return ToolResult::failure(QStringLiteral("The clip starts at beat 0 (bar 1) or later, and ends after it."));
        }
        const QJsonArray items = arguments.value(QStringLiteral("notes")).toArray();
        if (items.size() > kMaxNotes) return ToolResult::failure(QStringLiteral("At most %1 notes at once.").arg(kMaxNotes));
        // The rows (or objects) to write muted, as get_notes lists them.
        QSet<int> mutedRows;
        for (const QJsonValue& value : arguments.value(QStringLiteral("muted")).toArray()) {
            const int row = value.toInt(-1);
            if (row < 0 || row >= items.size()) {
                return ToolResult::failure(QStringLiteral("muted lists notes by their place in notes, from 0."));
            }
            mutedRows.insert(row);
        }
        std::vector<Note> notes;
        int bentWritten = 0;
        for (qsizetype i = 0; i < items.size(); ++i) {
            const QJsonValue& entry = items[i];
            // A row, as get_notes gives them ([start_beat, length, pitch, velocity]), or an object.
            const QJsonArray row = entry.toArray();
            const QJsonObject item = entry.toObject();
            if (entry.isArray() && (row.size() < 3 || row.size() > 4)) {
                return ToolResult::failure(QStringLiteral("A note row is [start_beat, length, pitch, velocity] (velocity may be left out)."));
            }
            const std::optional<int> pitch = pitchOf(entry.isArray() ? row.at(2) : item.value(QStringLiteral("pitch")));
            const double noteAt = entry.isArray() ? row.at(0).toDouble(-1.0) : item.value(QStringLiteral("start_beat")).toDouble(-1.0);
            const double length = entry.isArray() ? row.at(1).toDouble(0.0) : item.value(QStringLiteral("length")).toDouble(0.0);
            const int velocity = entry.isArray() ? (row.size() > 3 ? row.at(3).toInt(0) : 100)
                                                 : item.value(QStringLiteral("velocity")).toInt(100);
            if (!pitch) {
                return ToolResult::failure(QStringLiteral("A note's pitch is 0 to 127 (60 is C3), or its name (\"C3\", \"F#2\", \"Bb4\")."));
            }
            if (noteAt < start - edits::kEps || noteAt >= end) {
                return ToolResult::failure(
                    QStringLiteral("A note starts at beat %1, outside the clip (%2 to %3): notes' start_beat is on the "
                                   "song's timeline.").arg(number(noteAt, 3), number(start, 3), number(end, 3)));
            }
            if (!(length > 0.0)) return ToolResult::failure(QStringLiteral("A note's length is more than 0 beats."));
            if (velocity < 1 || velocity > 127) return ToolResult::failure(QStringLiteral("A note's velocity is 1 to 127."));
            Note note;
            note.pitch = *pitch;
            note.start = std::max(0.0, noteAt - start);  // (the clip's own beats)
            note.length = length;
            note.velocity = velocity;
            note.muted = mutedRows.contains(static_cast<int>(i)) || item.value(QStringLiteral("muted")).toBool(false);
            if (const QString why = bendsOf(item, note); !why.isEmpty()) return ToolResult::failure(why);
            if (note.bent()) ++bentWritten;
            notes.push_back(note);
        }
        std::sort(notes.begin(), notes.end(),
                  [](const Note& a, const Note& b) { return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch); });
        // What it writes over: the notes played there now, and how many of them bend.
        int replaced = 0;
        int bentReplaced = 0;
        for (const Clip& clip : track->clips) {
            if (!clip.isMidi()) continue;
            for (const PlayedNote& played : clip.playedNotes()) {
                if (played.end <= start || played.start >= end) continue;
                ++replaced;
                if (played.note.bent()) ++bentReplaced;
            }
        }
        const Clip clip = Clip::midi(newId(), QString(), start, end - start, 0.0, std::move(notes));
        const QString trackId = track->id;
        const QString text = QStringLiteral("Write %1 notes on %2, %3")
                                 .arg(QString::number(clip.notes.size()), track->name, stretch(start, end));
        QJsonObject out{{QStringLiteral("trackId"), trackId},
                        {QStringLiteral("clipId"), clip.id},
                        {QStringLiteral("replacedNotes"), replaced}};
        if (bentReplaced > bentWritten) {
            out.insert(QStringLiteral("warning"),
                       QStringLiteral("%1 of the notes it replaced had pitch bends or vibrato, more than it writes: "
                                      "rewind it and write them as objects with their bend (get_notes format "
                                      "\"objects\" gives them) to keep them.").arg(bentReplaced));
        }
        return change(text, QStringLiteral("write_midi_clip"), out, [&](ProjectEditor& editor) {
            std::vector<Clip> clips = editor.project()->track(trackId).clips;
            clips.push_back(clip);
            // (It wins where it overlaps: what was there is trimmed, split or goes.)
            editor.commitClips(text, {{trackId, edits::resolveOverlaps(clips, {clip.id}, project().tempo())}});
        });
    }

    ToolResult duplicateTrack(const QJsonObject& arguments) {
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        const Track* track = id.isEmpty() || !project().hasTrack(id) ? nullptr : &project().track(id);
        if (!track) return ToolResult::failure(QStringLiteral("track_id is required: a track's id from list_tracks (not a return's or the master's)."));
        const QString trackId = track->id;
        QStringList copies;
        ToolResult result = change(QStringLiteral("Duplicate %1").arg(track->name), QStringLiteral("duplicate_track"), {},
                                   [&](ProjectEditor& editor) {
                                       if (session_.bridge()) session_.bridge()->storePluginStates();  // (copies sound as it does)
                                       copies = editor.duplicateTracks({trackId});
                                   });
        if (!result.error && !copies.isEmpty()) {
            QJsonObject out = result.structured.toObject();
            out.insert(QStringLiteral("trackId"), copies.first());
            out.insert(QStringLiteral("name"), project().track(copies.first()).name);
            result.structured = out;
        }
        return result;
    }

    // --- The agents' history ---

    QJsonObject changeJson(const AgentHistory::Change& change) const {
        return {{QStringLiteral("id"), change.id},
                {QStringLiteral("change"), change.text},
                {QStringLiteral("tool"), change.tool},
                {QStringLiteral("time"), change.time.toString(Qt::ISODate)},
                {QStringLiteral("applied"), change.applied},
                {QStringLiteral("trackIds"), QJsonArray::fromStringList(change.tracks)}};
    }

    ToolResult listAgentChanges() const {
        QJsonArray list;
        for (const AgentHistory::Change& change : session_.agentChanges()->changes()) list.append(changeJson(change));
        return {QJsonObject{{QStringLiteral("changes"), list}}, {}, false};
    }

    // The changes the arguments' change_ids name, each once; none and why not.
    std::pair<std::optional<QList<int>>, QString> changeIds(const QJsonObject& arguments) const {
        QList<int> ids;
        for (const QJsonValue& value : arguments.value(QStringLiteral("change_ids")).toArray()) {
            const int id = value.toInt(-1);
            if (!session_.agentChanges()->find(id)) {
                return {std::nullopt, QStringLiteral("There is no change %1: list_agent_changes lists them.")
                                          .arg(value.toVariant().toString())};
            }
            if (!ids.contains(id)) ids.append(id);
        }
        return {ids, {}};
    }

    // Rewinds changes (or, with restore, puts rewound ones back).
    ToolResult rewindAgentChanges(const QJsonObject& arguments) {
        if (arguments.value(QStringLiteral("restore")).toBool(false)) return restoreAgentChanges(arguments);
        AgentHistory& history = *session_.agentChanges();
        const bool all = arguments.value(QStringLiteral("all")).toBool(false);
        auto [ids, why] = changeIds(arguments);
        if (!ids) return ToolResult::failure(why);
        if (all == !ids->isEmpty()) return ToolResult::failure(QStringLiteral("Give change_ids, or all: true (one of them)."));
        QList<int> rewound;
        QString problem;
        if (all) {
            problem = history.rewindFrom(0, &rewound);
        } else {
            std::sort(ids->begin(), ids->end(), std::greater<>());  // (the last first: those made over others)
            for (int id : *ids) {
                if (!history.find(id)->applied) continue;
                problem = history.rewind(id);
                if (!problem.isEmpty()) break;
                rewound.append(id);
            }
        }
        if (!problem.isEmpty() && rewound.isEmpty()) return ToolResult::failure(problem);
        QJsonArray done;
        for (int id : rewound) done.append(id);
        QJsonObject out{{QStringLiteral("rewound"), done}};
        if (!problem.isEmpty()) out.insert(QStringLiteral("stopped"), problem);
        return {out, {}, false};
    }

    ToolResult restoreAgentChanges(const QJsonObject& arguments) {
        // (Not wrapped by addTool(): rewinding works with edits off, putting changes back doesn't.)
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
        AgentHistory& history = *session_.agentChanges();
        auto [ids, why] = changeIds(arguments);
        if (!ids) return ToolResult::failure(why);
        if (arguments.value(QStringLiteral("all")).toBool(false)) {
            return ToolResult::failure(QStringLiteral("Restoring takes change_ids, not all: list_agent_changes lists the rewound ones."));
        }
        if (ids->isEmpty()) return ToolResult::failure(QStringLiteral("change_ids is required: ids from list_agent_changes."));
        std::sort(ids->begin(), ids->end());  // (the first first: as they were made)
        QJsonArray done;
        QString problem;
        for (int id : *ids) {
            if (history.find(id)->applied) continue;
            problem = history.restore(id);
            if (!problem.isEmpty()) break;
            done.append(id);
        }
        if (!problem.isEmpty() && done.isEmpty()) return ToolResult::failure(problem);
        QJsonObject out{{QStringLiteral("restored"), done}};
        if (!problem.isEmpty()) out.insert(QStringLiteral("stopped"), problem);
        return {out, {}, false};
    }

private:
    static constexpr int kDefaultParams = 100;
    static constexpr int kMaxParams = 500;
    static constexpr int kMaxChoices = 64;
    static constexpr int kMaxNotes = 5000;
    static constexpr int kMaxContextTracks = 16;
    static constexpr int kRecentEdits = 5;
    static constexpr int kSectionNames = 8;
    static constexpr int kMaxAutomationPoints = 2000;
};

}  // namespace

void addSessionTools(Protocol& protocol, Session& session) {
    auto tools = std::make_shared<Tools>(session);
    // Every edit (a tool not read-only) fails while the user has edits off.
    const auto addTool = [&protocol, tools](Tool tool) { mcp::addTool(protocol, tools, std::move(tool)); };
    // A stretch of the song, in beats or bars (the tools that take one).
    const QJsonObject startBeat = property(QStringLiteral("number"), QStringLiteral("From this beat (default: the start)."));
    const QJsonObject endBeat = property(QStringLiteral("number"), QStringLiteral("Up to this beat (default: the end)."));
    const QJsonObject startBar = property(QStringLiteral("number"), QStringLiteral("Instead of start_beat: from where this bar starts (from 1; 17.5: halfway through bar 17)."));
    const QJsonObject endBar = property(QStringLiteral("number"), QStringLiteral("Instead of end_beat: up to where this bar starts (bars 17 to 24: start_bar 17, end_bar 25)."));
    addTool({
        QStringLiteral("get_project"),
        QStringLiteral("Get Project"),
        QStringLiteral("The project open in SUBstation: its name and file, tempo (BPM), time signature and beats a bar, "
                       "key (the project's, else the one its notes suggest: keyInferred), its style as its sounds "
                       "suggest, loop, where its last clip ends, how many tracks and returns it has, the tracks the "
                       "user selected and their time selection, and what the user did last (recentEdits, the latest "
                       "first: what \"that\" is). Places are in beats and bars (startBar 17.5: halfway through bar "
                       "17; a stretch over bars 17 to 24 has startBar 17, endBar 25: tools take bars so). Start "
                       "here, then get_arrangement_map."),
        schema(),
        true,
        [tools](const QJsonObject&) { return tools->getProject(); },
    });
    addTool({
        QStringLiteral("get_arrangement_map"),
        QStringLiteral("Get Arrangement Map"),
        QStringLiteral("The song's arrangement at a glance: a line a track, a character a bar (# plays, - a clip "
                       "that is silent there, . nothing; a group's line is what its tracks play), and the sections "
                       "guessed from where tracks come in and drop out (A, B, A again...), each with its bars and "
                       "tracks. Use it to find the parts of the song (\"the chorus\", \"the drop\", \"where the bass "
                       "comes in\") and to see what plays together. Sections' firstBar and lastBar are whole bars, "
                       "both in it (a section's bars as a stretch: start_bar firstBar, end_bar lastBar + 1). Long "
                       "songs show several bars a character (bars_per_cell); first_bar and last_bar show part of it."),
        schema({{QStringLiteral("first_bar"), property(QStringLiteral("integer"), QStringLiteral("The first bar shown (default 1)."))},
                {QStringLiteral("last_bar"), property(QStringLiteral("integer"), QStringLiteral("The last bar shown, itself too (default: the song's last)."))},
                {QStringLiteral("bars_per_cell"),
                 property(QStringLiteral("integer"), QStringLiteral("Bars a character (default: 1, or as many as keep a line to 128)."))}}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getArrangementMap(arguments); },
    });
    addTool({
        QStringLiteral("list_tracks"),
        QStringLiteral("List Tracks"),
        QStringLiteral("Every track in the arrangement's order (top to bottom; groups before what they hold, "
                       "`parent` naming a track's group), then the return tracks, then the master. Each says what it "
                       "is: its label (a few words, e.g. \"Washed Out Serum Pluck\", worked out from its name, "
                       "presets, files, notes and effects), family and role (\"brass\", \"trumpet\"), traits, and its "
                       "mixer, devices and clip count. What is as usual is left out: no pan is the middle, no mute, "
                       "solo or frozen is false, no parent is in no group. With query, only the tracks matching its "
                       "words, the best first, as SUBstation's find bar (Q) finds them: names having the words at the "
                       "start of their words, then labels, then names having them anywhere, then what the labels were "
                       "made from (a preset's or a file's name); \"horn\" finds a track called Horn Stab, then an "
                       "orchestra playing a horns preset. Use get_track for one track in full."),
        schema({{QStringLiteral("query"),
                 property(QStringLiteral("string"), QStringLiteral("Only the tracks matching these words (any case; every word must match)."))}}),
        true,
        [tools](const QJsonObject& arguments) { return tools->listTracks(arguments); },
    });
    addTool({
        QStringLiteral("get_track"),
        QStringLiteral("Get Track"),
        QStringLiteral("One track (or return, or the master: \"master\") in full: what list_tracks says, plus what "
                       "its label was made from (details: its instrument and preset, notes, files, effects and their "
                       "amounts), its devices (plug-ins with their vendor and loaded preset; racks with their chains "
                       "and macros; sidechains; what modulators move), its clips (beats and bars; files and what they "
                       "sound like, gain, warp, fades; note counts; inactive ones too), its sends, where its output "
                       "goes, and what is automated (get_automation gives the points)."),
        schema({{QStringLiteral("track_id"), property(QStringLiteral("string"), QStringLiteral("The track's id, from list_tracks."))}},
               {QStringLiteral("track_id")}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getTrack(arguments); },
    });
    addTool({
        QStringLiteral("get_harmony"),
        QStringLiteral("Get Harmony"),
        QStringLiteral("The song's chords and key, inferred from the notes its MIDI tracks play (those heard: not "
                       "muted, not drums). Chords are spans of beats (and the bar each starts in) with names such as "
                       "\"Am7\" or \"C/E\". Give start_bar and end_bar (or beats) for part of the song; include_tracks "
                       "for the tracks they were heard from."),
        schema({{QStringLiteral("start_beat"), startBeat},
                {QStringLiteral("end_beat"), endBeat},
                {QStringLiteral("start_bar"), startBar},
                {QStringLiteral("end_bar"), endBar},
                {QStringLiteral("include_tracks"),
                 property(QStringLiteral("boolean"), QStringLiteral("Also the tracks the chords were heard from (id, name). Default false."))}}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getHarmony(arguments); },
    });
    const QJsonObject trackId = property(QStringLiteral("string"), QStringLiteral("The track's id, from list_tracks."));
    const QJsonObject deviceId = property(QStringLiteral("string"), QStringLiteral("The device's id, from get_track (racks' devices too)."));
    addTool({
        QStringLiteral("get_device_parameters"),
        QStringLiteral("Get Device Parameters"),
        QStringLiteral("A device's parameters (a plug-in's, a built-in device's, a rack's macros and chains) and what "
                       "each is set to now: id, name, value (in its units: min to max, unit), text (as the device shows "
                       "it, e.g. \"1.2 kHz\"), normalized (0..1), default, choices for a list, whether it is automated "
                       "and whether a rack's macro moves it. Plug-ins can have thousands: `query` keeps those whose "
                       "name has every word (\"filter cutoff\"), and a page is `limit` long (100 by default; "
                       "nextOffset says where the next starts). \"device:on\" is its on/off switch."),
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("device_id"), deviceId},
                {QStringLiteral("query"), property(QStringLiteral("string"), QStringLiteral("Words a parameter's name must have (any case)."))},
                {QStringLiteral("offset"), property(QStringLiteral("integer"), QStringLiteral("Skip this many (default 0)."))},
                {QStringLiteral("limit"), property(QStringLiteral("integer"), QStringLiteral("At most this many (1 to 500, default 100)."))}},
               {QStringLiteral("track_id"), QStringLiteral("device_id")}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getDeviceParameters(arguments); },
    });
    addTool({
        QStringLiteral("get_automation"),
        QStringLiteral("Get Automation"),
        QStringLiteral("A track's automation (a return's, the master's: \"master\"): each envelope's key, what it moves "
                       "(\"Volume\", \"Delay: Feedback\"), its range and unit, and its points as rows [beat, value, "
                       "curve] (the value in the target's units, as get_device_parameters gives values; curve, -1 to "
                       "1, bends the segment to the next point, left out when straight). get_track lists what is "
                       "automated. Give key for one envelope, start_bar and end_bar (or beats) for part of the song."),
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("key"), property(QStringLiteral("string"), QStringLiteral("One envelope's key, from get_track's automation (default: all)."))},
                {QStringLiteral("start_beat"), startBeat},
                {QStringLiteral("end_beat"), endBeat},
                {QStringLiteral("start_bar"), startBar},
                {QStringLiteral("end_bar"), endBar}},
               {QStringLiteral("track_id")}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getAutomation(arguments); },
    });
    const QJsonObject rowsFormat{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"), QJsonArray{QStringLiteral("rows"), QStringLiteral("names")}},
        {QStringLiteral("description"), QStringLiteral("\"rows\" (default: pitches as MIDI numbers) or \"names\" (pitches as names, \"C3\").")}};
    addTool({
        QStringLiteral("get_notes"),
        QStringLiteral("Get Notes"),
        QStringLiteral("The notes a MIDI track plays, on the song's timeline, and its clips there. As rows by "
                       "default: [startBeat, length, pitch, velocity] (beats; pitch 0..127, 60 is C3; velocity "
                       "1..127), `fields` naming them, `muted` listing the rows of notes that don't sound, `bent` "
                       "those with a pitch bend or vibrato; write_midi_clip takes rows the same. format \"names\" "
                       "gives pitches as names (\"G#3\"); \"objects\" gives each note as an object with its name, its "
                       "bend and vibrato. Give start_bar and end_bar (or beats) for part of the song."),
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("start_beat"), startBeat},
                {QStringLiteral("end_beat"), endBeat},
                {QStringLiteral("start_bar"), startBar},
                {QStringLiteral("end_bar"), endBar},
                {QStringLiteral("format"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                             {QStringLiteral("enum"), QJsonArray{QStringLiteral("rows"), QStringLiteral("names"), QStringLiteral("objects")}},
                             {QStringLiteral("description"), QStringLiteral("\"rows\" (default), \"names\" or \"objects\".")}}}},
               {QStringLiteral("track_id")}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getNotes(arguments); },
    });
    addTool({
        QStringLiteral("get_selection_context"),
        QStringLiteral("Get Selection Context"),
        QStringLiteral("Everything for working on what the user selected in the arrangement, in one call: the time "
                       "selection's beats and bars, the selected clips, each track in it (what get_track says of it in "
                       "short: label, instrument and effects, mixer) with a MIDI track's notes there as rows (as "
                       "get_notes gives them), the key and chords there, the tempo and time signature. Start here "
                       "when the user asks for something about \"this\", \"the selection\" or \"what I selected\". "
                       "Fails, saying so, when nothing is selected in time."),
        schema({{QStringLiteral("format"), rowsFormat}}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getSelectionContext(arguments); },
    });

    // Edits: each call is one change the user sees in View › Agent Changes, and can rewind.
    const QString edits = QStringLiteral(
        " It is one change in the agent's history (its changeId), apart from the user's undo: the user can rewind it in "
        "SUBstation, as can rewind_agent_changes.");
    QJsonObject valueSchema{
        {QStringLiteral("description"),
         QStringLiteral("In the parameter's units (min to max), a choice's name, or true/false for a switch.")}};
    valueSchema.insert(QStringLiteral("type"), QJsonArray{QStringLiteral("number"), QStringLiteral("string"), QStringLiteral("boolean")});
    const QJsonObject setting{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("id"), property(QStringLiteral("string"), QStringLiteral("The parameter's id, from get_device_parameters."))},
                     {QStringLiteral("value"), valueSchema},
                     {QStringLiteral("normalized"), property(QStringLiteral("number"), QStringLiteral("Instead of value: 0..1 over its range."))}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("id")}},
        {QStringLiteral("additionalProperties"), false}};
    addTool({
        QStringLiteral("set_device_parameters"),
        QStringLiteral("Set Device Parameters"),
        QStringLiteral("Sets parameters of one device, as the user would turn its knobs: each by id, to a value (or "
                       "normalized). \"device:on\" switches it on or off; a rack's \"macroN\" moves what is mapped to "
                       "it. A parameter that is automated plays its new value until the user re-enables its "
                       "automation.") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("device_id"), deviceId},
                {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                           {QStringLiteral("items"), setting},
                                                           {QStringLiteral("minItems"), 1}}}},
               {QStringLiteral("track_id"), QStringLiteral("device_id"), QStringLiteral("parameters")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->setDeviceParameters(arguments); },
        true,
    });
    const QJsonObject send{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("return_id"), property(QStringLiteral("string"), QStringLiteral("A return track's id, from list_tracks."))},
                     {QStringLiteral("level_db"), property(QStringLiteral("number"), QStringLiteral("The send's level, up to +6 dB (-70: silent)."))}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("return_id"), QStringLiteral("level_db")}},
        {QStringLiteral("additionalProperties"), false}};
    addTool({
        QStringLiteral("set_track"),
        QStringLiteral("Set Track"),
        QStringLiteral("A track's (a return's, the master's) name, color and mixer: name (a # in it is the track's "
                       "number: \"# Keys\" shows as \"8 Keys\"; not the master's), color (\"#rrggbb\"), volume (dB, up "
                       "to +6; -70 is silence), pan (-1 left to 1 right), mute (its activator; not the master's), and "
                       "its sends' levels. Give only what changes: all of it is one change.") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("name"), property(QStringLiteral("string"), QStringLiteral("Its new name."))},
                {QStringLiteral("color"), property(QStringLiteral("string"), QStringLiteral("\"#rrggbb\": to show what belongs together."))},
                {QStringLiteral("volume_db"), property(QStringLiteral("number"), QStringLiteral("Its volume in dB."))},
                {QStringLiteral("pan"), property(QStringLiteral("number"), QStringLiteral("-1 (left) to 1 (right)."))},
                {QStringLiteral("mute"), property(QStringLiteral("boolean"), QStringLiteral("Muted (its activator off)."))},
                {QStringLiteral("sends"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), send}}}},
               {QStringLiteral("track_id")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->setTrack(arguments); },
        true,
    });
    addTool({
        QStringLiteral("set_project"),
        QStringLiteral("Set Project"),
        QStringLiteral("The song's tempo (BPM; clips that aren't warped keep their length in seconds) and key (\"F#m\", "
                       "\"Bb\"; \"\" for none). Give only what changes.") + edits,
        schema({{QStringLiteral("tempo"), property(QStringLiteral("number"), QStringLiteral("20 to 999 BPM."))},
                {QStringLiteral("key"), property(QStringLiteral("string"), QStringLiteral("\"C\", \"F#m\", \"Bb\"...; \"\": none."))}}),
        false,
        [tools](const QJsonObject& arguments) { return tools->setProject(arguments); },
        true,
    });
    QJsonObject pitchSchema{{QStringLiteral("description"), QStringLiteral("A MIDI note number, 0..127 (60 is C3), or a name (\"C3\", \"F#2\", \"Bb4\").")}};
    pitchSchema.insert(QStringLiteral("type"), QJsonArray{QStringLiteral("integer"), QStringLiteral("string")});
    const QJsonObject note{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("pitch"), pitchSchema},
                     {QStringLiteral("start_beat"), property(QStringLiteral("number"), QStringLiteral("Where it starts on the song's timeline, inside the clip."))},
                     {QStringLiteral("length"), property(QStringLiteral("number"), QStringLiteral("How long, in beats."))},
                     {QStringLiteral("velocity"), property(QStringLiteral("integer"), QStringLiteral("1..127 (default 100)."))},
                     {QStringLiteral("muted"), property(QStringLiteral("boolean"), QStringLiteral("Deactivated: it doesn't sound (default false)."))},
                     {QStringLiteral("bend"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                                        {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}}}},
                                  {QStringLiteral("description"),
                                   QStringLiteral("Pitch bend points [time, semitones, curve]: time in beats from the note's start, curve -1..1 (may be left out). As get_notes' objects give them.")}}},
                     {QStringLiteral("vibrato"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
                                  {QStringLiteral("description"),
                                   QStringLiteral("Vibratos {start, length (beats from the note's start), depth (semitones), rate (a second), fade (0..1)}.")}}}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("pitch"), QStringLiteral("start_beat"), QStringLiteral("length")}},
        {QStringLiteral("additionalProperties"), false}};
    const QJsonObject noteRow{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("minItems"), 3},
        {QStringLiteral("maxItems"), 4},
        {QStringLiteral("description"), QStringLiteral("[start_beat, length, pitch, velocity]: pitch a number or a name (\"C3\"); velocity may be left out (100).")}};
    addTool({
        QStringLiteral("write_midi_clip"),
        QStringLiteral("Write MIDI Clip"),
        QStringLiteral("Puts a new MIDI clip on a MIDI track from start_bar to end_bar (or start_beat to end_beat), "
                       "playing these notes (their start_beat on the song's timeline, as get_notes gives them), each "
                       "a row [start_beat, length, pitch, velocity] (a fifth of the size; pitch a number or a name, "
                       "\"C3\") or an object (which can carry a bend and vibrato). `muted` lists the notes to write "
                       "deactivated, as get_notes' `muted` does. It replaces whatever was there on that track, as a "
                       "clip placed by the user does: clips under it are trimmed, split or go (replacedNotes says how "
                       "many notes; a warning if bends were lost). To keep the user's part, write on a copy "
                       "(duplicate_track) and let them compare. A part for the user's time selection: its startBar "
                       "and endBar (get_project).") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("start_beat"), property(QStringLiteral("number"), QStringLiteral("Where the clip starts."))},
                {QStringLiteral("end_beat"), property(QStringLiteral("number"), QStringLiteral("Where it ends."))},
                {QStringLiteral("start_bar"), startBar},
                {QStringLiteral("end_bar"), endBar},
                {QStringLiteral("notes"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), QJsonObject{{QStringLiteral("anyOf"), QJsonArray{noteRow, note}}}}}},
                {QStringLiteral("muted"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                             {QStringLiteral("description"), QStringLiteral("The notes (their places in notes, from 0) that don't sound.")}}}},
               {QStringLiteral("track_id"), QStringLiteral("notes")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->writeMidiClip(arguments); },
    });
    addTool({
        QStringLiteral("duplicate_track"),
        QStringLiteral("Duplicate Track"),
        QStringLiteral("A copy of a track (a group with what is in it) right after it, as Ctrl+D makes one: the same "
                       "instrument and effects, clips, mixer and sends. Its trackId. Write a new part on a copy to "
                       "leave the user's as it is.") + edits,
        schema({{QStringLiteral("track_id"), trackId}}, {QStringLiteral("track_id")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->duplicateTrack(arguments); },
    });

    // Notes where they are, automation, tracks and devices.
    addEditTools(protocol, session, edits);

    // The agent's history.
    const QJsonObject ids{{QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                          {QStringLiteral("description"), QStringLiteral("Changes' ids, from list_agent_changes.")}};
    addTool({
        QStringLiteral("list_agent_changes"),
        QStringLiteral("List Agent Changes"),
        QStringLiteral("What agents changed in this project since it opened, the first first: each change's id, what "
                       "it did, the tool, when, the tracks it touched, and whether it is in effect (applied) or was "
                       "rewound (by the user too)."),
        schema(),
        true,
        [tools](const QJsonObject&) { return tools->listAgentChanges(); },
    });
    // (Rewinding works with edits off: restoring checks for itself.)
    protocol.addTool({
        QStringLiteral("rewind_agent_changes"),
        QStringLiteral("Rewind Agent Changes"),
        QStringLiteral("Takes back agents' changes (the last first), or all of them, leaving the user's own edits as "
                       "they are. One the user changed the same since can't be: `stopped` says which and why (the "
                       "user's work comes first). With restore, puts rewound changes back instead (the first first), "
                       "unless the user changed the same since."),
        schema({{QStringLiteral("change_ids"), ids},
                {QStringLiteral("all"), property(QStringLiteral("boolean"), QStringLiteral("Every change in effect (not with restore)."))},
                {QStringLiteral("restore"), property(QStringLiteral("boolean"), QStringLiteral("Put these rewound changes back instead."))}}),
        false,
        [tools](const QJsonObject& arguments) { return tools->rewindAgentChanges(arguments); },
        true,
    });

    // Resources: the project as documents a client can read (and show) without a tool call.
    protocol.addResource({
        QStringLiteral("substation://project"),
        QStringLiteral("project"),
        QStringLiteral("Project"),
        QStringLiteral("The project open in SUBstation, as get_project gives it: tempo, key, time signature, the "
                       "user's selection and their last edits."),
        QStringLiteral("application/json"),
        [tools] {
            return QString::fromUtf8(QJsonDocument(tools->getProject().structured.toObject()).toJson(QJsonDocument::Compact));
        },
    });
    protocol.addResource({
        QStringLiteral("substation://arrangement"),
        QStringLiteral("arrangement"),
        QStringLiteral("Arrangement"),
        QStringLiteral("The song's arrangement at a glance, as get_arrangement_map gives it: where each track plays, "
                       "a character a bar, and the sections guessed from it."),
        QStringLiteral("text/plain"),
        [tools] { return tools->getArrangementMap({}).text; },
    });

    // Prompts: requests the user picks in the agent (Claude Code: /mcp__substation__write_part...).
    protocol.addPrompt({
        QStringLiteral("write_part"),
        QStringLiteral("Write a Part"),
        QStringLiteral("Write a part over the bars selected in SUBstation's arrangement."),
        {{QStringLiteral("request"), QStringLiteral("What to write (\"a funkier bass line\", \"strings that follow the chords\")."), true}},
        [](const QJsonObject& arguments) {
            return QStringLiteral(
                       "In SUBstation I selected part of my song. Write this there: %1\n\n"
                       "Call get_selection_context first: it has the selected bars, each track there with its notes and "
                       "what it is, and the chords. If you need more of the song around it, get_arrangement_map and "
                       "get_notes. Write on a copy of the track (duplicate_track) unless I asked you to change mine, "
                       "with write_midi_clip over the selection's bars, in the song's key and following its chords. "
                       "Then tell me what you wrote, on which track, and how it fits.")
                .arg(arguments.value(QStringLiteral("request")).toString().trimmed());
        },
    });
    protocol.addPrompt({
        QStringLiteral("describe_song"),
        QStringLiteral("Describe the Song"),
        QStringLiteral("What the song open in SUBstation is: its sections, its tracks, its chords."),
        {},
        [](const QJsonObject&) {
            return QStringLiteral(
                "Describe the song I have open in SUBstation, without changing anything. Call get_project, then "
                "get_arrangement_map for its sections (where tracks come in and drop out), list_tracks for what each "
                "track is, and get_harmony over each section's bars for its chords. Tell me its form (intro, verse, "
                "chorus... by bars), what plays in each part, the key and the progressions, and anything that stands "
                "out: a section that is thin or crowded, tracks that never play, parts that repeat unchanged.");
        },
    });
    protocol.addPrompt({
        QStringLiteral("mix_review"),
        QStringLiteral("Review the Mix"),
        QStringLiteral("Suggestions for the mix of the song open in SUBstation, made only once you agree."),
        {{QStringLiteral("focus"), QStringLiteral("What to look at most (\"the low end\", \"the vocals\"); none: everything."), false}},
        [](const QJsonObject& arguments) {
            const QString focus = arguments.value(QStringLiteral("focus")).toString().trimmed();
            return QStringLiteral(
                       "Review the mix of the song I have open in SUBstation%1. Call list_tracks for each track's level, "
                       "pan, sends and what it is, get_arrangement_map for what plays together, and get_track (and "
                       "get_device_parameters) for the effects on the tracks that matter. You can't hear it: reason "
                       "from what the tracks are, their levels, pans, effects and what overlaps. Suggest changes, "
                       "each with why, and make them (set_track, set_device_parameters) only once I say which.")
                .arg(focus.isEmpty() ? QString() : QStringLiteral(", above all %1").arg(focus));
        },
    });
}

}  // namespace sub::app::mcp
