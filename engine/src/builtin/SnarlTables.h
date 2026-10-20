#pragma once
// Snarl's wavetables: what its oscillators read. Each is a run of single-cycle
// frames that Wave moves along (0 % the first, 100 % the last):
//
// - Basic: Wave's shapes, sine, triangle, saw, square and a narrow pulse, from
//   their Fourier series (the keys at 0, 25, 50, 75 and 100 %).
// - Voice, FM, Hollow and Dirty: sixteen Adventure Kid single cycles each (CC0:
//   third_party/akwf, SnarlTablesData.h), dark to bright: human voices, FM
//   synths, overtones without a fundamental, distorted waves.
//
// Between two keys (shapes or AKWF waves) lie kStepsBetween - 1 more frames,
// blended in frequency: each harmonic's level linearly, its phase the short way
// round. (Crossfaded as samples, two waves whose harmonics differ in phase cancel
// there, and a scan through them phases; blended so, they never cancel.) Each
// AKWF wave is first turned to line up with the one before it (the shift of its
// cycle that correlates best), so that blend travels as little phase as it can,
// and brought to the same loudness.
//
// Each frame is kept band-limited at kLevels levels, an octave apart (512 harmonics,
// 256, ... 1), each a cycle of 4 samples a harmonic (64 at least), read with
// 4-point Hermite interpolation. A read takes the two levels either side of what
// its note allows, harmonics up to kLimitHz, crossfaded by where it lies between
// them, so it holds harmonics up to 2 kLimitHz at most: past what is heard, under
// what an oversampled oscillator's rate can hold, so nothing folds back (Snarl's
// oversampling filters take what is past the audible band out). A read is two
// frames by two levels: four Hermite reads.
//
// Built the first time instance() is called (on the main thread, in prepare():
// a few milliseconds, some 5 MB shared by every Snarl); read from any thread after.

#include <array>
#include <string>
#include <vector>

namespace sub::snarl {

enum class Table { Basic, Voice, FM, Hollow, Dirty };
inline constexpr int kTables = 5;
const std::vector<std::string>& tableLabels();

class Wavetables {
public:
    static constexpr int kTopHarmonics = 512;
    static constexpr int kLevels = 10;
    static constexpr int kStepsBetween = 4;
    static constexpr double kLimitHz = 20000.0;
    static constexpr double kLoudness = 0.5;  // an AKWF frame's RMS (a saw's is 0.577)

    static const Wavetables& instance();

    int frames(int table) const noexcept { return tables_[static_cast<size_t>(table)].frames; }
    // A level's cycle length (samples) and the harmonics it holds.
    static int levelLength(int level) noexcept;
    static int levelHarmonics(int level) noexcept { return kTopHarmonics >> level; }

    // `table` at `position` (0..1 along its frames) and phase `t` (0..1), band-limited for a note of
    // `harmonicLimit` harmonics under kLimitHz (kLimitHz over its frequency).
    float read(int table, double position, double t, double harmonicLimit) const noexcept;

    // One cycle of `table` at `position` from its fullest level, `points` values: what an editor draws.
    std::vector<float> cycle(int table, double position, int points) const;

private:
    Wavetables();

    struct Data {
        int frames = 0;
        // Each level's cycles, frame after frame, each with a sample before it and three after (its end and
        // start again), so a Hermite read needs no wrapping.
        std::array<std::vector<float>, kLevels> levels;
    };
    float sample(const Data& data, int level, int frame, double t) const noexcept;

    std::array<Data, kTables> tables_;
};

}  // namespace sub::snarl
