// The MCP server's edits beyond the first ones (McpTools.cpp): a part's
// notes changed where they are, automation drawn, tracks added, deleted,
// moved and grouped, and devices found, added, removed and moved. Each tool
// call is one change in the agents' history, as every edit is.

#include "mcp/McpEdits.h"

#include "audio/EngineBridge.h"
#include "browser/PresetIndex.h"
#include "editor/ProjectEditor.h"
#include "intelligence/Humanizer.h"
#include "io/Serialization.h"
#include "mcp/McpSupport.h"
#include "model/Automation.h"
#include "model/Devices.h"
#include "model/Errors.h"
#include "model/Notes.h"
#include "model/Project.h"
#include "plugins/PluginIndex.h"
#include "session/Session.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <memory>

namespace sub::app::mcp {

namespace {

const QString kPluginPrefix = QStringLiteral("plugin:");
const QString kPresetPrefix = QStringLiteral("preset:");

// Where a device is: the chain it is in ("": the track's own; else a rack's chain id) and its place there.
struct ChainPlace {
    QString chain;
    int index = 0;
};

std::optional<ChainPlace> placeOf(const std::vector<Device>& devices, const QString& deviceId, const QString& chain = {}) {
    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].id == deviceId) return ChainPlace{chain, static_cast<int>(i)};
        for (const Chain& inner : devices[i].chains) {
            if (auto found = placeOf(inner.devices, deviceId, inner.id)) return found;
        }
    }
    return std::nullopt;
}

bool hasChain(const std::vector<Device>& devices, const QString& chainId) {
    for (const Device& device : devices) {
        for (const Chain& chain : device.chains) {
            if (chain.id == chainId || hasChain(chain.devices, chainId)) return true;
        }
    }
    return false;
}

// What a device in the catalog is (list_devices; add_device and add_track take its id).
struct CatalogEntry {
    QString id;    // "utility", "rack", "plugin:<uid>", "preset:<path>"
    QString name;
    QString type;  // "builtin", "plugin", "preset"
    QString category;  // the built-in's ("Audio Effects"), the plug-in's ("Fx|EQ"), the device a preset is for
    QString vendor;
    bool instrument = false;
};

class EditTools : public ToolContext {
public:
    using ToolContext::ToolContext;

    // --- Finding what the arguments name ---

    // A track of the arrangement, a return or the master, as `field` names it.
    const Track* trackOf(const QJsonObject& arguments, const QString& field, QString& error) const {
        const QString id = arguments.value(field).toString();
        const Track* track = id.isEmpty() ? nullptr : project().findTrack(id);
        if (!track) {
            error = id.isEmpty() ? QStringLiteral("%1 is required: a track's id from list_tracks.").arg(field)
                                 : QStringLiteral("No track has the id \"%1\": list_tracks gives the ids.").arg(id);
        }
        return track;
    }

    // The ids `field` lists, each a track of the arrangement (returns too, with `returns`); none and why not.
    std::optional<QStringList> trackIds(const QJsonObject& arguments, const QString& field, bool returns, QString& error) const {
        QStringList ids;
        for (const QJsonValue& value : arguments.value(field).toArray()) {
            const QString id = value.toString();
            if (!(project().hasTrack(id) || (returns && project().hasReturn(id)))) {
                error = QStringLiteral("\"%1\" isn't a track%2: list_tracks gives the ids (not the master's).")
                            .arg(id, returns ? QString() : QStringLiteral(" of the arrangement (nor a return)"));
                return std::nullopt;
            }
            if (!ids.contains(id)) ids.append(id);
        }
        if (ids.isEmpty()) {
            error = QStringLiteral("%1 is required: tracks' ids from list_tracks.").arg(field);
            return std::nullopt;
        }
        return ids;
    }

    // --- Notes, changed where they are ---

    // A note to change: its clip, and its place in the clip's notes as it was.
    struct Target {
        QString clipId;
        Note original;
        Note note;  // as it is to be (the clip's own beats)
        bool deleted = false;
    };

