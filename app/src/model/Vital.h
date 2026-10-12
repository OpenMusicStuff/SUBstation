#pragma once
// A built-in Vital device as the application sees it besides its parameters:
// its modulation routing and its preset's names, read from its state (the
// engine's builtin/VitalPatch.h has the format), and the sources and
// destinations Vital's engine has, as the editor lists them. The edits are the
// editor's (ProjectEditor::setVitalModulation, addVitalModulation).

#include <QString>
#include <QStringList>

#include <vector>

#include "model/Device.h"

namespace sub::app {

// The device kind of a Vital ("vital").
QString vitalKind();
bool isVital(const Device& device);

struct VitalModulation {
    int slot = 0;  // 0..63: its amount is the parameter "modulation_<slot + 1>_amount"
    QString source;
    QString destination;
};
// Its modulations in use (a source and a destination), by slot.
std::vector<VitalModulation> vitalModulations(const Device& device);
// Every slot as the state has it ("" both: free); 64 of them.
std::vector<VitalModulation> vitalModulationSlots(const Device& device);

// Every source and destination Vital's engine has, sorted.
QStringList vitalSources();
QStringList vitalDestinations();
// What they are called: "LFO 1"; a destination by its parameter's name ("Oscillator 1 Level").
QString vitalSourceName(const QString& source);
QString vitalDestinationName(const QString& destination);
// A slot's parameter: "modulation_<slot + 1>_<what>" (what: amount, bipolar, power, stereo, bypass).
QString vitalSlotParam(int slot, const QString& what);
inline constexpr int kVitalModulationSlots = 64;

// A state value of its patch ("preset_name", "author", "macro1"...; "" if none).
QString vitalStateValue(const Device& device, const QString& key);

}  // namespace sub::app
