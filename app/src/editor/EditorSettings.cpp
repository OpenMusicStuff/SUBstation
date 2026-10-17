// Editing the project's settings: tempo, time signature, key and loop.

#include "editor/ProjectEditor.h"

#include "model/Commands.h"
#include "model/Edits.h"
#include "model/Numbers.h"

#include <QUndoStack>

#include <algorithm>

namespace sub::app {

void ProjectEditor::setSettings(const QString& text, const SettingsValues& values, const QString& mergeKey) {
    SettingsValues old;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) old.insert(it.key(), project_->setting(it.key()));
    if (old != values) push(std::make_unique<UpdateSettingsCommand>(project_, old, values, text, mergeKey));
}

void ProjectEditor::setTempo(double bpm, const QString& mergeKey) {
    const Project& p = *project_;
    const double tempo = std::clamp(formatFixed(bpm, 2).toDouble(), 20.0, 999.0);  // (rounded as Python's round(bpm, 2))
    if (tempo == p.tempo()) return;
    if (p.tempoMap().constant()) {
        setTempoMap(Tempo(tempo), mergeKey);
        return;
    }
    // A tempo that changes along the song: all of it in proportion, so it starts at this one.
    const double scale = tempo / p.tempo();
    std::vector<TempoMap::Point> points = p.tempoMap().map().points();
    for (TempoMap::Point& point : points) point.bpm *= scale;
    setTempoMap(Tempo(TempoMap(std::move(points))), mergeKey);
}

std::pair<TempoState, TempoState> ProjectEditor::tempoChange(const Tempo& tempo, const QString& mergeKey) const {
    const Project& p = *project_;
    ClipLists current;
    for (const Track& t : p.tracks()) current.insert(t.id, t.clips);
    // Within one drag, fit from the clips as they were when the drag began, so
    // going up and back down doesn't leave clips trimmed.
    ClipLists baseline = current;
    const int index = undoStack_->index();
    const QUndoCommand* last = index > 0 ? undoStack_->command(index - 1) : nullptr;
    std::optional<TempoState> began;
    if (const auto* set = dynamic_cast<const SetTempoCommand*>(last); set && set->mergeKey() == mergeKey) {
        began = set->oldValue();
    } else if (const auto* lanes = dynamic_cast<const SetEnvelopesCommand*>(last);
               lanes && lanes->mergeKey() == mergeKey && lanes->tempo()) {
        began = lanes->tempo()->first;
    }
    if (!mergeKey.isEmpty() && began && began->clips.keys() == current.keys()) baseline = began->clips;
    ClipLists fitted;
    for (auto it = baseline.constBegin(); it != baseline.constEnd(); ++it) {
        fitted.insert(it.key(), edits::fitToTempo(it.value(), tempo));
    }
    return {TempoState{p.tempoMap(), current}, TempoState{tempo, fitted}};
}

void ProjectEditor::setTempoMap(const Tempo& tempo, const QString& mergeKey) {
    if (tempo == project_->tempoMap()) return;
    auto [old, nw] = tempoChange(tempo, mergeKey);
    push(std::make_unique<SetTempoCommand>(project_, std::move(old), std::move(nw), mergeKey));
}

void ProjectEditor::setMeter(const MeterMap& meter) {
    if (meter == project_->meter()) return;
    setSettings(QStringLiteral("Change Time Signature"), {{SettingsField::Meter, meter}});
}

void ProjectEditor::setTimeSignatureAt(double beat, int numerator, int denominator) {
    if (!MeterMap::valid(numerator, denominator)) return;
    // At the start of the bar it is in: a change starts a bar.
    const MeterMap& meter = project_->meter();
    const double at = meter.barAt(std::max(0.0, beat)).start;
    std::vector<MeterMap::Change> changes = meter.changes();
    std::erase_if(changes, [&](const MeterMap::Change& c) { return std::abs(c.beat - at) < 1e-9; });
    changes.push_back({at, numerator, denominator});
    setMeter(MeterMap(std::move(changes)));
}

void ProjectEditor::removeTimeSignatureAt(double beat) {
    std::vector<MeterMap::Change> changes = project_->meter().changes();
    if (changes.size() < 2 || beat < 1e-9) return;  // (the song's own, at its start, stays)
    std::erase_if(changes, [&](const MeterMap::Change& c) { return std::abs(c.beat - beat) < 1e-9; });
    setMeter(MeterMap(std::move(changes)));
}

void ProjectEditor::setTimeSignature(const TimeSignature& timeSignature) {
    const MeterMap& meter = project_->meter();
    if (meter.constant()) {
        setSettings(QStringLiteral("Change Time Signature"), {{SettingsField::TimeSignature, timeSignature}});
        return;
    }
    // Changes along the song: the one at its start.
    std::vector<MeterMap::Change> changes = meter.changes();
    changes.front().numerator = timeSignature.numerator;
    changes.front().denominator = timeSignature.denominator;
    setSettings(QStringLiteral("Change Time Signature"), {{SettingsField::Meter, MeterMap(std::move(changes))}});
}

void ProjectEditor::setTimeSignature(int numerator, int denominator) {
    if (numerator < 1 ||
        std::find(kValidDenominators.begin(), kValidDenominators.end(), denominator) == kValidDenominators.end()) {
        return;  // (not a time signature)
    }
    setTimeSignature(TimeSignature{numerator, denominator});
}

void ProjectEditor::setKey(const std::optional<Key>& key) {
    setSettings(QStringLiteral("Change Key"), {{SettingsField::Key, key}});
}

void ProjectEditor::setKeyByName(const QString& name) { setKey(keyFromName(name)); }

void ProjectEditor::setLoop(bool enabled, double start, double end, const QString& mergeKey) {
    start = std::max(0.0, start);
    end = std::max(start + 0.25, end);
    setSettings(QStringLiteral("Change Loop"),
                {{SettingsField::LoopEnabled, enabled}, {SettingsField::LoopStart, start}, {SettingsField::LoopEnd, end}},
                mergeKey);
}

void ProjectEditor::setLoopEnabled(bool enabled) {
    setSettings(QStringLiteral("Toggle Loop"), {{SettingsField::LoopEnabled, enabled}});
}

void ProjectEditor::setPunchIn(bool enabled) {
    setSettings(QStringLiteral("Toggle Punch In"), {{SettingsField::PunchIn, enabled}});
}

void ProjectEditor::setPunchOut(bool enabled) {
    setSettings(QStringLiteral("Toggle Punch Out"), {{SettingsField::PunchOut, enabled}});
}

}  // namespace sub::app