    ToolResult editNotes(const QJsonObject& arguments) {
        QString error;
        const Track* track = trackOf(arguments, QStringLiteral("track_id"), error);
        if (!track) return ToolResult::failure(error);
        if (!track->isMidi()) return ToolResult::failure(QStringLiteral("%1 isn't a MIDI track: it has no notes.").arg(track->name));
        const Span at = span(arguments);
        if (!at.error.isEmpty()) return ToolResult::failure(at.error);
        QSet<int> pitches;
        for (const QJsonValue& value : arguments.value(QStringLiteral("pitches")).toArray()) {
            const auto pitch = pitchOf(value);
            if (!pitch) return ToolResult::failure(QStringLiteral("pitches are MIDI note numbers (60 is C3) or names (\"C3\")."));
            pitches.insert(*pitch);
        }
        QSet<QString> clipIds;
        for (const QJsonValue& value : arguments.value(QStringLiteral("clip_ids")).toArray()) {
            const QString id = value.toString();
            if (!project().findClip(track->id, id)) {
                return ToolResult::failure(QStringLiteral("%1 has no clip \"%2\": get_notes lists its clips.").arg(track->name, id));
            }
            clipIds.insert(id);
        }
        const QJsonArray operations = arguments.value(QStringLiteral("operations")).toArray();
        if (operations.isEmpty()) {
            return ToolResult::failure(QStringLiteral("operations is required: [{\"type\": \"transpose\", \"semitones\": 12}, ...]."));
        }

        // The notes to change: those its clips play there (on the timeline), of these pitches.
        std::vector<Target> targets;
        std::map<QString, const Clip*> clips;
        for (const Clip& clip : track->clips) {
            if (!clip.isMidi() || (!clipIds.isEmpty() && !clipIds.contains(clip.id))) continue;
            for (const PlayedNote& played : clip.playedNotes()) {
                if (played.start < at.from || played.start >= at.to) continue;
                if (!pitches.isEmpty() && !pitches.contains(played.note.pitch)) continue;
                targets.push_back({clip.id, played.note, played.note, false});
                clips[clip.id] = &clip;
            }
        }
        if (targets.empty()) {
            return ToolResult::failure(QStringLiteral("No notes of %1 start there%2: get_notes says where they are.")
                                           .arg(track->name, pitches.isEmpty() ? QString() : QStringLiteral(" at those pitches")));
        }
        // Timeline beats, for what works on where notes are in the song (quantizing to its grid).
        const auto shiftOf = [&](const Target& t) { return clips[t.clipId]->startBeat - clips[t.clipId]->offsetBeats; };
        const auto onTimeline = [&](const std::function<std::vector<Note>(const std::vector<Note>&)>& tool) {
            std::vector<Note> notes;
            std::vector<size_t> live;
            for (size_t i = 0; i < targets.size(); ++i) {
                if (targets[i].deleted) continue;
                Note moved = targets[i].note;
                moved.start += shiftOf(targets[i]);
                notes.push_back(moved);
                live.push_back(i);
            }
            const std::vector<Note> changed = tool(notes);  // (note for note, in order)
            for (size_t k = 0; k < live.size() && k < changed.size(); ++k) {
                Target& t = targets[live[k]];
                Note note = changed[k];
                note.start = std::max(clips[t.clipId]->offsetBeats, note.start - shiftOf(t));
                t.note = note;
            }
        };

        QStringList done;
        for (const QJsonValue& value : operations) {
            const QJsonObject op = value.toObject();
            const QString type = op.value(QStringLiteral("type")).toString();
            if (type == u"transpose") {
                const QJsonValue semitones = op.value(QStringLiteral("semitones"));
                if (!semitones.isDouble() || semitones.toDouble() != std::floor(semitones.toDouble())) {
                    return ToolResult::failure(QStringLiteral("transpose takes semitones, a whole number (12: an octave up)."));
                }
                const int by = semitones.toInt();
                for (Target& t : targets) {
                    if (t.deleted) continue;
                    if (t.note.pitch + by < 0 || t.note.pitch + by > 127) {
                        return ToolResult::failure(QStringLiteral("Transposed %1, %2 would go off the keyboard (0..127).")
                                                       .arg(number(by), notes::noteName(t.note.pitch)));
                    }
                    t.note.pitch += by;
                }
                done << QStringLiteral("transpose %1%2").arg(by > 0 ? QStringLiteral("+") : QString()).arg(by);
            } else if (type == u"shift") {
                const double beats = op.value(QStringLiteral("beats")).toDouble(0.0);
                if (beats == 0.0) return ToolResult::failure(QStringLiteral("shift takes beats (not 0): -0.25 is a 16th earlier."));
                onTimeline([beats](const std::vector<Note>& notes) { return notes::shifted(notes, beats, 0); });
                done << QStringLiteral("shift %1 beats").arg(number(beats, 4));
            } else if (type == u"quantize") {
                const double grid = op.value(QStringLiteral("grid")).toDouble(0.25);
                const double amount = op.value(QStringLiteral("amount")).toDouble(1.0);
                if (!(grid > 0.0) || !(amount > 0.0 && amount <= 1.0)) {
                    return ToolResult::failure(QStringLiteral("quantize takes grid (beats: 0.25 is a 16th) and amount (0 to 1, default 1)."));
                }
                onTimeline([grid, amount](const std::vector<Note>& notes) { return notes::quantized(notes, grid, amount); });
                done << QStringLiteral("quantize to %1 beats").arg(number(grid, 4));
            } else if (type == u"humanize_timing") {
                const double amount = op.value(QStringLiteral("amount")).toDouble(0.5);
                if (!(amount > 0.0 && amount <= 1.0)) return ToolResult::failure(QStringLiteral("humanize_timing's amount is 0 to 1."));
                QRandomGenerator rng = op.contains(QStringLiteral("seed"))
                                           ? QRandomGenerator(static_cast<quint32>(op.value(QStringLiteral("seed")).toInt()))
                                           : QRandomGenerator(QRandomGenerator::global()->generate());
                onTimeline([&rng, amount](const std::vector<Note>& notes) { return notes::humanizedTiming(notes, rng, amount); });
                done << QStringLiteral("humanize timing");
            } else if (type == u"scale_time") {
                const double factor = op.value(QStringLiteral("factor")).toDouble(0.0);
                if (!(factor > 0.0 && factor <= 16.0)) {
                    return ToolResult::failure(QStringLiteral("scale_time takes factor (2: twice as long and spread out; 0.5: half)."));
                }
                onTimeline([factor](const std::vector<Note>& notes) { return notes::timeScaled(notes, factor); });
                done << QStringLiteral("timing ×%1").arg(number(factor));
            } else if (type == u"legato") {
                // In each clip, up to its own next notes (or its end).
                for (const auto& [clipId, clip] : clips) {
                    std::vector<Note> mine;
                    std::vector<size_t> live;
                    for (size_t i = 0; i < targets.size(); ++i) {
                        if (targets[i].clipId != clipId || targets[i].deleted) continue;
                        mine.push_back(targets[i].note);
                        live.push_back(i);
                    }
                    const std::vector<Note> changed = notes::legato(mine, clip->notes, clip->windowEnd());
                    for (size_t k = 0; k < live.size() && k < changed.size(); ++k) targets[live[k]].note = changed[k];
                }
                done << QStringLiteral("legato");
            } else if (type == u"length") {
                const QJsonValue set = op.value(QStringLiteral("beats"));
                const QJsonValue scale = op.value(QStringLiteral("factor"));
                if (set.isDouble() == scale.isDouble() || (set.isDouble() && !(set.toDouble() >= notes::kMinNoteBeats)) ||
                    (scale.isDouble() && !(scale.toDouble() > 0.0))) {
                    return ToolResult::failure(QStringLiteral("length takes beats (each note that long) or factor (each note's length times it)."));
                }
                for (Target& t : targets) {
                    t.note.length = std::max(notes::kMinNoteBeats, set.isDouble() ? set.toDouble() : t.note.length * scale.toDouble());
                }
                done << QStringLiteral("length");
            } else if (type == u"velocity") {
                const QJsonValue set = op.value(QStringLiteral("set"));
                const QJsonValue change = op.value(QStringLiteral("change"));
                const QJsonValue scale = op.value(QStringLiteral("factor"));
                if (set.isDouble() + change.isDouble() + scale.isDouble() != 1) {
                    return ToolResult::failure(QStringLiteral("velocity takes one of set (1 to 127), change (+ or -) or factor (times)."));
                }
                for (Target& t : targets) {
                    const double v = set.isDouble()      ? set.toDouble()
                                     : change.isDouble() ? t.note.velocity + change.toDouble()
                                                         : t.note.velocity * scale.toDouble();
                    t.note.velocity = std::clamp(static_cast<int>(std::lround(v)), 1, 127);
                }
                done << QStringLiteral("velocity");
            } else if (type == u"humanize_velocity") {
                const double amount = op.value(QStringLiteral("amount")).toDouble(0.5);
                if (!(amount > 0.0 && amount <= 1.0)) return ToolResult::failure(QStringLiteral("humanize_velocity's amount is 0 to 1."));
                std::vector<Humanizer::Target> wanted;
                std::vector<size_t> live;
                for (size_t i = 0; i < targets.size(); ++i) {
                    if (targets[i].deleted) continue;
                    wanted.push_back({track->id, targets[i].clipId, targets[i].note});
                    live.push_back(i);
                }
                const auto velocities = session_.humanizer() ? session_.humanizer()->velocities(wanted, amount) : std::nullopt;
                if (!velocities) {
                    return ToolResult::failure(QStringLiteral("The velocity model isn't there (models/velocity.hbm): use velocity instead."));
                }
                for (size_t k = 0; k < live.size() && k < velocities->size(); ++k) targets[live[k]].note.velocity = (*velocities)[k];
                done << QStringLiteral("humanize velocity");
            } else if (type == u"mute" || type == u"unmute") {
                for (Target& t : targets) t.note.muted = type == u"mute";
                done << type;
            } else if (type == u"delete") {
                for (Target& t : targets) t.deleted = true;
                done << QStringLiteral("delete");
            } else {
                return ToolResult::failure(QStringLiteral("\"%1\" isn't an operation: transpose, shift, quantize, humanize_timing, "
                                                          "scale_time, legato, length, velocity, humanize_velocity, mute, unmute or delete.")
                                               .arg(type));
            }
        }

        // Each clip's notes: the targets as they were out, as they are now in.
        std::vector<std::pair<ClipRef, std::vector<Note>>> changes;
        int changed = 0;
        for (const auto& [clipId, clip] : clips) {
            std::vector<Note> removed, added;
            for (const Target& t : targets) {
                if (t.clipId != clipId) continue;
                removed.push_back(t.original);
                if (!t.deleted) added.push_back(t.note);
                if (t.deleted || !(t.note == t.original)) ++changed;
            }
            changes.push_back({ClipRef{track->id, clipId}, notes::place(clip->notes, removed, added)});
        }
        const QString text = QStringLiteral("Edit %1 notes on %2: %3")
                                 .arg(QString::number(targets.size()), track->name, done.join(QStringLiteral(", ")));
        return change(text, QStringLiteral("edit_notes"),
                      {{QStringLiteral("trackId"), track->id}, {QStringLiteral("notes"), static_cast<int>(targets.size())},
                       {QStringLiteral("changedNotes"), changed}},
                      [&](ProjectEditor& editor) { editor.setClipsNotes(changes, text); });
    }

    // --- Automation, drawn ---

