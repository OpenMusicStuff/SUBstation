#include "mcp/McpTools.h"

#include "audio/EngineBridge.h"
#include "editor/AgentHistory.h"
#include "editor/ProjectEditor.h"
#include "intelligence/Harmony.h"
#include "intelligence/TrackLabels.h"
#include "intelligence/TrackSearch.h"
#include "mcp/McpProtocol.h"
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

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

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

// A number as the model reads it: no more digits than it means.
QString number(double value, int decimals = 2) {
    QString text = QString::number(value, 'f', decimals);
    if (text.contains(u'.')) {
        while (text.endsWith(u'0')) text.chop(1);
        if (text.endsWith(u'.')) text.chop(1);
    }
    return text == u"-0" ? QStringLiteral("0") : text;
}

// Whether every word of `query` is in `text` (any case).
bool matches(const QString& text, const QStringList& words) {
    return std::all_of(words.begin(), words.end(), [&](const QString& word) { return text.contains(word, Qt::CaseInsensitive); });
}

// A beat as the model reads it: to a ten-thousandth (a 1/2560th note), not 579.7826893194218.
double beat(double value) { return std::round(value * 1e4) / 1e4; }

// What each row of notes holds (get_notes, get_selection_context; write_midi_clip takes them so too).
const QJsonArray kNoteFields{QStringLiteral("startBeat"), QStringLiteral("length"), QStringLiteral("pitch"),
                             QStringLiteral("velocity")};

