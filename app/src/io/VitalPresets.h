#pragma once
// Vital's preset files (.vital) as built-in Vital devices, and back: the
// engine reads and writes them (builtin/VitalPatch.h); this turns them into a
// model Device (its parameters, and its state besides them as the device keeps
// it: model/DeviceState.h). A .vital file loads wherever a preset does
// (loadPreset: dropped on a track or a Vital, from the device's menu).

#include <QString>

#include "model/Device.h"

namespace sub::app {

inline const QString kVitalPresetExtension = QStringLiteral(".vital");

// Whether the path is a Vital preset's (by its extension).
bool isVitalPreset(const QString& path);
// The preset as a Vital device (a new device: no id). Throws ProjectFileError
// if it can't be read or isn't a Vital preset.
Device loadVitalPreset(const QString& path);
// A Vital device as a .vital file Vital opens. Throws ProjectFileError if it
// can't be written.
void saveVitalPreset(const Device& device, const QString& path);
// The text of a .vital preset from a Vital device (what saveVitalPreset writes).
QByteArray vitalPresetText(const Device& device);

// An oscillator's wavetable (as a Vital device's state keeps it) from a file:
// Vital's own (.vitaltable), or audio (a .wav, .flac...) cut into frames
// (`style` 0: Splice, a wavetable file's frames), or following its pitch (1:
// Vocode, 2: Pitch), as Vital loads one. Throws ProjectFileError if it can't.
QString vitalWavetableFromFile(const QString& path, int style = 0);
inline const QString kVitalTableExtension = QStringLiteral(".vitaltable");

}  // namespace sub::app
