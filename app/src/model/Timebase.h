#pragma once
// Musical time helpers. Timeline positions are quarter-note beats.

#include <QString>

#include "music/TempoMap.h"

#include <array>
#include <memory>
#include <optional>

namespace sub::app {

inline constexpr std::array<int, 6> kValidDenominators{1, 2, 4, 8, 16, 32};

struct TimeSignature {
    int numerator = 4;
    int denominator = 4;

    // Bar length in quarter-note beats.
    double beatsPerBar() const { return numerator * 4.0 / denominator; }
    // Length of one signature beat (the denominator note) in quarter notes.
    double beatLength() const { return 4.0 / denominator; }
    // "6/8".
    QString toString() const;

    friend bool operator==(const TimeSignature&, const TimeSignature&) = default;
};

// A position split into zero-based bar, beat and sixteenth.
struct BarPosition {
    int bar = 0;
    int beat = 0;
    int sixteenth = 0;

    friend bool operator==(const BarPosition&, const BarPosition&) = default;
};

double beatsToSeconds(double beats, double tempo);
double secondsToBeats(double seconds, double tempo);

// The song's tempo as the model hands it about: its tempo map (music/TempoMap.h), cheap to copy; made
// from a BPM (implicitly) where one tempo is all there is. Lengths depend on where they are: `beats`
// from beat `from` last secondsFor(from, beats). One tempo throughout converts as it always has
// (beats * 60 / tempo), to the bit.
class Tempo {
public:
    Tempo() : Tempo(120.0) {}
    Tempo(double bpm);  // NOLINT(google-explicit-constructor): one tempo throughout
    Tempo(const TempoMap& map);  // NOLINT(google-explicit-constructor)

    const TempoMap& map() const { return *map_; }
    bool constant() const { return constant_; }
    double bpm() const { return bpm_; }  // at beat 0
    double bpmAt(double beat) const { return constant_ ? bpm_ : map_->bpmAt(beat); }
    double secondsAt(double beat) const { return constant_ ? beatsToSeconds(beat, bpm_) : map_->secondsAt(beat); }
    double beatAt(double seconds) const { return constant_ ? secondsToBeats(seconds, bpm_) : map_->beatAt(seconds); }
    // How long `beats` from `from` last (negative beats: the stretch before it, negative seconds).
    double secondsFor(double from, double beats) const;
    // How many beats from `from` last `seconds` (negative seconds: back from it, negative beats).
    double beatsFor(double from, double seconds) const;

    friend bool operator==(const Tempo& a, const Tempo& b) { return a.map() == b.map(); }

private:
    std::shared_ptr<const TempoMap> map_;
    double bpm_ = 120.0;
    bool constant_ = true;
};

// Zero-based (bar, beat, sixteenth) for a beat position. (By a MeterMap: its bars,
// each with its own signature; one time signature throughout counts as it always has.)
BarPosition splitPosition(double beats, const TimeSignature& ts);
BarPosition splitPosition(double beats, const MeterMap& meter);
// Ableton-style "bars.beats.sixteenths", one-based.
QString formatPosition(double beats, const TimeSignature& ts);
QString formatPosition(double beats, const MeterMap& meter);
// The inverse of formatPosition; missing parts default to 1. None if unparsable.
std::optional<double> parsePosition(const QString& text, const TimeSignature& ts);
std::optional<double> parsePosition(const QString& text, const MeterMap& meter);
bool isMultiple(double value, double step);
// A beat as a bar number, from 1, with the fraction of its bar it is into it (bar 17 is 17, halfway
// through it 17.5), and back. One time signature throughout: 1 + beat / beats a bar, as it always was.
double barNumberAt(double beat, const MeterMap& meter);
double beatOfBarNumber(double bar, const MeterMap& meter);
// Ruler label: "5" on a bar line, "5.3" on a beat, "5.3.2" below that.
QString formatBarLabel(double beats, const TimeSignature& ts);
QString formatBarLabel(double beats, const MeterMap& meter);

// -70 dB (the faders' floor) and below is silence.
double dbToGain(double db);
double gainToDb(double gain);
QString formatDb(double db);
// "25L", "30R", "C" or a plain -50..50 number (negative = left) as -1..1.
std::optional<double> parsePan(const QString& text);
QString formatPan(double pan);

}  // namespace sub::app