const QString kNoEdits = QStringLiteral(
    "Agents may not edit this project: the user turned edits off (Preferences › Agents). Tell them what you would "
    "change instead.");

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
            {QStringLiteral("insertBeat"), selection.insertBeat()},
        };
        // The time selection (a stretch of beats over tracks the user selected), and the clips in it.
        if (const auto& range = selection.timeRange()) {
            QJsonArray clips;
            for (const ClipRef& ref : selection.clips()) {
                clips.append(QJsonObject{{QStringLiteral("trackId"), ref.trackId}, {QStringLiteral("clipId"), ref.clipId}});
            }
            out.insert(QStringLiteral("timeSelection"),
                       QJsonObject{{QStringLiteral("startBeat"), range->start},
                                   {QStringLiteral("endBeat"), range->end},
                                   {QStringLiteral("trackIds"), QJsonArray::fromStringList(selection.rangeTrackIds())},
                                   {QStringLiteral("clips"), clips}});
        } else {
            out.insert(QStringLiteral("timeSelection"), QJsonValue(QJsonValue::Null));
        }
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

    // The key and the chords between two beats; with `withTracks`, the tracks
    // they were heard from (id and name: what an agent rarely needs, so asked for).
    QJsonObject harmonyJson(double from, double to, bool withTracks) const {
        const Harmony& harmony = *session_.harmony();
        QJsonArray chords;
        for (const Harmony::ChordSpan& span : harmony.chords()) {
            if (span.end <= from || span.start >= to) continue;
            chords.append(QJsonObject{{QStringLiteral("startBeat"), beat(span.start)},
                                      {QStringLiteral("endBeat"), beat(span.end)},
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
        const double from = arguments.value(QStringLiteral("start_beat")).toDouble(0.0);
        const double to = arguments.value(QStringLiteral("end_beat")).toDouble(std::numeric_limits<double>::infinity());
        if (to <= from) return ToolResult::failure(QStringLiteral("end_beat must come after start_beat."));
        return {harmonyJson(from, to, arguments.value(QStringLiteral("include_tracks")).toBool(false)), {}, false};
    }

    // --- Device parameters ---

    // A device's parameter: what the engine says of it, and its id on the
    // device ("cutoff", a plug-in's number, "device:on", "macro1", "chain:<id>:volume").
    struct Param {
        ParamSpec spec;
        QString id;
    };

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
            return {{}, {}, QStringLiteral("No track has the id \"%1\": list_tracks or find_tracks give the ids.").arg(trackId)};
        }
        const Device* device = sub::app::findDevice(track->devices, deviceId);
        if (!device) {
            return {*track, {}, QStringLiteral("%1 has no device \"%2\": get_track lists its devices.").arg(track->name, deviceId)};
        }
        return {*track, *device, {}};
    }

    std::vector<Param> params(const QString& trackId, const Device& device) const {
        std::vector<Param> out;
        if (!session_.bridge()) return out;
        for (ParamSpec& spec : session_.bridge()->deviceParamSpecs(trackId, device)) {
            const auto target = automation::parseKey(spec.key);
            const QString id = target ? target->param : spec.key;
            out.push_back({std::move(spec), id});
        }
        return out;
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

    bool editsAllowed() const { return !session_.mcp() || session_.mcp()->editsAllowed(); }

    // Runs `edit` as one change; the result (`out`) says what it did, and its id.
    ToolResult change(const QString& text, const QString& tool, QJsonObject out,
                      const std::function<void(ProjectEditor&)>& edit) {
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
        std::optional<int> id;
        try {
            id = session_.agentChanges()->record(text, tool, edit);
        } catch (const EditError& error) {
            return ToolResult::failure(error.message());
        }
        out.insert(QStringLiteral("changeId"), id ? QJsonValue(*id) : QJsonValue(QJsonValue::Null));
        out.insert(QStringLiteral("change"), id ? text : QStringLiteral("Nothing changed: it was so already."));
        return {out, {}, false};
    }

    // What a parameter is to be set to: `value` in its units (a choice's name,
    // true or false for a switch), or `normalized` (0..1). Plain; none and why not.
    static std::pair<std::optional<double>, QString> wanted(const Param& param, const QJsonObject& item) {
        const ParamSpec& spec = param.spec;
        const QJsonValue value = item.value(QStringLiteral("value"));
        const QJsonValue normalized = item.value(QStringLiteral("normalized"));
        if (value.isUndefined() == normalized.isUndefined()) {
            return {std::nullopt, QStringLiteral("%1: give value or normalized (one of them).").arg(param.id)};
        }
        if (!normalized.isUndefined()) {
            const double n = normalized.toDouble(-1.0);
            if (!(n >= 0.0 && n <= 1.0)) return {std::nullopt, QStringLiteral("%1: normalized is 0 to 1.").arg(param.id)};
            return {spec.fromNormalized(spec.quantize(n)), {}};
        }
        if (value.isBool()) {
            if (spec.steps != 1) return {std::nullopt, QStringLiteral("%1 isn't a switch: give a number.").arg(param.id)};
            return {value.toBool() ? spec.maximum : spec.minimum, {}};
        }
        if (value.isString()) {
            const QString name = value.toString().trimmed();
            for (qsizetype i = 0; i < spec.labels.size(); ++i) {
                if (spec.labels[i].compare(name, Qt::CaseInsensitive) != 0) continue;
                const double n = spec.steps > 0 ? static_cast<double>(i) / spec.steps : 0.0;
                return {spec.fromNormalized(spec.quantize(n)), {}};
            }
            if (spec.labels.isEmpty()) {
                return {std::nullopt, QStringLiteral("%1 takes a number from %2 to %3.")
                                          .arg(param.id, number(spec.minimum), number(spec.maximum))};
            }
            return {std::nullopt, QStringLiteral("%1 has no choice \"%2\": it is one of %3.")
                                      .arg(param.id, name, spec.labels.join(QStringLiteral(", ")))};
        }
        if (!value.isDouble()) return {std::nullopt, QStringLiteral("%1: value is a number.").arg(param.id)};
        const double plain = value.toDouble();
        const double slack = (spec.maximum - spec.minimum) * 1e-9;
        if (plain < spec.minimum - slack || plain > spec.maximum + slack) {
            return {std::nullopt, QStringLiteral("%1 goes from %2 to %3%4 (asked for %5).")
                                      .arg(param.id, number(spec.minimum), number(spec.maximum),
                                           spec.unit.isEmpty() ? QString() : u' ' + spec.unit, number(plain))};
        }
        return {spec.fromNormalized(spec.quantize(spec.toNormalized(plain))), {}};
    }

    ToolResult setDeviceParameters(const QJsonObject& arguments) {
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
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

    ToolResult setMixer(const QJsonObject& arguments) {
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
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
        if (volume.isUndefined() && pan.isUndefined() && mute.isUndefined() && sends.isEmpty()) {
            return ToolResult::failure(QStringLiteral("Give volume_db, pan, mute or sends: what to change."));
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
        if (!volume.isUndefined()) what << QStringLiteral("volume to %1 dB").arg(number(volume.toDouble(), 1));
        if (!pan.isUndefined()) what << QStringLiteral("pan to %1").arg(number(pan.toDouble()));
        if (!mute.isUndefined()) what << (mute.toBool() ? QStringLiteral("mute") : QStringLiteral("unmute"));
        for (const auto& [returnId, level] : levels) {
            what << QStringLiteral("send to %1 to %2 dB").arg(project().track(returnId).name, number(level, 1));
        }
        const QString trackId = track->id;
        const QString text = QStringLiteral("Set %1 of %2").arg(what.join(QStringLiteral(", ")), track->name);
        return change(text, QStringLiteral("set_mixer"), {}, [&](ProjectEditor& editor) {
            if (!volume.isUndefined()) {
                editor.setTrackParam(trackId, TrackField::VolumeDb, std::max(volume.toDouble(), automation::kMinVolumeDb));
            }
            if (!pan.isUndefined()) editor.setTrackParam(trackId, TrackField::Pan, pan.toDouble());
            if (!mute.isUndefined()) editor.setTrackParam(trackId, TrackField::Mute, mute.toBool() ? 1.0 : 0.0);
            for (const auto& [returnId, level] : levels) editor.setSend(trackId, returnId, level);
        });
    }

    ToolResult renameTrack(const QJsonObject& arguments) {
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        const QString name = arguments.value(QStringLiteral("name")).toString().trimmed();
        const Track* track = id.isEmpty() || id == kMaster ? nullptr : project().findTrack(id);
        if (!track) {
            return ToolResult::failure(
                QStringLiteral("track_id is required: a track's or a return's id from list_tracks (not the master's)."));
        }
        if (name.isEmpty()) return ToolResult::failure(QStringLiteral("name is required."));
        const QString trackId = track->id;
        return change(QStringLiteral("Rename %1 to %2").arg(track->name, name), QStringLiteral("rename_track"), {},
                      [&](ProjectEditor& editor) { editor.renameTrack(trackId, name); });
    }

    ToolResult setProject(const QJsonObject& arguments) {
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
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
    // As rows (`rows`: [startBeat, length, pitch, velocity] each, "fields"
    // naming them, "muted" listing the rows of muted ones: a fifth of the size), or
    // as objects with each note's name.
    QJsonObject notesJson(const Track& track, double from, double to, bool rows) const {
        QJsonArray notes;
        QJsonArray clips;
        QJsonArray muted;
        bool truncated = false;
        for (const Clip& clip : track.clips) {
            if (!clip.isMidi() || clip.endBeat(0.0) <= from || clip.startBeat >= to) continue;
            clips.append(QJsonObject{{QStringLiteral("id"), clip.id},
                                     {QStringLiteral("startBeat"), beat(clip.startBeat)},
                                     {QStringLiteral("endBeat"), beat(clip.endBeat(0.0))},
                                     {QStringLiteral("active"), clip.plays()}});
            for (const PlayedNote& played : clip.playedNotes()) {
                if (played.end <= from || played.start >= to) continue;
                if (notes.size() >= kMaxNotes) {
                    truncated = true;
                    break;
                }
                const bool silent = played.note.muted || !clip.plays();
                if (rows) {
                    if (silent) muted.append(static_cast<int>(notes.size()));
                    notes.append(QJsonArray{beat(played.start), beat(played.end - played.start), played.note.pitch,
                                            played.note.velocity});
                    continue;
                }
                QJsonObject note{{QStringLiteral("pitch"), played.note.pitch},
                                 {QStringLiteral("name"), notes::noteName(played.note.pitch)},
                                 {QStringLiteral("startBeat"), beat(played.start)},
                                 {QStringLiteral("length"), beat(played.end - played.start)},
                                 {QStringLiteral("velocity"), played.note.velocity}};
                if (silent) note.insert(QStringLiteral("muted"), true);
                notes.append(note);
            }
        }
        QJsonObject out{{QStringLiteral("clips"), clips}};
        if (rows) out.insert(QStringLiteral("fields"), kNoteFields);
        out.insert(QStringLiteral("notes"), notes);
        if (!muted.isEmpty()) out.insert(QStringLiteral("muted"), muted);
        if (truncated) {
            out.insert(QStringLiteral("truncated"),
                       QStringLiteral("Only the first %1 notes: ask for a shorter stretch (start_beat, end_beat).").arg(kMaxNotes));
        }
        return out;
    }

    ToolResult getNotes(const QJsonObject& arguments) const {
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        const Track* track = id.isEmpty() ? nullptr : project().findTrack(id);
        if (!track) return ToolResult::failure(QStringLiteral("track_id is required: a MIDI track's id from list_tracks."));
        if (!track->isMidi()) return ToolResult::failure(QStringLiteral("%1 isn't a MIDI track: it has no notes.").arg(track->name));
        const double from = arguments.value(QStringLiteral("start_beat")).toDouble(0.0);
        const double to = arguments.value(QStringLiteral("end_beat")).toDouble(std::numeric_limits<double>::infinity());
        if (to <= from) return ToolResult::failure(QStringLiteral("end_beat must come after start_beat."));
        const QString format = arguments.value(QStringLiteral("format")).toString(QStringLiteral("rows"));
        if (format != u"rows" && format != u"objects") return ToolResult::failure(QStringLiteral("format is \"rows\" or \"objects\"."));
        QJsonObject out = notesJson(*track, from, to, format == u"rows");
        out.insert(QStringLiteral("trackId"), track->id);
        return {out, {}, false};
    }

    // --- The selection, with what a part written there needs ---

    ToolResult getSelectionContext() const {
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
                const QJsonObject notes = notesJson(*track, range->start, range->end, true);
                for (auto it = notes.constBegin(); it != notes.constEnd(); ++it) out.insert(it.key(), it.value());
            }
            tracks.append(out);
        }
        QJsonObject out{{QStringLiteral("startBeat"), beat(range->start)},
                        {QStringLiteral("endBeat"), beat(range->end)},
                        {QStringLiteral("selectedClips"), clips},
                        {QStringLiteral("tracks"), tracks},
                        {QStringLiteral("harmony"), harmonyJson(range->start, range->end, false)},
                        {QStringLiteral("tempo"), project().tempo()},
                        {QStringLiteral("timeSignature"), project().timeSignatureText()}};
        if (skipped > 0) {
            out.insert(QStringLiteral("moreTracks"),
                       QStringLiteral("%1 more tracks are in the selection: get_track and get_notes give them.").arg(skipped));
        }
        return {out, {}, false};
    }

    ToolResult writeMidiClip(const QJsonObject& arguments) {
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
        const QString id = arguments.value(QStringLiteral("track_id")).toString();
        const Track* track = id.isEmpty() ? nullptr : project().findTrack(id);
        if (!track) return ToolResult::failure(QStringLiteral("track_id is required: a MIDI track's id from list_tracks."));
        if (!track->isMidi()) {
            return ToolResult::failure(QStringLiteral("%1 isn't a MIDI track: duplicate_track a MIDI track, or pick one.").arg(track->name));
        }
        const double start = arguments.value(QStringLiteral("start_beat")).toDouble(-1.0);
        const double end = arguments.value(QStringLiteral("end_beat")).toDouble(-1.0);
        if (start < 0.0 || end - start < edits::kMinMidiClipBeats) {
            return ToolResult::failure(QStringLiteral("start_beat (0 or later) and end_beat (after it) are required."));
        }
        const QJsonArray items = arguments.value(QStringLiteral("notes")).toArray();
        if (items.size() > kMaxNotes) return ToolResult::failure(QStringLiteral("At most %1 notes at once.").arg(kMaxNotes));
        std::vector<Note> notes;
        for (const QJsonValue& entry : items) {
            // A row, as get_notes gives them ([start_beat, length, pitch, velocity]), or an object.
            const QJsonArray row = entry.toArray();
            const QJsonObject item = entry.toObject();
            if (entry.isArray() && (row.size() < 3 || row.size() > 4)) {
                return ToolResult::failure(QStringLiteral("A note row is [start_beat, length, pitch, velocity] (velocity may be left out)."));
            }
            const int pitch = entry.isArray() ? row.at(2).toInt(-1) : item.value(QStringLiteral("pitch")).toInt(-1);
            const double at = entry.isArray() ? row.at(0).toDouble(-1.0) : item.value(QStringLiteral("start_beat")).toDouble(-1.0);
            const double length = entry.isArray() ? row.at(1).toDouble(0.0) : item.value(QStringLiteral("length")).toDouble(0.0);
            const int velocity = entry.isArray() ? (row.size() > 3 ? row.at(3).toInt(0) : 100)
                                                 : item.value(QStringLiteral("velocity")).toInt(100);
            if (pitch < 0 || pitch > 127) return ToolResult::failure(QStringLiteral("A note's pitch is 0 to 127 (60 is C3)."));
            if (at < start - edits::kEps || at >= end) {
                return ToolResult::failure(
                    QStringLiteral("A note starts at beat %1, outside the clip (%2 to %3): notes' start_beat is on the "
                                   "song's timeline.").arg(number(at, 3), number(start, 3), number(end, 3)));
            }
            if (!(length > 0.0)) return ToolResult::failure(QStringLiteral("A note's length is more than 0 beats."));
            if (velocity < 1 || velocity > 127) return ToolResult::failure(QStringLiteral("A note's velocity is 1 to 127."));
            Note note;
            note.pitch = pitch;
            note.start = std::max(0.0, at - start);  // (the clip's own beats)
            note.length = length;
            note.velocity = velocity;
            notes.push_back(note);
        }
        std::sort(notes.begin(), notes.end(),
                  [](const Note& a, const Note& b) { return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch); });
        const Clip clip = Clip::midi(newId(), QString(), start, end - start, 0.0, std::move(notes));
        const QString trackId = track->id;
        const QString text = QStringLiteral("Write %1 notes on %2, beats %3 to %4")
                                 .arg(QString::number(clip.notes.size()), track->name, number(start, 3), number(end, 3));
        return change(text, QStringLiteral("write_midi_clip"),
                      {{QStringLiteral("trackId"), trackId}, {QStringLiteral("clipId"), clip.id}},
                      [&](ProjectEditor& editor) {
                          std::vector<Clip> clips = editor.project()->track(trackId).clips;
                          clips.push_back(clip);
                          // (It wins where it overlaps: what was there is trimmed, split or goes.)
                          editor.commitClips(text, {{trackId, edits::resolveOverlaps(clips, {clip.id}, project().tempo())}});
                      });
    }

    ToolResult duplicateTrack(const QJsonObject& arguments) {
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
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

    ToolResult rewindAgentChanges(const QJsonObject& arguments) {
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
        if (!editsAllowed()) return ToolResult::failure(kNoEdits);
        AgentHistory& history = *session_.agentChanges();
        auto [ids, why] = changeIds(arguments);
        if (!ids) return ToolResult::failure(why);
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
                       "muted, not drums). Chords are spans of beats with names such as \"Am7\" or \"C/E\". Give "
                       "start_beat and end_beat for part of the song; include_tracks for the tracks they were heard "
                       "from."),
        schema({{QStringLiteral("start_beat"), property(QStringLiteral("number"), QStringLiteral("From this beat (default: the start)."))},
                {QStringLiteral("end_beat"), property(QStringLiteral("number"), QStringLiteral("Up to this beat (default: the end)."))},
                {QStringLiteral("include_tracks"),
                 property(QStringLiteral("boolean"), QStringLiteral("Also the tracks the chords were heard from (id, name). Default false."))}}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getHarmony(arguments); },
    });
    const QJsonObject trackId = property(QStringLiteral("string"), QStringLiteral("The track's id, from list_tracks or find_tracks."));
    const QJsonObject deviceId = property(QStringLiteral("string"), QStringLiteral("The device's id, from get_track (racks' devices too)."));
    protocol.addTool({
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
    protocol.addTool({
        QStringLiteral("get_notes"),
        QStringLiteral("Get Notes"),
        QStringLiteral("The notes a MIDI track plays, on the song's timeline, and its clips there. As rows by "
                       "default: [startBeat, length, pitch, velocity] (beats; pitch 0..127, 60 is C3; velocity "
                       "1..127), `fields` naming them, `muted` listing the rows of notes that don't sound; "
                       "write_midi_clip takes rows the same. format \"objects\" gives each note as an object with its "
                       "name (\"G#3\"). Give start_beat and end_beat for part of the song."),
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("start_beat"), property(QStringLiteral("number"), QStringLiteral("From this beat (default: the start)."))},
                {QStringLiteral("end_beat"), property(QStringLiteral("number"), QStringLiteral("Up to this beat (default: the end)."))},
                {QStringLiteral("format"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                             {QStringLiteral("enum"), QJsonArray{QStringLiteral("rows"), QStringLiteral("objects")}},
                             {QStringLiteral("description"), QStringLiteral("\"rows\" (default) or \"objects\".")}}}},
               {QStringLiteral("track_id")}),
        true,
        [tools](const QJsonObject& arguments) { return tools->getNotes(arguments); },
    });
    protocol.addTool({
        QStringLiteral("get_selection_context"),
        QStringLiteral("Get Selection Context"),
        QStringLiteral("Everything for working on what the user selected in the arrangement, in one call: the time "
                       "selection's beats, the selected clips, each track in it (what get_track says of it in short: "
                       "label, instrument and effects, mixer) with a MIDI track's notes there as rows (as get_notes "
                       "gives them), the key and chords there, the tempo and time signature. Start here when the "
                       "user asks for something about \"this\", \"the selection\" or \"what I selected\". Fails, "
                       "saying so, when nothing is selected in time."),
        schema(),
        true,
        [tools](const QJsonObject&) { return tools->getSelectionContext(); },
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
    protocol.addTool({
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
    protocol.addTool({
        QStringLiteral("set_mixer"),
        QStringLiteral("Set Mixer"),
        QStringLiteral("A track's (a return's, the master's) mixer: volume (dB, up to +6; -70 is silence), pan (-1 "
                       "left to 1 right), mute (its activator; not the master's), and its sends' levels. Give only "
                       "what changes.") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("volume_db"), property(QStringLiteral("number"), QStringLiteral("Its volume in dB."))},
                {QStringLiteral("pan"), property(QStringLiteral("number"), QStringLiteral("-1 (left) to 1 (right)."))},
                {QStringLiteral("mute"), property(QStringLiteral("boolean"), QStringLiteral("Muted (its activator off)."))},
                {QStringLiteral("sends"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), send}}}},
               {QStringLiteral("track_id")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->setMixer(arguments); },
        true,
    });
    protocol.addTool({
        QStringLiteral("rename_track"),
        QStringLiteral("Rename Track"),
        QStringLiteral("Names a track or a return. A # in the name is the track's number (\"# Keys\" shows as "
                       "\"8 Keys\").") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("name"), property(QStringLiteral("string"), QStringLiteral("Its new name."))}},
               {QStringLiteral("track_id"), QStringLiteral("name")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->renameTrack(arguments); },
        true,
    });
    protocol.addTool({
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
    const QJsonObject note{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("pitch"), property(QStringLiteral("integer"), QStringLiteral("MIDI note number, 0..127 (60 is C3)."))},
                     {QStringLiteral("start_beat"), property(QStringLiteral("number"), QStringLiteral("Where it starts on the song's timeline, inside the clip."))},
                     {QStringLiteral("length"), property(QStringLiteral("number"), QStringLiteral("How long, in beats."))},
                     {QStringLiteral("velocity"), property(QStringLiteral("integer"), QStringLiteral("1..127 (default 100)."))}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("pitch"), QStringLiteral("start_beat"), QStringLiteral("length")}},
        {QStringLiteral("additionalProperties"), false}};
    const QJsonObject noteRow{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
        {QStringLiteral("minItems"), 3},
        {QStringLiteral("maxItems"), 4},
        {QStringLiteral("description"), QStringLiteral("[start_beat, length, pitch, velocity]: velocity may be left out (100).")}};
    protocol.addTool({
        QStringLiteral("write_midi_clip"),
        QStringLiteral("Write MIDI Clip"),
        QStringLiteral("Puts a new MIDI clip from start_beat to end_beat on a MIDI track, playing these notes (their "
                       "start_beat on the song's timeline, as get_notes gives them), each a row [start_beat, length, "
                       "pitch, velocity] (a fifth of the size) or an object. It replaces whatever was there "
                       "on that track, as a clip placed by the user does: clips under it are trimmed, split or go. "
                       "To keep the user's part, write on a copy (duplicate_track) and let them compare. A part for "
                       "the user's time selection: its startBeat and endBeat (get_project).") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("start_beat"), property(QStringLiteral("number"), QStringLiteral("Where the clip starts."))},
                {QStringLiteral("end_beat"), property(QStringLiteral("number"), QStringLiteral("Where it ends."))},
                {QStringLiteral("notes"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), QJsonObject{{QStringLiteral("anyOf"), QJsonArray{noteRow, note}}}}}}},
               {QStringLiteral("track_id"), QStringLiteral("start_beat"), QStringLiteral("end_beat"), QStringLiteral("notes")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->writeMidiClip(arguments); },
    });
    protocol.addTool({
        QStringLiteral("duplicate_track"),
        QStringLiteral("Duplicate Track"),
        QStringLiteral("A copy of a track (a group with what is in it) right after it, as Ctrl+D makes one: the same "
                       "instrument and effects, clips, mixer and sends. Its trackId. Write a new part on a copy to "
                       "leave the user's as it is.") + edits,
        schema({{QStringLiteral("track_id"), trackId}}, {QStringLiteral("track_id")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->duplicateTrack(arguments); },
    });

    // The agent's history.
    const QJsonObject ids{{QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                          {QStringLiteral("description"), QStringLiteral("Changes' ids, from list_agent_changes.")}};
    protocol.addTool({
        QStringLiteral("list_agent_changes"),
        QStringLiteral("List Agent Changes"),
        QStringLiteral("What agents changed in this project since it opened, the first first: each change's id, what "
                       "it did, the tool, when, the tracks it touched, and whether it is in effect (applied) or was "
                       "rewound (by the user too)."),
        schema(),
        true,
        [tools](const QJsonObject&) { return tools->listAgentChanges(); },
    });
    protocol.addTool({
        QStringLiteral("rewind_agent_changes"),
        QStringLiteral("Rewind Agent Changes"),
        QStringLiteral("Takes back agents' changes (the last first), or all of them, leaving the user's own edits as "
                       "they are. One the user changed the same since can't be: `stopped` says which and why (the "
                       "user's work comes first)."),
        schema({{QStringLiteral("change_ids"), ids},
                {QStringLiteral("all"), property(QStringLiteral("boolean"), QStringLiteral("Every change in effect."))}}),
        false,
        [tools](const QJsonObject& arguments) { return tools->rewindAgentChanges(arguments); },
        true,
    });
    protocol.addTool({
        QStringLiteral("restore_agent_changes"),
        QStringLiteral("Restore Agent Changes"),
        QStringLiteral("Puts rewound changes back (the first first), unless the user changed the same since."),
        schema({{QStringLiteral("change_ids"), ids}}, {QStringLiteral("change_ids")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->restoreAgentChanges(arguments); },
        true,
    });
}

}  // namespace sub::app::mcp