    // The key the arguments name: key, device_id and parameter_id, mixer, or send; why not.
    QString automationKey(const Track& track, const QJsonObject& arguments, QString& error) const {
        const QString key = arguments.value(QStringLiteral("key")).toString();
        const QString deviceId = arguments.value(QStringLiteral("device_id")).toString();
        const QString mixer = arguments.value(QStringLiteral("mixer")).toString();
        const QString send = arguments.value(QStringLiteral("send")).toString();
        if (!key.isEmpty() + !deviceId.isEmpty() + !mixer.isEmpty() + !send.isEmpty() != 1) {
            error = QStringLiteral("Name what to automate, one way: key (from get_track's automation), device_id and "
                                   "parameter_id (from get_device_parameters), mixer (\"volume\", \"pan\", \"activator\"), "
                                   "or send (a return's id).");
            return {};
        }
        if (!key.isEmpty()) return key;
        if (!send.isEmpty()) {
            if (!project().hasReturn(send)) error = QStringLiteral("No return track has the id \"%1\".").arg(send);
            return automation::sendKey(send);
        }
        if (!mixer.isEmpty()) {
            if (mixer == u"volume") return automation::kMixerVolume;
            if (mixer == u"pan") return automation::kMixerPan;
            if (mixer == u"activator" && !track.isMaster()) return automation::kMixerOn;
            error = QStringLiteral("mixer is \"volume\", \"pan\" or \"activator\" (its on/off; the master has none).");
            return {};
        }
        const QString paramId = arguments.value(QStringLiteral("parameter_id")).toString();
        if (paramId.isEmpty()) {
            error = QStringLiteral("parameter_id is required with device_id: a parameter's id from get_device_parameters.");
            return {};
        }
        if (!sub::app::findDevice(track.devices, deviceId)) {
            error = QStringLiteral("%1 has no device \"%2\": get_track lists its devices.").arg(track.name, deviceId);
            return {};
        }
        return paramId == automation::kDeviceOn ? automation::deviceOnKey(deviceId) : automation::deviceKey(deviceId, paramId);
    }

    ToolResult writeAutomation(const QJsonObject& arguments) {
        QString error;
        const Track* track = trackOf(arguments, QStringLiteral("track_id"), error);
        if (!track) return ToolResult::failure(error);
        const QString key = automationKey(*track, arguments, error);
        if (!error.isEmpty()) return ToolResult::failure(error);
        const std::optional<ParamSpec> spec = session_.bridge() ? session_.bridge()->paramSpec(track->id, key) : std::nullopt;
        if (!spec || !automation::isKey(key)) {
            return ToolResult::failure(QStringLiteral("\"%1\" can't be automated on %2 (a plug-in not loaded yet can't): "
                                                      "get_device_parameters lists what can.").arg(key, track->name));
        }
        const Param param{*spec, spec->name};
        Span at = span(arguments);
        if (!at.error.isEmpty()) return ToolResult::failure(at.error);
        const QJsonValue pointsValue = arguments.value(QStringLiteral("points"));
        const QJsonObject shape = arguments.value(QStringLiteral("shape")).toObject();
        const bool clear = arguments.value(QStringLiteral("clear")).toBool(false);
        if (!pointsValue.isUndefined() + !shape.isEmpty() + clear != 1) {
            return ToolResult::failure(QStringLiteral("Give points, a shape, or clear: true (one of them)."));
        }
        const Envelope existing = project().envelope(track->id, key);
        // A value as given, in the target's units: normalized.
        const auto normalizedOf = [&](const QJsonValue& value, QString& why) -> std::optional<double> {
            const auto [plain, problem] = wanted(param, QJsonObject{{QStringLiteral("value"), value}});
            if (!plain) {
                why = problem;
                return std::nullopt;
            }
            return spec->toNormalized(*plain);
        };

        Envelope envelope;
        QString what;
        if (clear) {
            if (existing.empty()) return ToolResult::failure(QStringLiteral("%1 isn't automated on %2.").arg(spec->name, track->name));
            const bool whole = at.from <= 0.0 && !std::isfinite(at.to);
            envelope = whole ? Envelope() : automation::removeRange(existing, at.from, std::isfinite(at.to) ? at.to : existing.back().beat + 1.0);
            what = whole ? QStringLiteral("Clear the automation of %1 on %2").arg(spec->name, track->name)
                         : QStringLiteral("Clear the automation of %1 on %2, %3")
                               .arg(spec->name, track->name, stretch(at.from, std::isfinite(at.to) ? at.to : existing.back().beat));
        } else {
            Envelope content;  // (on the timeline)
            if (!pointsValue.isUndefined()) {
                const QJsonArray rows = pointsValue.toArray();
                if (rows.isEmpty() || rows.size() > kMaxPoints) {
                    return ToolResult::failure(QStringLiteral("points are rows [beat, value, curve] (1 to %1 of them).").arg(kMaxPoints));
                }
                for (const QJsonValue& value : rows) {
                    const QJsonArray row = value.toArray();
                    if (row.size() < 2 || row.size() > 3 || !row.at(0).isDouble() || (row.size() == 3 && !row.at(2).isDouble())) {
                        return ToolResult::failure(QStringLiteral("A point is [beat, value, curve]: the beat on the song's timeline, "
                                                                  "the value in the target's units, curve -1..1 (may be left out)."));
                    }
                    QString why;
                    const auto v = normalizedOf(row.at(1), why);
                    if (!v) return ToolResult::failure(why);
                    const double curve = row.size() == 3 ? row.at(2).toDouble() : 0.0;
                    if (row.at(0).toDouble() < 0.0 || std::abs(curve) > 1.0) {
                        return ToolResult::failure(QStringLiteral("A point's beat is 0 or later, its curve -1 to 1."));
                    }
                    content.push_back({row.at(0).toDouble(), *v, curve});
                }
                content = automation::normalize(content);
                if (!arguments.contains(QStringLiteral("start_beat")) && !arguments.contains(QStringLiteral("start_bar"))) {
                    at.from = content.front().beat;
                }
                if (!std::isfinite(at.to)) at.to = content.back().beat;
                if (content.front().beat < at.from - 1e-9 || content.back().beat > at.to + 1e-9) {
                    return ToolResult::failure(QStringLiteral("The points are outside the stretch given."));
                }
            } else {
                if (!std::isfinite(at.to)) {
                    return ToolResult::failure(QStringLiteral("A shape needs where it ends: end_bar (or end_beat)."));
                }
                QString why;
                const auto shaped = shapeOf(shape, at, normalizedOf, why);
                if (!shaped) return ToolResult::failure(why);
                content = *shaped;
            }
            if (existing.empty()) {
                envelope = content;
            } else if (at.to - at.from < 1e-9) {  // (one point: among the others, nothing replaced)
                auto [added, index] = automation::addPoint(existing, content.front().beat, content.front().value);
                envelope = automation::setCurve(added, index, content.front().curve);
            } else {
                const double length = std::max(at.to - at.from, 0.0);
                envelope = automation::pasteRange(existing, automation::shift(content, -at.from), at.from, length);
            }
            what = QStringLiteral("Automate %1 on %2, %3").arg(spec->name, track->name, stretch(at.from, at.to));
        }
        envelope = automation::normalize(envelope);
        const QString trackId = track->id;
        QJsonObject out{{QStringLiteral("trackId"), trackId}, {QStringLiteral("key"), key},
                        {QStringLiteral("points"), static_cast<int>(envelope.size())}};
        if (session_.bridge() && session_.bridge()->isOverridden(trackId, key)) {
            out.insert(QStringLiteral("note"), QStringLiteral("The user changed it by hand since its automation last played: "
                                                              "it plays the hand-set value until they re-enable automation "
                                                              "(Edit › Re-Enable Automation)."));
        }
        return change(what, QStringLiteral("write_automation"), out, [&](ProjectEditor& editor) {
            if (envelope.empty()) {
                editor.clearEnvelope(trackId, key);
            } else {
                editor.setEnvelope(trackId, key, envelope, what);
            }
        });
    }

