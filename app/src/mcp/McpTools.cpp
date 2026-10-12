#include "mcp/McpTools.h"

#include "audio/EngineBridge.h"
#include "intelligence/Harmony.h"
#include "intelligence/TrackLabels.h"
#include "intelligence/TrackSearch.h"
#include "mcp/McpProtocol.h"
#include "model/Devices.h"
#include "model/Project.h"
#include "session/Selection.h"
#include "session/Session.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace sub::app::mcp {

namespace labels = intelligence::labels;

namespace {

QJsonArray strings(const std::vector<std::string>& values) {
    QJsonArray out;
    for (const std::string& value : values) out.append(QString::fromStdString(value));
    return out;
}

QJsonObject schema(const QJsonObject& properties = {}, const QStringList& required = {}) {
    QJsonObject out{{QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("properties"), properties},
                    {QStringLiteral("additionalProperties"), false}};
    if (!required.isEmpty()) out.insert(QStringLiteral("required"), QJsonArray::fromStringList(required));
    return out;
}

QJsonObject property(const QString& type, const QString& description) {
    return {{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
}

QJsonValue orNull(const QString& value) { return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value); }

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

class Tools {
public:
    explicit Tools(Session& session) : session_(session) {}

    const Project& project() const { return *session_.project(); }

    // What every track says of itself, in a list or alone.
    QJsonObject summary(const Track& track) const {
        const labels::TrackLabel* label = session_.trackLabels()->find(track.id);
        QJsonArray devices;
        for (const Device& device : track.devices) devices.append(deviceName(device));
        QJsonObject out{
            {QStringLiteral("id"), track.id},
            {QStringLiteral("name"), track.name},
            {QStringLiteral("kind"), track.kind},
            {QStringLiteral("parent"), track.parent ? QJsonValue(*track.parent) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("label"), label ? orNull(QString::fromStdString(label->label)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("family"), label ? orNull(QString::fromStdString(label->family)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("role"), label ? orNull(QString::fromStdString(label->role)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("traits"), label ? strings(label->traits) : QJsonArray()},
            {QStringLiteral("volumeDb"), track.volumeDb},
            {QStringLiteral("pan"), track.pan},
            {QStringLiteral("mute"), track.mute},
            {QStringLiteral("solo"), track.solo},
            {QStringLiteral("frozen"), track.frozen.has_value()},
            {QStringLiteral("devices"), devices},
        };
        if (track.hasClips()) out.insert(QStringLiteral("clipCount"), static_cast<int>(track.clips.size()));
        return out;
    }

    QJsonObject device(const QString& trackId, const Device& device) const {
        QJsonObject out{{QStringLiteral("id"), device.id},
                        {QStringLiteral("kind"), device.kind},
                        {QStringLiteral("name"), deviceName(device)},
                        {QStringLiteral("enabled"), device.enabled}};
        if (device.isPlugin() && device.plugin) {
            out.insert(QStringLiteral("plugin"), QJsonObject{{QStringLiteral("name"), device.plugin->name},
                                                             {QStringLiteral("vendor"), device.plugin->vendor},
                                                             {QStringLiteral("instrument"), device.plugin->instrument}});
            const std::optional<QString> preset =
                session_.bridge() ? session_.bridge()->pluginPresetName(trackId, device.id) : std::nullopt;
            out.insert(QStringLiteral("loaded"), preset.has_value());
            if (preset && !preset->isEmpty()) out.insert(QStringLiteral("preset"), *preset);
        }
        if (device.isRack()) {
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

    QJsonObject detail(const Track& track) const {
        QJsonObject out = summary(track);
        const labels::TrackLabel* label = session_.trackLabels()->find(track.id);
        out.insert(QStringLiteral("details"), label ? strings(label->details) : QJsonArray());
        QJsonArray devices;
        for (const Device& d : track.devices) devices.append(device(track.id, d));
        out.insert(QStringLiteral("devices"), devices);
        if (track.hasClips()) {
            const double tempo = project().tempo();
            QJsonArray clips;
            for (const Clip& clip : track.clips) {
                QJsonObject c{{QStringLiteral("id"), clip.id},
                              {QStringLiteral("kind"), clip.isMidi() ? QStringLiteral("midi") : QStringLiteral("audio")},
                              {QStringLiteral("startBeat"), clip.startBeat},
                              {QStringLiteral("endBeat"), clip.endBeat(tempo)},
                              {QStringLiteral("active"), clip.plays()}};
                if (!clip.name.isEmpty()) c.insert(QStringLiteral("name"), clip.name);
                if (clip.isAudio()) c.insert(QStringLiteral("file"), clip.path);
                if (clip.isMidi()) c.insert(QStringLiteral("noteCount"), static_cast<int>(clip.notes.size()));
                clips.append(c);
            }
            out.insert(QStringLiteral("clips"), clips);
        }
        QJsonArray sends;
        for (auto it = track.sends.constBegin(); it != track.sends.constEnd(); ++it) {
            const Track* ret = project().findTrack(it.key());
            sends.append(QJsonObject{{QStringLiteral("returnId"), it.key()},
                                     {QStringLiteral("returnName"), ret ? ret->name : QString()},
                                     {QStringLiteral("levelDb"), it->levelDb},
                                     {QStringLiteral("preFader"), it->preFader}});
        }
        out.insert(QStringLiteral("sends"), sends);
        if (!track.isMaster()) out.insert(QStringLiteral("output"), output(track.output));
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
            {QStringLiteral("tempo"), p.tempo()},
            {QStringLiteral("timeSignature"), p.timeSignatureText()},
            {QStringLiteral("key"), key ? QJsonValue(key->label()) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("keyInferred"), key.has_value() && harmony.keyInferred()},
            {QStringLiteral("loop"), QJsonObject{{QStringLiteral("enabled"), p.loopEnabled()},
                                                {QStringLiteral("startBeat"), p.loopStart()},
                                                {QStringLiteral("endBeat"), p.loopEnd()}}},
            {QStringLiteral("endBeat"), p.endBeat()},
            {QStringLiteral("trackCount"), static_cast<int>(p.tracks().size())},
            {QStringLiteral("returnCount"), static_cast<int>(p.returns().size())},
            {QStringLiteral("selectedTrackId"), orNull(selection.trackId())},
            {QStringLiteral("selectedTrackIds"), QJsonArray::fromStringList(selection.trackIds())},
        };
        return {out, {}, false};
    }

    ToolResult listTracks() const {
        QJsonArray tracks;
        for (const Track* track : project().allTracks()) tracks.append(summary(*track));
        return {QJsonObject{{QStringLiteral("tracks"), tracks}}, {}, false};
    }

    ToolResult getTrack(const QJsonObject& arguments) const {
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        if (id.isEmpty()) return ToolResult::failure(QStringLiteral("track_id is required: a track's id from list_tracks."));
        const Track* track = project().findTrack(id);
        if (!track) {
            return ToolResult::failure(
                QStringLiteral("No track has the id \"%1\": list_tracks or find_tracks give the ids.").arg(id));
        }
        return {detail(*track), {}, false};
    }

    ToolResult findTracks(const QJsonObject& arguments) const {
        const QString query = arguments.value(QStringLiteral("query")).toString();
        if (query.trimmed().isEmpty()) return ToolResult::failure(QStringLiteral("query is required: words to look for."));
        QJsonArray found;
        for (const QString& id : TrackSearch::find(project(), *session_.trackLabels(), query)) {
            const Track& track = project().track(id);
            found.append(QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("name"), track.name},
                                     {QStringLiteral("label"), orNull(session_.trackLabels()->label(id))}});
        }
        return {QJsonObject{{QStringLiteral("tracks"), found}}, {}, false};
    }

    ToolResult getHarmony(const QJsonObject& arguments) const {
        const Harmony& harmony = *session_.harmony();
        const double from = arguments.value(QStringLiteral("start_beat")).toDouble(0.0);
        const double to = arguments.value(QStringLiteral("end_beat")).toDouble(std::numeric_limits<double>::infinity());
        if (to <= from) return ToolResult::failure(QStringLiteral("end_beat must come after start_beat."));
        QJsonArray chords;
        for (const Harmony::ChordSpan& span : harmony.chords()) {
            if (span.end <= from || span.start >= to) continue;
            chords.append(QJsonObject{{QStringLiteral("startBeat"), span.start},
                                      {QStringLiteral("endBeat"), span.end},
                                      {QStringLiteral("chord"), QString::fromStdString(span.chord.name())}});
        }
        const std::optional<Key> key = harmony.key();
        return {QJsonObject{{QStringLiteral("key"), key ? QJsonValue(key->label()) : QJsonValue(QJsonValue::Null)},
                            {QStringLiteral("keyInferred"), key.has_value() && harmony.keyInferred()},
                            {QStringLiteral("tracks"), QJsonArray::fromStringList(Harmony::heardTracks(project()))},
                            {QStringLiteral("chords"), chords}},
                {},
                false};
    }

private:
    Session& session_;
};

}  // namespace

void addSessionTools(Protocol& protocol, Session& session) {
    auto tools = std::make_shared<Tools>(session);
    protocol.addTool({
        QStringLiteral("get_project"),
        QStringLiteral("Get Project"),
        QStringLiteral("The project open in SUBstation: its name and file, tempo (BPM), time signature, key (the "
                       "project's, else the one its notes suggest: keyInferred), loop, where its last clip ends "
                       "(endBeat), how many tracks and returns it has, and the tracks the user has selected. "
                       "Start here."),
        schema(),
        true,
        [tools](const QJsonObject&) { return tools->getProject(); },
    });
    protocol.addTool({
        QStringLiteral("list_tracks"),
        QStringLiteral("List Tracks"),
        QStringLiteral("Every track in the arrangement's order (top to bottom; groups before what they hold, "
                       "`parent` naming a track's group), then the return tracks, then the master. Each says what it "
                       "is: its label (a few words, e.g. \"Washed Out Serum Pluck\", worked out from its name, "
                       "presets, files, notes and effects), family and role (\"brass\", \"trumpet\"), traits, and its "
                       "mixer (volumeDb, pan from -1 to 1, mute, solo), devices and clip count. Use get_track for one "
                       "track in full."),
        schema(),
        true,
        [tools](const QJsonObject&) { return tools->listTracks(); },
    });
    protocol.addTool({
        QStringLiteral("get_track"),
        QStringLiteral("Get Track"),
        QStringLiteral("One track (or return, or the master: \"master\") in full: what list_tracks says, plus what "
                       "its label was made from (details: its instrument and preset, notes, files, effects and their "
                       "amounts), its devices (plug-ins with their vendor and loaded preset; racks with their chains), "
                       "its clips (beats, files, note counts; inactive ones too), its sends to returns and where its "
                       "output goes."),
        schema({{QStringLiteral("track_id"), property(QStringLiteral("string"), QStringLiteral("The track's id, from list_tracks or find_tracks."))}},
               {QStringLiteral("track_id")}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getTrack(arguments); },
    });
    protocol.addTool({
        QStringLiteral("find_tracks"),
        QStringLiteral("Find Tracks"),
        QStringLiteral("Tracks matching words, the best first, as SUBstation's find bar (Q) finds them: names "
                       "having the words at the start of their words, then labels, then names having them anywhere, "
                       "then what the labels were made from (a preset's or a file's name). \"horn\" finds a track "
                       "called Horn Stab, then an orchestra playing a horns preset. Every word must match."),
        schema({{QStringLiteral("query"), property(QStringLiteral("string"), QStringLiteral("Words to look for (any case)."))}},
               {QStringLiteral("query")}),
        true,
        [tools](const QJsonObject& arguments) { return tools->findTracks(arguments); },
    });
    protocol.addTool({
        QStringLiteral("get_harmony"),
        QStringLiteral("Get Harmony"),
        QStringLiteral("The song's chords and key, inferred from the notes its MIDI tracks play (those heard: not "
                       "muted, not drums; `tracks` lists them). Chords are spans of beats with names such as \"Am7\" "
                       "or \"C/E\". Give start_beat and end_beat for part of the song."),
        schema({{QStringLiteral("start_beat"), property(QStringLiteral("number"), QStringLiteral("From this beat (default: the start)."))},
                {QStringLiteral("end_beat"), property(QStringLiteral("number"), QStringLiteral("Up to this beat (default: the end)."))}}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getHarmony(arguments); },
    });
}

}  // namespace sub::app::mcp
