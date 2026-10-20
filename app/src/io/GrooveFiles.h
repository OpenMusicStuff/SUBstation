#pragma once
// Grooves in files: as the project file stores them (the library, the default
// groove, a clip's groove: JSON), a groove file of the user's library
// (.gilgroove: one groove and its name, JSON), and Ableton's groove files (.agr:
// XML, a MIDI clip's notes with Base, Quantize, Timing, Random and Velocity).
//
// A groove as JSON: {"step": beats, "bars": n, "base": beats, "lanes": [{"keys":
// [...], "steps": [[timing, ms, velocity, length], ...]}]}, normalized as it is
// read (groove::normalized()). A source: {"source": "swing" | "project" |
// "library" | "own", "swing": percent, "step": beats, "groove_id": id,
// "groove": a groove}, only what its kind uses; a clip's groove is its source
// with its amounts ("timing", "velocity", "random", "quantize", "seed").
//
// An .agr file's groove is made from its clip's notes (groove::fromNotes, on
// its Base grid, over the clip's bars): its Timing and Velocity are worked into
// the groove (offsets and velocities scaled by them); its Quantize and Random,
// which a library groove doesn't keep, are read too (agrGroove()'s amounts), for
// a Live Set's clips that play it.
//
// A groove file in the preset library is a Groove device's preset (io/Presets.h):
// loaded as a preset, it is a Groove device playing it as its own
// (grooveDevice()).
//
// Reading throws ProjectFileError (model/Errors.h) with a message for the user.

#include "model/Device.h"
#include "model/GrooveTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>
#include <vector>

namespace sub::app {

namespace live {
class Element;
}

inline const QString kGrooveFormat = QStringLiteral("gilstudio-groove");
inline constexpr int kGrooveVersion = 1;
inline const QString kGrooveExtension = QStringLiteral(".gilgroove");
inline const QString kAgrExtension = QStringLiteral(".agr");

QJsonObject grooveToJson(const groove::Groove& groove);
std::optional<groove::Groove> grooveFromJson(const QJsonValue& value);
// The notes a library groove was made from: [[beat, key, velocity, length], ...].
QJsonArray grooveNotesToJson(const std::vector<groove::NoteTime>& notes);
std::vector<groove::NoteTime> grooveNotesFromJson(const QJsonValue& value);
// A library groove as the project file (and a groove file) holds it: {"id", "name", "groove", "notes"}.
QJsonObject libraryGrooveToJson(const LibraryGroove& groove);
std::optional<LibraryGroove> libraryGrooveFromJson(const QJsonValue& value);
QJsonObject grooveSourceToJson(const GrooveSource& source);
std::optional<GrooveSource> grooveSourceFromJson(const QJsonValue& value);
QJsonObject clipGrooveToJson(const ClipGroove& groove);
std::optional<ClipGroove> clipGrooveFromJson(const QJsonValue& value);

// A groove file of the user's library (.gilgroove), written whole or not at all.
void saveGrooveFile(const LibraryGroove& groove, const QString& path);
// A groove file (.gilgroove or .agr) as a library groove: a fresh id, named as its file is.
LibraryGroove loadGrooveFile(const QString& path);

// Ableton's groove (the GrooveFile element of an .agr, or a Groove of a Live Set's pool): its
// groove, and the amounts it is played at (Timing, Velocity, Random, Quantize as fractions).
struct AgrGroove {
    QString name;
    groove::Groove groove;
    groove::Amounts amounts;
    std::vector<groove::NoteTime> notes;  // its clip's, folded into the groove's cycle
};
AgrGroove agrGroove(const live::Element& element);

// Whether a file is a groove file (.gilgroove, .agr), by its name.
bool isGrooveFile(const QString& path);
// A new Groove device playing a groove as its own (a groove file loaded as a preset).
Device grooveDevice(const LibraryGroove& groove);

}  // namespace sub::app