    // A shape's points over the stretch: a ramp, or an LFO (sine, triangle, saw, square); why not.
    std::optional<Envelope> shapeOf(const QJsonObject& shape, const Span& at,
                                    const std::function<std::optional<double>(const QJsonValue&, QString&)>& normalizedOf,
                                    QString& why) const {
        const QString type = shape.value(QStringLiteral("type")).toString();
        if (type == u"ramp") {
            const auto from = normalizedOf(shape.value(QStringLiteral("from")), why);
            if (!from) return std::nullopt;
            const auto to = normalizedOf(shape.value(QStringLiteral("to")), why);
            if (!to) return std::nullopt;
            const double curve = shape.value(QStringLiteral("curve")).toDouble(0.0);
            if (std::abs(curve) > 1.0) {
                why = QStringLiteral("A ramp's curve is -1 to 1.");
                return std::nullopt;
            }
            return Envelope{{at.from, *from, curve}, {at.to, *to, 0.0}};
        }
        if (type != u"lfo") {
            why = QStringLiteral("A shape's type is \"ramp\" ({from, to, curve}) or \"lfo\" ({wave, low, high, cycle_beats}).");
            return std::nullopt;
        }
        const auto low = normalizedOf(shape.value(QStringLiteral("low")), why);
        if (!low) return std::nullopt;
        const auto high = normalizedOf(shape.value(QStringLiteral("high")), why);
        if (!high) return std::nullopt;
        const QString wave = shape.value(QStringLiteral("wave")).toString(QStringLiteral("sine"));
        const double cycle = shape.value(QStringLiteral("cycle_beats")).toDouble(beatsPerBar());
        if (!(cycle >= 1.0 / 16.0)) {
            why = QStringLiteral("An LFO's cycle_beats is how long one cycle is (default a bar), at least 1/16 beat.");
            return std::nullopt;
        }
        // Each wave's level (0 low .. 1 high) at a phase (0..1), and its points over one cycle (a step: two at one phase).
        const auto level = [&wave](double phase) {
            if (wave == u"sine") return 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * phase);
            if (wave == u"triangle") return phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
            if (wave == u"saw") return phase;
            return phase < 0.5 ? 1.0 : 0.0;  // (square)
        };
        std::vector<std::pair<double, double>> cyclePoints;
        if (wave == u"sine") {
            for (int i = 0; i < kSinePoints; ++i) {
                const double phase = static_cast<double>(i) / kSinePoints;
                cyclePoints.emplace_back(phase, level(phase));
            }
        } else if (wave == u"triangle") {
            cyclePoints = {{0.0, 0.0}, {0.5, 1.0}};
        } else if (wave == u"saw") {
            cyclePoints = {{0.0, 0.0}, {1.0, 1.0}};  // (and down again at the next cycle's start)
        } else if (wave == u"square") {
            cyclePoints = {{0.0, 1.0}, {0.5, 1.0}, {0.5, 0.0}, {1.0, 0.0}};
        } else {
            why = QStringLiteral("An LFO's wave is \"sine\", \"triangle\", \"saw\" or \"square\".");
            return std::nullopt;
        }
        Envelope out;
        const auto valueAt = [&](double level) { return *low + (*high - *low) * level; };
        for (double start = at.from; start < at.to - 1e-9; start += cycle) {
            for (const auto& [phase, level] : cyclePoints) {
                const double beat = start + phase * cycle;
                if (beat > at.to + 1e-9) break;
                out.push_back({beat, valueAt(level), 0.0});
            }
            if (static_cast<int>(out.size()) > kMaxPoints) {
                why = QStringLiteral("That is more than %1 points: a longer cycle_beats, or a shorter stretch.").arg(kMaxPoints);
                return std::nullopt;
            }
        }
        // It ends where the wave is at the stretch's end.
        if (out.empty() || out.back().beat < at.to - 1e-9) {
            const double cycles = (at.to - at.from) / cycle;
            out.push_back({at.to, valueAt(level(cycles - std::floor(cycles))), 0.0});
        }
        return automation::normalize(out);
    }

    // --- Tracks ---

    // Where a track the arguments place goes: after_track_id, or into group_id
    // (last there), or last of all.
    std::optional<InsertionPoint> placeFor(const QJsonObject& arguments, QString& error) const {
        const QString after = arguments.value(QStringLiteral("after_track_id")).toString();
        const QString group = arguments.value(QStringLiteral("group_id")).toString();
        if (!after.isEmpty() && !group.isEmpty()) {
            error = QStringLiteral("Give after_track_id or group_id, not both.");
            return std::nullopt;
        }
        if (!after.isEmpty()) {
            if (!project().hasTrack(after)) {
                error = QStringLiteral("after_track_id: no track of the arrangement has the id \"%1\".").arg(after);
                return std::nullopt;
            }
            return session_.editor()->insertionPoint(after);
        }
        if (!group.isEmpty()) {
            if (!project().hasTrack(group) || !project().track(group).isGroup()) {
                error = QStringLiteral("group_id: \"%1\" isn't a group (list_tracks: kind group).").arg(group);
                return std::nullopt;
            }
            InsertionPoint point = session_.editor()->insertionPoint(group);  // (after it and what is in it...)
            point.parent = group;                                             // (...in it)
            return point;
        }
        return InsertionPoint{static_cast<int>(project().tracks().size()), std::nullopt};
    }

    ToolResult addTrack(const QJsonObject& arguments) {
        const QString kind = arguments.value(QStringLiteral("kind")).toString();
        const QString name = arguments.value(QStringLiteral("name")).toString().trimmed();
        const QString instrument = arguments.value(QStringLiteral("instrument")).toString();
        if (kind != u"midi" && kind != u"audio" && kind != u"return") {
            return ToolResult::failure(QStringLiteral("kind is \"midi\", \"audio\" or \"return\"."));
        }
        if (!instrument.isEmpty() && kind != u"midi") {
            return ToolResult::failure(QStringLiteral("Only a MIDI track has an instrument."));
        }
        QString error;
        const std::optional<InsertionPoint> point = placeFor(arguments, error);
        if (!point) return ToolResult::failure(error);
        if (kind == u"return" && (arguments.contains(QStringLiteral("after_track_id")) || arguments.contains(QStringLiteral("group_id")))) {
            return ToolResult::failure(QStringLiteral("A return goes after the other returns: no after_track_id or group_id."));
        }
        // Its instrument, from the catalog: a built-in kind, a plug-in, or a preset.
        std::optional<Device> preset;
        QString builtin;
        std::optional<PluginRef> plugin;
        if (!instrument.isEmpty()) {
            if (!resolve(instrument, builtin, plugin, preset, error)) return ToolResult::failure(error);
            const bool isInstrument = preset ? deviceIsInstrument(*preset) : sub::app::isInstrument(builtin, plugin);
            if (!isInstrument) {
                return ToolResult::failure(QStringLiteral("\"%1\" isn't an instrument: list_devices with role \"instrument\" "
                                                          "lists them.").arg(instrument));
            }
        }
        const int index = point->index.value_or(static_cast<int>(project().tracks().size()));
        const TrackParent parent{point->parent};
        QString made;
        const QString text = QStringLiteral("Add %1 track%2").arg(kind == u"midi" ? QStringLiteral("a MIDI")
                                                                  : kind == u"audio" ? QStringLiteral("an audio")
                                                                                     : QStringLiteral("a return"),
                                                                  name.isEmpty() ? QString() : QStringLiteral(" %1").arg(name));
        ToolResult result = change(text, QStringLiteral("add_track"), {}, [&](ProjectEditor& editor) {
            if (kind == u"return") {
                made = editor.addReturnTrack(-1, name);
            } else if (kind == u"audio") {
                made = editor.addAudioTrack(index, name, parent);
            } else if (preset) {
                made = editor.addMidiTrackWith(*preset, index, parent, text);
                if (!made.isEmpty() && !name.isEmpty()) editor.renameTrack(made, name);
            } else {
                made = editor.addMidiTrack(index, name, plugin ? kPluginKind : builtin, plugin, parent);
            }
            if (made.isEmpty()) throw EditError(QStringLiteral("The track couldn't be added there."));
        });
        if (!result.error) {
            QJsonObject out = result.structured.toObject();
            out.insert(QStringLiteral("trackId"), made);
            out.insert(QStringLiteral("name"), project().track(made).name);
            result.structured = out;
        }
        return result;
    }

    ToolResult deleteTracks(const QJsonObject& arguments) {
        QString error;
        const auto ids = trackIds(arguments, QStringLiteral("track_ids"), true, error);
        if (!ids) return ToolResult::failure(error);
        QStringList names;
        for (const QString& id : *ids) names << project().track(id).name;
        const QString text = QStringLiteral("Delete %1").arg(names.join(QStringLiteral(", ")));
        return change(text, QStringLiteral("delete_tracks"), {}, [&](ProjectEditor& editor) {
            if (session_.bridge()) session_.bridge()->storePluginStates();  // (rewound, they sound as they did)
            editor.deleteTracks(*ids);
            for (const QString& id : *ids) {
                if (editor.project()->hasOwner(id)) throw EditError(QStringLiteral("%1 can't be deleted (it is in a frozen group).").arg(project().track(id).name));
            }
        });
    }

    ToolResult moveTracks(const QJsonObject& arguments) {
        QString error;
        const auto ids = trackIds(arguments, QStringLiteral("track_ids"), false, error);
        if (!ids) return ToolResult::failure(error);
        int index = 0;
        QString parent;
        const QString before = arguments.value(QStringLiteral("before_track_id")).toString();
        if (!before.isEmpty()) {
            if (arguments.contains(QStringLiteral("after_track_id")) || arguments.contains(QStringLiteral("group_id"))) {
                return ToolResult::failure(QStringLiteral("Give one of before_track_id, after_track_id or group_id."));
            }
            if (!project().hasTrack(before)) {
                return ToolResult::failure(QStringLiteral("before_track_id: no track of the arrangement has the id \"%1\".").arg(before));
            }
            index = project().trackIndex(before);
            parent = project().track(before).parent.value_or(QString());
        } else {
            const std::optional<InsertionPoint> point = placeFor(arguments, error);
            if (!point) return ToolResult::failure(error);
            index = point->index.value_or(static_cast<int>(project().tracks().size()));
            parent = point->parent.value_or(QString());
        }
        QStringList names;
        for (const QString& id : *ids) names << project().track(id).name;
        const QString text = QStringLiteral("Move %1").arg(names.join(QStringLiteral(", ")));
        return change(text, QStringLiteral("move_tracks"), {}, [&](ProjectEditor& editor) {
            if (!editor.canMoveTracks(*ids, index, parent)) {
                throw EditError(QStringLiteral("They can't go there (a group into itself, amid another group's tracks, into or "
                                               "out of a frozen group), or they are there already."));
            }
            editor.moveTracks(*ids, index, parent);
        });
    }

    // A new group of track_ids (named), or the groups `ungroup` lists gone (what was in them in their place).
    ToolResult groupTracks(const QJsonObject& arguments) {
        QString error;
        if (arguments.contains(QStringLiteral("ungroup"))) {
            if (arguments.contains(QStringLiteral("track_ids")) || arguments.contains(QStringLiteral("name"))) {
                return ToolResult::failure(QStringLiteral("Give track_ids to group, or ungroup: groups to take apart (not both)."));
            }
            const auto groups = trackIds(arguments, QStringLiteral("ungroup"), false, error);
            if (!groups) return ToolResult::failure(error);
            QStringList names;
            for (const QString& id : *groups) {
                if (!project().track(id).isGroup()) return ToolResult::failure(QStringLiteral("%1 isn't a group.").arg(project().track(id).name));
                names << project().track(id).name;
            }
            return change(QStringLiteral("Ungroup %1").arg(names.join(QStringLiteral(", "))), QStringLiteral("group_tracks"), {},
                          [&](ProjectEditor& editor) { editor.ungroup(*groups); });
        }
        const auto ids = trackIds(arguments, QStringLiteral("track_ids"), false, error);
        if (!ids) return ToolResult::failure(error);
        const QString name = arguments.value(QStringLiteral("name")).toString().trimmed();
        QString group;
        ToolResult result = change(QStringLiteral("Group %1 tracks%2").arg(QString::number(ids->size()),
                                                                          name.isEmpty() ? QString() : QStringLiteral(" as %1").arg(name)),
                                   QStringLiteral("group_tracks"), {}, [&](ProjectEditor& editor) {
                                       group = editor.groupTracks(*ids);
                                       if (group.isEmpty()) throw EditError(QStringLiteral("These tracks can't be grouped."));
                                       if (!name.isEmpty()) editor.renameTrack(group, name);
                                   });
        if (!result.error) {
            QJsonObject out = result.structured.toObject();
            out.insert(QStringLiteral("groupId"), group);
            result.structured = out;
        }
        return result;
    }

    // --- Devices ---

    std::vector<CatalogEntry> catalog() const {
        std::vector<CatalogEntry> out;
        for (const BuiltinDevice& device : builtinDevices()) {
            out.push_back({device.kind, device.name, QStringLiteral("builtin"), device.category, {}, device.instrument});
        }
        out.push_back({kRackKind, QStringLiteral("Rack"), QStringLiteral("builtin"), QStringLiteral("Racks"), {}, false});
        if (session_.plugins()) {
            for (const PluginInfo& plugin : session_.plugins()->plugins()) {
                out.push_back({kPluginPrefix + plugin.uid, plugin.name, QStringLiteral("plugin"), plugin.category,
                               plugin.vendor, plugin.instrument});
            }
        }
        if (session_.presets()) {
            for (const BrowserItem& item : session_.presets()->items()) {
                // (Whether it is an instrument's, the device it is for says: read when it is added.)
                out.push_back({kPresetPrefix + item.path, item.name, QStringLiteral("preset"), item.detail, {}, false});
            }
        }
        return out;
    }

    ToolResult listDevices(const QJsonObject& arguments) const {
        const QStringList words = arguments.value(QStringLiteral("query")).toString().split(u' ', Qt::SkipEmptyParts);
        const QString type = arguments.value(QStringLiteral("type")).toString();
        const QString role = arguments.value(QStringLiteral("role")).toString();
        if (!type.isEmpty() && type != u"builtin" && type != u"plugin" && type != u"preset") {
            return ToolResult::failure(QStringLiteral("type is \"builtin\", \"plugin\" or \"preset\" (none: all)."));
        }
        if (!role.isEmpty() && role != u"instrument" && role != u"effect") {
            return ToolResult::failure(QStringLiteral("role is \"instrument\" or \"effect\" (none: both)."));
        }
        const int offset = std::max(0, arguments.value(QStringLiteral("offset")).toInt(0));
        const int limit = std::clamp(arguments.value(QStringLiteral("limit")).toInt(kDefaultDevices), 1, kMaxDevices);
        QJsonArray list;
        int total = 0;
        for (const CatalogEntry& entry : catalog()) {
            if (!type.isEmpty() && entry.type != type) continue;
            if (!role.isEmpty() && (entry.type == u"preset" || entry.instrument != (role == u"instrument"))) continue;
            if (!matches(entry.name + u' ' + entry.category + u' ' + entry.vendor, words)) continue;
            if (total++ < offset || list.size() >= limit) continue;
            QJsonObject e{{QStringLiteral("id"), entry.id},
                          {QStringLiteral("name"), entry.name},
                          {QStringLiteral("type"), entry.type}};
            if (!entry.category.isEmpty()) e.insert(entry.type == u"preset" ? QStringLiteral("for") : QStringLiteral("category"), entry.category);
            if (!entry.vendor.isEmpty()) e.insert(QStringLiteral("vendor"), entry.vendor);
            if (entry.instrument) e.insert(QStringLiteral("instrument"), true);
            list.append(e);
        }
        QJsonObject out{{QStringLiteral("devices"), list}, {QStringLiteral("total"), total}, {QStringLiteral("offset"), offset}};
        if (offset + list.size() < total) out.insert(QStringLiteral("nextOffset"), offset + static_cast<int>(list.size()));
        if (session_.plugins() && session_.plugins()->scanning()) {
            out.insert(QStringLiteral("note"), QStringLiteral("Plug-ins are being scanned: more may come."));
        }
        return {out, {}, false};
    }

    // What a catalog id is: a built-in kind (or a rack), a plug-in, or a preset (read); false and why not.
    bool resolve(const QString& id, QString& builtin, std::optional<PluginRef>& plugin, std::optional<Device>& preset,
                 QString& error) const {
        if (id.startsWith(kPluginPrefix)) {
            const QString uid = id.mid(kPluginPrefix.size());
            if (session_.plugins()) {
                for (const PluginInfo& info : session_.plugins()->plugins()) {
                    if (info.uid != uid) continue;
                    plugin = PluginRef{info.format, info.uid, info.name, info.vendor, info.path, info.instrument};
                    return true;
                }
            }
            error = QStringLiteral("No plug-in \"%1\": list_devices lists them.").arg(id);
            return false;
        }
        if (id.startsWith(kPresetPrefix)) {
            const QString path = id.mid(kPresetPrefix.size());
            const bool listed = session_.presets() && std::any_of(session_.presets()->items().begin(), session_.presets()->items().end(),
                                                                  [&](const BrowserItem& item) { return item.path == path; });
            if (!listed) {
                error = QStringLiteral("No preset \"%1\" in the library: list_devices lists them.").arg(id);
                return false;
            }
            try {
                preset = loadPreset(path);
            } catch (const ProjectFileError& e) {
                error = e.message();
                return false;
            }
            return true;
        }
        if (id == kRackKind || builtinDevice(id)) {
            builtin = id;
            return true;
        }
        error = QStringLiteral("No device \"%1\": list_devices gives the ids (\"compressor\", \"plugin:...\", \"preset:...\").").arg(id);
        return false;
    }

    // Where a device goes on a track: before before_device_id (in its chain),
    // else last in chain_id (a rack's chain) or the track's own chain.
    std::optional<ChainPlace> chainPlace(const Track& track, const QJsonObject& arguments, QString& error) const {
        const QString before = arguments.value(QStringLiteral("before_device_id")).toString();
        const QString chain = arguments.value(QStringLiteral("chain_id")).toString();
        if (!before.isEmpty()) {
            const auto place = placeOf(track.devices, before);
            if (!place) error = QStringLiteral("%1 has no device \"%2\": get_track lists its devices.").arg(track.name, before);
            return place;
        }
        if (!chain.isEmpty() && !hasChain(track.devices, chain)) {
            error = QStringLiteral("%1 has no rack chain \"%2\": get_track lists racks' chains.").arg(track.name, chain);
            return std::nullopt;
        }
        return ChainPlace{chain, -1};
    }

    ToolResult addDevice(const QJsonObject& arguments) {
        QString error;
        const Track* track = trackOf(arguments, QStringLiteral("track_id"), error);
        if (!track) return ToolResult::failure(error);
        const QString id = arguments.value(QStringLiteral("device")).toString();
        if (id.isEmpty()) return ToolResult::failure(QStringLiteral("device is required: an id from list_devices."));
        QString builtin;
        std::optional<PluginRef> plugin;
        std::optional<Device> preset;
        if (!resolve(id, builtin, plugin, preset, error)) return ToolResult::failure(error);
        const bool instrument = preset ? deviceIsInstrument(*preset) : sub::app::isInstrument(builtin, plugin);
        if (instrument && !track->isMidi()) {
            return ToolResult::failure(QStringLiteral("An instrument goes on a MIDI track (add_track with kind \"midi\" and "
                                                      "this instrument makes one)."));
        }
        const auto place = chainPlace(*track, arguments, error);
        if (!place) return ToolResult::failure(error);
        const QString trackId = track->id;
        const QString name = preset ? deviceName(*preset) : plugin ? plugin->name : builtinDevice(builtin) ? builtinDevice(builtin)->name : QStringLiteral("Rack");
        const QString text = QStringLiteral("Add %1 to %2").arg(name, track->name);
        QStringList added;
        QJsonObject out{{QStringLiteral("trackId"), trackId}};
        if (instrument) {
            for (const Device& d : track->devices) {
                if (deviceIsInstrument(d)) {
                    out.insert(QStringLiteral("replaced"), QStringLiteral("%1 (%2): a MIDI track has one instrument.").arg(deviceName(d), d.id));
                }
            }
        }
        ToolResult result = change(text, QStringLiteral("add_device"), out, [&](ProjectEditor& editor) {
            if (preset) {
                added = editor.insertDevices(trackId, {*preset}, place->index, place->chain, text, false);
            } else {
                const QString made = editor.addDevice(trackId, plugin ? kPluginKind : builtin, place->index, plugin, place->chain);
                if (!made.isEmpty()) added << made;
            }
            if (added.isEmpty()) throw EditError(QStringLiteral("%1 can't go there (racks nest at most %2 deep).").arg(name).arg(kMaxRackDepth));
        });
        if (!result.error) {
            QJsonObject o = result.structured.toObject();
            o.insert(QStringLiteral("deviceId"), added.first());
            result.structured = o;
        }
        return result;
    }

    // The devices `device_ids` lists, all on the track; none and why not.
    std::optional<QStringList> deviceIds(const Track& track, const QJsonObject& arguments, QString& error) const {
        QStringList ids;
        for (const QJsonValue& value : arguments.value(QStringLiteral("device_ids")).toArray()) {
            const QString id = value.toString();
            if (!sub::app::findDevice(track.devices, id)) {
                error = QStringLiteral("%1 has no device \"%2\": get_track lists its devices.").arg(track.name, id);
                return std::nullopt;
            }
            if (!ids.contains(id)) ids.append(id);
        }
        if (ids.isEmpty()) {
            error = QStringLiteral("device_ids is required: devices' ids from get_track.");
            return std::nullopt;
        }
        return ids;
    }

    QStringList deviceNames(const Track& track, const QStringList& ids) const {
        QStringList names;
        for (const QString& id : ids) names << deviceName(*sub::app::findDevice(track.devices, id));
        return names;
    }

    ToolResult removeDevices(const QJsonObject& arguments) {
        QString error;
        const Track* track = trackOf(arguments, QStringLiteral("track_id"), error);
        if (!track) return ToolResult::failure(error);
        const auto ids = deviceIds(*track, arguments, error);
        if (!ids) return ToolResult::failure(error);
        const QString trackId = track->id;
        const QString text = QStringLiteral("Remove %1 from %2").arg(deviceNames(*track, *ids).join(QStringLiteral(", ")), track->name);
        return change(text, QStringLiteral("remove_devices"), {}, [&](ProjectEditor& editor) {
            if (session_.bridge()) session_.bridge()->storePluginStates();  // (rewound, they sound as they did)
            editor.removeDevices(trackId, *ids);
        });
    }

    ToolResult moveDevices(const QJsonObject& arguments) {
        QString error;
        const Track* track = trackOf(arguments, QStringLiteral("track_id"), error);
        if (!track) return ToolResult::failure(error);
        const auto ids = deviceIds(*track, arguments, error);
        if (!ids) return ToolResult::failure(error);
        const Track* to = track;
        if (arguments.contains(QStringLiteral("to_track_id"))) {
            to = trackOf(arguments, QStringLiteral("to_track_id"), error);
            if (!to) return ToolResult::failure(error);
        }
        if (!arguments.contains(QStringLiteral("before_device_id")) && !arguments.contains(QStringLiteral("chain_id")) && to == track) {
            return ToolResult::failure(QStringLiteral("Say where: before_device_id, chain_id (a rack's chain), or to_track_id."));
        }
        const auto place = chainPlace(*to, arguments, error);
        if (!place) return ToolResult::failure(error);
        const QString trackId = track->id;
        const QString toId = to->id;
        const QString names = deviceNames(*track, *ids).join(QStringLiteral(", "));
        const QString text = to == track ? QStringLiteral("Move %1 on %2").arg(names, track->name)
                                         : QStringLiteral("Move %1 from %2 to %3").arg(names, track->name, to->name);
        return change(text, QStringLiteral("move_devices"), {}, [&](ProjectEditor& editor) {
            const bool moved = toId == trackId
                                   ? editor.moveDevices(trackId, *ids, place->index < 0 ? std::numeric_limits<int>::max() : place->index,
                                                        place->chain)
                                   : editor.moveDevicesToTrack(trackId, *ids, toId, place->index, place->chain);
            if (!moved) {
                throw EditError(QStringLiteral("They can't go there: an instrument stays first on its track (and doesn't move to "
                                               "another), a rack doesn't go into itself, racks nest at most %1 deep; or they are "
                                               "there already.").arg(kMaxRackDepth));
            }
        });
    }

    static constexpr int kMaxPoints = 2000;
    static constexpr int kSinePoints = 16;
    static constexpr int kDefaultDevices = 100;
    static constexpr int kMaxDevices = 500;
};

}  // namespace

