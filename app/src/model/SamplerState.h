#pragma once
// A Sampler's state besides its parameters (DeviceState.h's values): its sample,
// and the reference its similar sounds are found from. The similar-sound arrows
// (SimilarSamples.h) step through the sounds most like the reference, best
// first; the reference is the sample they started from, unless one was saved
// (right-click › Save as Similar Reference), which stays until another is saved,
// whatever sample is loaded meanwhile. Pure functions, for the editor, the
// stepping and the tests.

#include "model/DeviceState.h"

#include <QString>
#include <QStringList>

namespace sub::app::samplerState {

inline const QString kSample = QStringLiteral("sample");
inline const QString kReference = QStringLiteral("similar_reference");
inline const QString kReferenceSaved = QStringLiteral("similar_reference_saved");  // "1": saved by hand

QString sample(const deviceState::Values& values);
// The reference similar sounds come from ("" none yet: the sample itself, when stepping starts).
QString reference(const deviceState::Values& values);
bool referenceSaved(const deviceState::Values& values);

// After loading a sample by hand (a drop, a file dialog; "": cleared): a
// reference stepping started from goes with the sample it came from, a saved one stays.
deviceState::Values loaded(deviceState::Values values, const QString& path);
// After stepping to a similar sound: its reference is kept (the sample stepped
// from becomes it if there was none).
deviceState::Values stepped(deviceState::Values values, const QString& path);
// The current sample saved as the reference.
deviceState::Values referenceSet(deviceState::Values values);

// The sound `direction` (+1 next, -1 previous) steps from `current` to among
// `best` (the reference's most similar sounds, best first; not the reference
// itself): next from the reference (or from a sound not among them) is the
// best; previous from the best is the reference itself. "" where there is none.
QString step(const QStringList& best, const QString& reference, const QString& current, int direction);

}  // namespace sub::app::samplerState
