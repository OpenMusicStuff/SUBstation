#pragma once
// Parameters, whatever they belong to: a track's or the master's mixer, a
// built-in device, or a plug-in of any format.
//
// A ParamSpec describes one: its name, range, units and how its values read.
// (A rack's macros and its chains' faders are its parameters, as far as
// automation goes: macroSpecs, chainSpecs.) It maps plain values (in the parameter's own units) to and from
// normalized ones (0..1), which is how automation stores them. Device
// parameters come from the engine's ParamInfo (ParamSpec::fromInfo) and map as
// it does; the mixer's controls (volume, pan and sends) are described here
// (mixerSpecs). The UI shows every kind alike.

#include <QString>
#include <QStringList>

#include <functional>
#include <utility>
#include <vector>

namespace sub {
struct ParamInfo;
}

namespace sub::app {

// A built-in device's value, in its units ("dB", "%", "Hz", "ms", "st", "ct", "note",
// ":1", "#": a count, "beats": a length, "1 Beat", "3 Beats", "2 Bars").
QString formatValue(double value, const QString& unit);

struct ParamSpec {
    enum class Scale {
        Linear,
        Log,    // moves evenly in log(value)
        Fader,  // a mixer's volume
    };

    QString key;  // its automation key (see Automation.h)
    QString name;
    QString group;  // "Mixer", or its device's name
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    QString unit;
    Scale scale = Scale::Linear;
    int steps = 0;  // > 0: discrete, this many steps from minimum to maximum
    QStringList labels;  // names of the steps of a list
    // Plain value -> text; by default from the units (or the labels).
    std::function<QString(double)> text;
    // How its value is shown when that isn't the value itself (Vital's knobs: a
    // level squared, a time to the fourth, a cutoff's note in hertz;
    // sub::ParamDisplay): plain value -> the number shown, and back (NaN: none
    // shows so), in `shownUnit`. None: shown as it is, in `unit`.
    std::function<double(double)> shown;
    std::function<double(double)> fromShown;
    QString shownUnit;

    // A device parameter, from the engine's ParamInfo.
    static ParamSpec fromInfo(const sub::ParamInfo& info, const QString& key, const QString& group,
                              std::function<QString(double)> text = {});

    bool discrete() const { return steps > 0; }
    bool showsOtherwise() const { return static_cast<bool>(shown); }
    // The same mapping as the engine's ParamInfo (and the mixer's in Automation.h).
    double toNormalized(double plain) const;
    double fromNormalized(double value) const;
    // A normalized value as the parameter can take it.
    double quantize(double value) const;
    QString format(double plain) const;
    QString formatNormalized(double value) const;

    // (Its text and shown functions aren't compared; the unit they show in is.)
    bool operator==(const ParamSpec& other) const;
};

// The mixer controls of a track (or the master) that can be automated: its
// volume and pan, its activator (not the master's), and its sends to `sends`
// ((return id, letter) each).
std::vector<ParamSpec> mixerSpecs(bool master = false, const std::vector<std::pair<QString, QString>>& sends = {});
// A switch: Off (0) or On (1), on by default (a track's activator, a device's on/off).
ParamSpec switchSpec(const QString& key, const QString& name, const QString& group);
// The master's Song Tempo lane (automation::kSongTempo): BPM, linear from `low` to `high`
// (Project::tempoRange()), in the group "Song".
ParamSpec tempoSpec(double low, double high);
// A device's on/off ("Device On"), of the device named `group`.
ParamSpec deviceOnSpec(const QString& deviceId, const QString& group);
// A send's level to a return (lettered as it shows), automated as a volume is.
ParamSpec sendSpec(const QString& returnId, const QString& letter);
// A rack's macros (their names, in order): 0..1, shown in percent.
std::vector<ParamSpec> macroSpecs(const QString& rackId, const QStringList& names, const QString& group);
// A rack's chains' faders ((chain id, name) each): volume and pan, automated as a track's are.
std::vector<ParamSpec> chainSpecs(const QString& rackId, const std::vector<std::pair<QString, QString>>& chains,
                                  const QString& group);

}  // namespace sub::app