void addEditTools(Protocol& protocol, Session& session, const QString& edits) {
    auto tools = std::make_shared<EditTools>(session);
    // Every edit (a tool not read-only) fails while the user has edits off.
    const auto addTool = [&protocol, tools](Tool tool) { mcp::addTool(protocol, tools, std::move(tool)); };
    const QJsonObject trackId = property(QStringLiteral("string"), QStringLiteral("The track's id, from list_tracks."));
    const QJsonObject trackIds{{QStringLiteral("type"), QStringLiteral("array")},
                               {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                               {QStringLiteral("description"), QStringLiteral("Tracks' ids, from list_tracks.")}};
    const QJsonObject startBeat = property(QStringLiteral("number"), QStringLiteral("From this beat (default: the start)."));
    const QJsonObject endBeat = property(QStringLiteral("number"), QStringLiteral("Up to this beat (default: the end)."));
    const QJsonObject startBar = property(QStringLiteral("number"), QStringLiteral("Instead of start_beat: from where this bar starts (from 1)."));
    const QJsonObject endBar = property(QStringLiteral("number"), QStringLiteral("Instead of end_beat: up to where this bar starts (bars 17 to 24: start_bar 17, end_bar 25)."));
    QJsonObject anyValue{{QStringLiteral("description"), QStringLiteral("In the target's units (as get_device_parameters gives them), a choice's name, or true/false for a switch.")}};
    anyValue.insert(QStringLiteral("type"), QJsonArray{QStringLiteral("number"), QStringLiteral("string"), QStringLiteral("boolean")});
    QJsonObject pitchList{{QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("description"), QStringLiteral("Only notes of these pitches (numbers, or names: \"C1\" for a kick).")}};
    pitchList.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QJsonArray{QStringLiteral("integer"), QStringLiteral("string")}}});

    addTool({
        QStringLiteral("edit_notes"),
        QStringLiteral("Edit Notes"),
        QStringLiteral("Changes the notes a MIDI track already plays, where they are, as the piano roll's tools do: "
                       "those starting in a stretch (start_bar/end_bar or beats; default all), of some pitches or "
                       "clips if given. operations apply in order, e.g. [{\"type\": \"transpose\", \"semitones\": -12}, "
                       "{\"type\": \"quantize\", \"grid\": 0.25}]: transpose {semitones}; shift {beats} (in time); "
                       "quantize {grid in beats, amount 0..1}; humanize_timing {amount 0..1, seed}; scale_time {factor: "
                       "2 twice as spread out}; legato (each note up to the next); length {beats, or factor}; velocity "
                       "{set, change or factor}; humanize_velocity {amount 0..1: a model's velocities, as a player's}; "
                       "mute; unmute; delete. Bends, vibrato and the notes it doesn't select stay as they are. Use it "
                       "rather than write_midi_clip to change part of what is there.") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("operations"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
                             {QStringLiteral("minItems"), 1},
                             {QStringLiteral("description"), QStringLiteral("What to do, in order: each {\"type\": ..., its settings}.")}}},
                {QStringLiteral("start_beat"), startBeat},
                {QStringLiteral("end_beat"), endBeat},
                {QStringLiteral("start_bar"), startBar},
                {QStringLiteral("end_bar"), endBar},
                {QStringLiteral("pitches"), pitchList},
                {QStringLiteral("clip_ids"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                         {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                         {QStringLiteral("description"), QStringLiteral("Only these clips' notes (get_notes lists the clips).")}}}},
               {QStringLiteral("track_id"), QStringLiteral("operations")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->editNotes(arguments); },
    });
    addTool({
        QStringLiteral("write_automation"),
        QStringLiteral("Write Automation"),
        QStringLiteral("Draws automation on a track (a return, the master): of a device's parameter (device_id and "
                       "parameter_id, from get_device_parameters), its mixer (mixer: \"volume\", \"pan\", "
                       "\"activator\"), a send (send: a return's id), or an envelope's key (get_track). Either points, "
                       "rows [beat, value, curve] in the target's units as get_automation gives them; or a shape over a "
                       "stretch: {\"type\": \"ramp\", \"from\", \"to\", \"curve\"} (a fade, a filter sweep) or {\"type\": "
                       "\"lfo\", \"wave\": sine/triangle/saw/square, \"low\", \"high\", \"cycle_beats\"} (a wobble, a "
                       "tremolo; default a cycle a bar); or clear: true. It replaces the automation in that stretch "
                       "(points: from the first to the last, unless a stretch is given), and keeps the rest.") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("device_id"), property(QStringLiteral("string"), QStringLiteral("The device's id (with parameter_id)."))},
                {QStringLiteral("parameter_id"), property(QStringLiteral("string"), QStringLiteral("The parameter's id, from get_device_parameters."))},
                {QStringLiteral("mixer"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                             {QStringLiteral("enum"), QJsonArray{QStringLiteral("volume"), QStringLiteral("pan"), QStringLiteral("activator")}}}},
                {QStringLiteral("send"), property(QStringLiteral("string"), QStringLiteral("A return's id: the level of the send to it."))},
                {QStringLiteral("key"), property(QStringLiteral("string"), QStringLiteral("An envelope's key, from get_track's automation."))},
                {QStringLiteral("points"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                             {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
                             {QStringLiteral("description"), QStringLiteral("Rows [beat, value, curve]: the beat on the song's timeline, the value in the target's units, curve -1..1 (may be left out).")}}},
                {QStringLiteral("shape"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"),
                              QJsonObject{{QStringLiteral("type"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                               {QStringLiteral("enum"), QJsonArray{QStringLiteral("ramp"), QStringLiteral("lfo")}}}},
                                          {QStringLiteral("from"), anyValue},
                                          {QStringLiteral("to"), anyValue},
                                          {QStringLiteral("curve"), property(QStringLiteral("number"), QStringLiteral("A ramp's bend, -1..1."))},
                                          {QStringLiteral("wave"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                               {QStringLiteral("enum"), QJsonArray{QStringLiteral("sine"), QStringLiteral("triangle"), QStringLiteral("saw"), QStringLiteral("square")}}}},
                                          {QStringLiteral("low"), anyValue},
                                          {QStringLiteral("high"), anyValue},
                                          {QStringLiteral("cycle_beats"), property(QStringLiteral("number"), QStringLiteral("An LFO's cycle in beats (default a bar)."))}}}}},
                {QStringLiteral("clear"), property(QStringLiteral("boolean"), QStringLiteral("Take the automation away (in the stretch, else all of it)."))},
                {QStringLiteral("start_beat"), startBeat},
                {QStringLiteral("end_beat"), endBeat},
                {QStringLiteral("start_bar"), startBar},
                {QStringLiteral("end_bar"), endBar}},
               {QStringLiteral("track_id")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->writeAutomation(arguments); },
    });

    // Tracks.
    addTool({
        QStringLiteral("add_track"),
        QStringLiteral("Add Track"),
        QStringLiteral("A new track: \"midi\" (with an instrument from list_devices: a built-in one, a plug-in or a "
                       "preset; none: empty), \"audio\", or \"return\" (after the returns). It goes after "
                       "after_track_id (and what is in it, in its group), last in group_id, or last. Its trackId; "
                       "a name may have # for its number (\"# Bass\").") + edits,
        schema({{QStringLiteral("kind"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                     {QStringLiteral("enum"), QJsonArray{QStringLiteral("midi"), QStringLiteral("audio"), QStringLiteral("return")}}}},
                {QStringLiteral("name"), property(QStringLiteral("string"), QStringLiteral("Its name (default: from its instrument)."))},
                {QStringLiteral("instrument"), property(QStringLiteral("string"), QStringLiteral("A MIDI track's instrument: an id from list_devices (role instrument)."))},
                {QStringLiteral("after_track_id"), property(QStringLiteral("string"), QStringLiteral("Put it after this track."))},
                {QStringLiteral("group_id"), property(QStringLiteral("string"), QStringLiteral("Put it last in this group."))}},
               {QStringLiteral("kind")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->addTrack(arguments); },
    });
    addTool({
        QStringLiteral("delete_tracks"),
        QStringLiteral("Delete Tracks"),
        QStringLiteral("Deletes tracks (returns too, with the sends into them; a group with what is in it), with their "
                       "clips, devices and automation.") + edits,
        schema({{QStringLiteral("track_ids"), trackIds}}, {QStringLiteral("track_ids")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->deleteTracks(arguments); },
    });
    addTool({
        QStringLiteral("move_tracks"),
        QStringLiteral("Move Tracks"),
        QStringLiteral("Moves tracks (a group with what is in it), together in their order: before before_track_id or "
                       "after after_track_id (in that one's group), last in group_id, or with none of these last and "
                       "out of any group.") + edits,
        schema({{QStringLiteral("track_ids"), trackIds},
                {QStringLiteral("before_track_id"), property(QStringLiteral("string"), QStringLiteral("Before this track."))},
                {QStringLiteral("after_track_id"), property(QStringLiteral("string"), QStringLiteral("After this track (and what is in it)."))},
                {QStringLiteral("group_id"), property(QStringLiteral("string"), QStringLiteral("Last in this group."))}},
               {QStringLiteral("track_ids")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->moveTracks(arguments); },
    });
    addTool({
        QStringLiteral("group_tracks"),
        QStringLiteral("Group Tracks"),
        QStringLiteral("A new group holding track_ids, where the first of them was, as Ctrl+G makes one (named, if "
                       "name is given); its groupId. Or, with ungroup instead, those groups go and what was in them "
                       "takes their place, as Ctrl+Shift+G.") + edits,
        schema({{QStringLiteral("track_ids"), trackIds},
                {QStringLiteral("name"), property(QStringLiteral("string"), QStringLiteral("The new group's name (default \"# Group\")."))},
                {QStringLiteral("ungroup"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                        {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                        {QStringLiteral("description"), QStringLiteral("Instead: groups' ids to take apart.")}}}}),
        false,
        [tools](const QJsonObject& arguments) { return tools->groupTracks(arguments); },
    });

    // Devices.
    addTool({
        QStringLiteral("list_devices"),
        QStringLiteral("List Devices"),
        QStringLiteral("The devices that can go on a track: SUBstation's built-in ones (id: its kind, \"compressor\"; "
                       "\"rack\" for an empty rack), the plug-ins installed (\"plugin:<id>\", with vendor and category) "
                       "and the presets in the user's library (\"preset:<path>\", with the device it is `for`). Each "
                       "says whether it is an instrument. query keeps those having every word (in the name, category "
                       "or vendor); type and role narrow it; a page is limit long (100; nextOffset).") ,
        schema({{QStringLiteral("query"), property(QStringLiteral("string"), QStringLiteral("Words to look for (any case): \"reverb\", \"serum\"."))},
                {QStringLiteral("type"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                     {QStringLiteral("enum"), QJsonArray{QStringLiteral("builtin"), QStringLiteral("plugin"), QStringLiteral("preset")}}}},
                {QStringLiteral("role"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                     {QStringLiteral("enum"), QJsonArray{QStringLiteral("instrument"), QStringLiteral("effect")}}}},
                {QStringLiteral("offset"), property(QStringLiteral("integer"), QStringLiteral("Skip this many (default 0)."))},
                {QStringLiteral("limit"), property(QStringLiteral("integer"), QStringLiteral("At most this many (1 to 500, default 100)."))}}),
        true,
        [tools](const QJsonObject& arguments) { return tools->listDevices(arguments); },
    });
    const QJsonObject before = property(QStringLiteral("string"), QStringLiteral("Before this device (in its chain; default: last)."));
    const QJsonObject chain = property(QStringLiteral("string"), QStringLiteral("Into this rack chain (get_track lists racks' chains), last."));
    addTool({
        QStringLiteral("add_device"),
        QStringLiteral("Add Device"),
        QStringLiteral("Puts a device from list_devices on a track (a return, the master), as the user would drop it "
                       "there: a built-in device as its default preset has it, a plug-in, or a preset. Last in the "
                       "chain unless before_device_id or chain_id (a rack's chain) says where. An instrument goes on a "
                       "MIDI track, first, replacing the one there (`replaced` says which). Its deviceId; "
                       "get_device_parameters then sets it up.") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("device"), property(QStringLiteral("string"), QStringLiteral("An id from list_devices: \"reverb\", \"plugin:...\", \"preset:...\"."))},
                {QStringLiteral("before_device_id"), before},
                {QStringLiteral("chain_id"), chain}},
               {QStringLiteral("track_id"), QStringLiteral("device")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->addDevice(arguments); },
    });
    const QJsonObject deviceIds{{QStringLiteral("type"), QStringLiteral("array")},
                                {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                {QStringLiteral("description"), QStringLiteral("Devices' ids, from get_track (racks' devices too).")}};
    addTool({
        QStringLiteral("remove_devices"),
        QStringLiteral("Remove Devices"),
        QStringLiteral("Takes devices off a track (a rack with what is in it), with their automation and the macros "
                       "mapped to them.") + edits,
        schema({{QStringLiteral("track_id"), trackId}, {QStringLiteral("device_ids"), deviceIds}},
               {QStringLiteral("track_id"), QStringLiteral("device_ids")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->removeDevices(arguments); },
    });
    addTool({
        QStringLiteral("move_devices"),
        QStringLiteral("Move Devices"),
        QStringLiteral("Moves devices together, in their order: before before_device_id, last in chain_id (a rack's "
                       "chain), or to another track (to_track_id: last there, or before before_device_id on it; "
                       "effects only, keeping their settings and automation). An instrument stays first on its "
                       "track.") + edits,
        schema({{QStringLiteral("track_id"), trackId},
                {QStringLiteral("device_ids"), deviceIds},
                {QStringLiteral("before_device_id"), before},
                {QStringLiteral("chain_id"), chain},
                {QStringLiteral("to_track_id"), property(QStringLiteral("string"), QStringLiteral("Another track to move them to."))}},
               {QStringLiteral("track_id"), QStringLiteral("device_ids")}),
        false,
        [tools](const QJsonObject& arguments) { return tools->moveDevices(arguments); },
    });
}

}  // namespace sub::app::mcp
