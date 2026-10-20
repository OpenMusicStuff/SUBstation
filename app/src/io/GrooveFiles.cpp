#include "io/GrooveFiles.h"

#include "io/LiveSet.h"
#include "model/Devices.h"
#include "model/Errors.h"
#include "model/Grooves.h"
#include "model/Ids.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <map>

namespace sub::app {

namespace {

double numberOr(const QJsonObject& data, const QString& key, double fallback) {
    const QJsonValue value = data.value(key);
    return value.isDouble() && std::isfinite(value.toDouble()) ? value.toDouble() : fallback;
}

[[noreturn]] void damaged(const QString& name, const QString& why) {
    throw ProjectFileError(QStringLiteral("%1 is damaged: %2").arg(name, why));
}

}  // namespace

QJsonObject grooveToJson(const groove::Groove& groove) {
    QJsonArray lanes;
    for (const groove::Lane& lane : groove.lanes) {
        QJsonArray keys;
        for (const int key : lane.keys) keys.append(key);
        QJsonArray steps;
        for (const groove::Step& step : lane.steps) {
            steps.append(QJsonArray{step.timing, step.timingMs, step.velocity, step.length});
        }
        lanes.append(QJsonObject{{QStringLiteral("keys"), keys}, {QStringLiteral("steps"), steps}});
    }
    return {{QStringLiteral("step"), groove.stepBeats},
            {QStringLiteral("bars"), groove.bars},
            {QStringLiteral("base"), groove.base},
            {QStringLiteral("lanes"), lanes}};
}

std::optional<groove::Groove> grooveFromJson(const QJsonValue& value) {
    if (!value.isObject()) return std::nullopt;
    const QJsonObject data = value.toObject();
    groove::Groove groove;
    groove.stepBeats = numberOr(data, QStringLiteral("step"), 0.25);
    groove.bars = static_cast<int>(std::clamp(numberOr(data, QStringLiteral("bars"), 1.0), 1.0, 64.0));
    groove.base = numberOr(data, QStringLiteral("base"), groove.stepBeats);
    groove.lanes.clear();
    for (const QJsonValue& entry : data.value(QStringLiteral("lanes")).toArray()) {
        const QJsonObject l = entry.toObject();
        groove::Lane lane;
        for (const QJsonValue& key : l.value(QStringLiteral("keys")).toArray()) {
            // (A key, 0..127; others aren't, and couldn't all be an int.)
            if (const double k = key.toDouble(-1.0); k >= 0.0 && k <= 127.0) lane.keys.push_back(static_cast<int>(k));
        }
        for (const QJsonValue& s : l.value(QStringLiteral("steps")).toArray()) {
            const QJsonArray step = s.toArray();
            const auto at = [&step](int i, double fallback) {
                return i < step.size() && step.at(i).isDouble() ? step.at(i).toDouble() : fallback;
            };
            lane.steps.push_back({at(0, 0.0), at(1, 0.0), at(2, 0.0), at(3, 1.0)});
        }
        groove.lanes.push_back(std::move(lane));
    }
    return groove::normalized(std::move(groove));
}

QJsonArray grooveNotesToJson(const std::vector<groove::NoteTime>& notes) {
    QJsonArray out;
    for (const groove::NoteTime& note : notes) out.append(QJsonArray{note.beat, note.key, note.velocity, note.lengthBeats});
    return out;
}

std::vector<groove::NoteTime> grooveNotesFromJson(const QJsonValue& value) {
    std::vector<groove::NoteTime> notes;
    for (const QJsonValue& entry : value.toArray()) {
        const QJsonArray n = entry.toArray();
        if (n.size() < 3 || !n.at(0).isDouble() || !std::isfinite(n.at(0).toDouble())) continue;
        notes.push_back({n.at(0).toDouble(), std::clamp(n.at(1).toInt(60), 0, 127),
                         std::clamp(n.at(2).toDouble(100.0), 1.0, 127.0), n.size() > 3 ? n.at(3).toDouble(0.0) : 0.0});
    }
    return notes;
}

QJsonObject libraryGrooveToJson(const LibraryGroove& groove) {
    QJsonObject data{{QStringLiteral("id"), groove.id},
                     {QStringLiteral("name"), groove.name},
                     {QStringLiteral("groove"), grooveToJson(groove.groove)}};
    if (!groove.notes.empty()) data[QStringLiteral("notes")] = grooveNotesToJson(groove.notes);
    return data;
}

std::optional<LibraryGroove> libraryGrooveFromJson(const QJsonValue& value) {
    const QJsonObject data = value.toObject();
    const std::optional<groove::Groove> groove = grooveFromJson(data.value(QStringLiteral("groove")));
    if (!groove) return std::nullopt;
    return LibraryGroove{data.value(QStringLiteral("id")).toString(), data.value(QStringLiteral("name")).toString(),
                         *groove, grooveNotesFromJson(data.value(QStringLiteral("notes")))};
}

QJsonObject grooveSourceToJson(const GrooveSource& source) {
    QJsonObject data{{QStringLiteral("source"), grooves::kindName(source.kind)}};
    switch (source.kind) {
        case GrooveSource::Kind::Swing:
            data[QStringLiteral("swing")] = source.swing;
            data[QStringLiteral("step")] = source.stepBeats;
            break;
        case GrooveSource::Kind::Library: data[QStringLiteral("groove_id")] = source.libraryId; break;
        case GrooveSource::Kind::Own:
            if (source.own) data[QStringLiteral("groove")] = grooveToJson(*source.own);
            break;
        case GrooveSource::Kind::Project: break;
    }
    return data;
}

std::optional<GrooveSource> grooveSourceFromJson(const QJsonValue& value) {
    if (!value.isObject()) return std::nullopt;
    const QJsonObject data = value.toObject();
    const std::optional<GrooveSource::Kind> kind = grooves::kindFromName(data.value(QStringLiteral("source")).toString());
    if (!kind) return std::nullopt;
    GrooveSource source;
    source.kind = *kind;
    switch (*kind) {
        case GrooveSource::Kind::Swing:
            source = grooves::swingSource(numberOr(data, QStringLiteral("swing"), groove::kMinSwing),
                                          numberOr(data, QStringLiteral("step"), 0.25));
            break;
        case GrooveSource::Kind::Library:
            source.libraryId = data.value(QStringLiteral("groove_id")).toString();
            if (source.libraryId.isEmpty()) return std::nullopt;
            break;
        case GrooveSource::Kind::Own:
            source.own = grooveFromJson(data.value(QStringLiteral("groove")));
            if (!source.own) return std::nullopt;
            break;
        case GrooveSource::Kind::Project: break;
    }
    return source;
}

QJsonObject clipGrooveToJson(const ClipGroove& groove) {
    QJsonObject data = grooveSourceToJson(groove.source);
    data[QStringLiteral("timing")] = groove.amounts.timing;
    data[QStringLiteral("velocity")] = groove.amounts.velocity;
    data[QStringLiteral("random")] = groove.amounts.random;
    data[QStringLiteral("quantize")] = groove.amounts.quantize;
    if (groove.amounts.seed != 0) data[QStringLiteral("seed")] = static_cast<double>(groove.amounts.seed);
    return data;
}

std::optional<ClipGroove> clipGrooveFromJson(const QJsonValue& value) {
    const std::optional<GrooveSource> source = grooveSourceFromJson(value);
    if (!source) return std::nullopt;
    const QJsonObject data = value.toObject();
    ClipGroove groove;
    groove.source = *source;
    groove.amounts.timing = std::clamp(numberOr(data, QStringLiteral("timing"), 1.0), 0.0, groove::kMaxTimingAmount);
    groove.amounts.velocity = std::clamp(numberOr(data, QStringLiteral("velocity"), 1.0), -1.0, 1.0);
    groove.amounts.random = std::clamp(numberOr(data, QStringLiteral("random"), 0.0), 0.0, 1.0);
    groove.amounts.quantize = std::clamp(numberOr(data, QStringLiteral("quantize"), 0.0), 0.0, 1.0);
    groove.amounts.length = 0.0;
    groove.amounts.seed = static_cast<uint32_t>(std::clamp(numberOr(data, QStringLiteral("seed"), 0.0), 0.0, 4294967295.0));
    return groove;
}

void saveGrooveFile(const LibraryGroove& groove, const QString& path) {
    QJsonObject data{{QStringLiteral("format"), kGrooveFormat},
                     {QStringLiteral("version"), kGrooveVersion},
                     {QStringLiteral("name"), groove.name},
                     {QStringLiteral("groove"), grooveToJson(groove.groove)}};
    if (!groove.notes.empty()) data[QStringLiteral("notes")] = grooveNotesToJson(groove.notes);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        throw ProjectFileError(QStringLiteral("Could not write %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
    file.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        throw ProjectFileError(QStringLiteral("Could not write %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
}

LibraryGroove loadGrooveFile(const QString& path) {
    const QString name = QFileInfo(path).fileName();
    const QString stem = QFileInfo(path).completeBaseName();
    if (path.endsWith(kAgrExtension, Qt::CaseInsensitive)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            throw ProjectFileError(QStringLiteral("Could not read %1: %2").arg(name, file.errorString()));
        }
        const std::unique_ptr<live::Element> root =
            live::parseAbleton(file.readAll(), name, {}, QStringLiteral("an Ableton groove"));
        const live::Element* element = root->child(u"GrooveFile");
        if (!element) throw ProjectFileError(QStringLiteral("%1 is not an Ableton groove").arg(name));
        AgrGroove read = agrGroove(*element);
        // Its Timing and Velocity worked into it (a library groove keeps no amounts).
        for (groove::Lane& lane : read.groove.lanes) {
            for (groove::Step& step : lane.steps) {
                step.timing *= read.amounts.timing;
                step.timingMs *= read.amounts.timing;
                step.velocity *= read.amounts.velocity;
            }
        }
        return {newId(), stem, groove::normalized(read.groove), read.notes};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw ProjectFileError(QStringLiteral("Could not read %1: %2").arg(name, file.errorString()));
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) damaged(name, error.errorString());
    const QJsonObject data = document.object();
    if (data.value(QStringLiteral("format")).toString() != kGrooveFormat) {
        throw ProjectFileError(QStringLiteral("%1 is not a SUBstation groove").arg(name));
    }
    if (data.value(QStringLiteral("version")).toInt() > kGrooveVersion) {
        throw ProjectFileError(QStringLiteral("%1 was saved by a newer version of SUBstation").arg(name));
    }
    const std::optional<groove::Groove> groove = grooveFromJson(data.value(QStringLiteral("groove")));
    if (!groove) damaged(name, QStringLiteral("it has no groove"));
    return {newId(), stem, *groove, grooveNotesFromJson(data.value(QStringLiteral("notes")))};
}

bool isGrooveFile(const QString& path) {
    return path.endsWith(kGrooveExtension, Qt::CaseInsensitive) || path.endsWith(kAgrExtension, Qt::CaseInsensitive);
}

Device grooveDevice(const LibraryGroove& groove) {
    Device device = newDevice(grooves::kDeviceKind);
    GrooveSource own;
    own.kind = GrooveSource::Kind::Own;
    own.own = groove.groove;
    device.state = grooves::deviceState(std::nullopt, own, {}, device.id);
    return device;
}

AgrGroove agrGroove(const live::Element& element) {
    AgrGroove out;
    out.name = element.value(u"Name").value_or(QString()).trimmed();
    // Base: Live's list, 1/4 to 1/32.
    static constexpr double kBases[] = {1.0, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};
    const int base = std::clamp(element.integer(u"Base", 3), 0, static_cast<int>(std::size(kBases)) - 1);
    const double step = kBases[base];
    // Amounts: Live writes percents (fractions in some versions: up to 1).
    const auto amount = [&element](QStringView path, double fallback) {
        const double value = element.number(path, fallback);
        return std::abs(value) > 1.0 + 1e-9 ? value / 100.0 : value;
    };
    out.amounts.timing = std::clamp(amount(u"Timing", 1.0), 0.0, groove::kMaxTimingAmount);
    out.amounts.velocity = std::clamp(amount(u"Velocity", 0.0), -1.0, 1.0);
    out.amounts.random = std::clamp(amount(u"Random", 0.0), 0.0, 1.0);
    const double quantize = element.at(u"Quantize") ? amount(u"Quantize", 0.0) : amount(u"Quantization", 0.0);
    out.amounts.quantize = std::clamp(quantize, 0.0, 1.0);
    // Its clip: the notes, from its loop's (or its own) start.
    const live::Element* clip = element.at(u"Clip/Value/MidiClip");
    if (const live::Element* holder = element.child(u"Clip"); !clip && holder) {
        const std::vector<const live::Element*> found = holder->descendants(u"MidiClip");
        if (!found.empty()) clip = found.front();
    }
    std::vector<groove::NoteTime> notes;
    double length = 4.0;
    if (clip) {
        const double start = clip->number(u"Loop/LoopStart", clip->number(u"CurrentStart", 0.0));
        const double end = clip->number(u"Loop/LoopEnd", clip->number(u"CurrentEnd", start + 4.0));
        length = std::max(end - start, step);
        if (out.name.isEmpty()) out.name = clip->value(u"Name").value_or(QString()).trimmed();
        if (const live::Element* keyTracks = clip->at(u"Notes/KeyTracks")) {
            for (const live::Element* keyTrack : keyTracks->all(u"KeyTrack")) {
                const int key = keyTrack->integer(u"MidiKey", 60);
                const live::Element* events = keyTrack->child(u"Notes");
                if (!events) continue;
                for (const live::Element* event : events->all(u"MidiNoteEvent")) {
                    if (event->attribute(u"IsEnabled") == u"false") continue;
                    notes.push_back({event->numberAttribute(u"Time") - start, key,
                                     std::clamp(event->numberAttribute(u"Velocity", 100.0), 1.0, 127.0),
                                     event->numberAttribute(u"Duration", step)});
                }
            }
        }
    }
    const int bars = length > 8.0 + 1e-9 ? 4 : length > 4.0 + 1e-9 ? 2 : 1;
    out.groove = groove::fromNotes(notes, step, bars, false);
    out.groove.base = step;
    out.groove = groove::normalized(std::move(out.groove));
    out.notes = grooves::folded(std::move(notes), out.groove);
    return out;
}

}  // namespace sub::app
