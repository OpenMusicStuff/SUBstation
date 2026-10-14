#include "tagging/SoundTagger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "similarity/EssentiaExtractor.h"
#include "tagging/TaggerModel.inc"

namespace sub::intelligence::tagging {

namespace {

namespace m = model;
static_assert(m::kKinds == kKinds && m::kSources == 3 && m::kQualities == 10);
static_assert(m::kWords <= kMaxWords);

constexpr const char* kKindNames[kKinds] = {"kick", "snare", "clap", "closed hat", "open hat", "crash", "ride",
                                            "tom", "rim", "cowbell", "shaker", "tambourine", "percussion",
                                            "bass", "keys", "organ", "guitar", "mallet", "strings", "brass",
                                            "woodwind", "vocal", "synth"};

// The descriptors attributes are read from (train.py's DESCRIPTORS, in its order).
enum Descriptor { Brightness, AttackTime, Duration, SubBass, PitchNote };
static_assert(m::kDescriptors == 5);

constexpr int kDistortion = 2, kReverb = 8;  // NSynth's qualities, in its order
constexpr float kGradeRow = 0.4f;  // the likeliest kind's probability from which it is graded among that kind

float sigmoid(float x) { return 1.f / (1.f + std::exp(-x)); }

uint8_t byte(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); }

// Where `value` is among its kind's: 0.1 at the 20th percentile, 0.9 at the 80th.
float graded(float value, int row, Descriptor d) {
    const float* q = m::kQuantiles + (row * m::kDescriptors + d) * 2;
    const float mid = 0.5f * (q[0] + q[1]);
    const float half = std::max(0.5f * (q[1] - q[0]), 1e-4f);
    return sigmoid(2.2f * (value - mid) / half);  // (logit(0.9) = 2.2)
}

static_assert(m::kInputMajor);

// A layer: `w` input-major ([inputs][outputs]), so it is a sum of rows scaled by
// each input (which vectorizes), skipping the inputs a ReLU made 0 (half or so).
void dense(const float* w, const float* b, const float* in, int inputs, int outputs, float* out, bool relu) {
    std::copy(b, b + outputs, out);
    for (int i = 0; i < inputs; ++i) {
        const float x = in[i];
        if (x == 0.f) continue;
        const float* row = w + static_cast<size_t>(i) * static_cast<size_t>(outputs);
        for (int o = 0; o < outputs; ++o) out[o] += row[o] * x;
    }
    if (relu)
        for (int o = 0; o < outputs; ++o) out[o] = std::max(out[o], 0.f);
}

}  // namespace

const char* kindName(Kind kind) { return kind == Kind::None ? "" : kKindNames[static_cast<int>(kind)]; }

std::string kindTitle(Kind kind) {
    std::string s = kindName(kind);
    bool start = true;
    for (char& c : s) {
        if (start && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        start = c == ' ';
    }
    return s;
}

int wordCount() { return m::kWords; }

const char* wordName(int word) { return word >= 0 && word < m::kWords ? m::kWordNames[word] : ""; }

int wordIndex(std::string_view name) {
    for (int w = 0; w < m::kWords; ++w)
        if (name == m::kWordNames[w]) return w;
    return -1;
}

bool wordLearned(int word) { return word >= 0 && word < m::kWords && m::kWordLearned[word]; }

int outputCount() { return output::Words + m::kWords; }

int embeddingDims() { return m::kEmbedding; }

const char* modelId() { return m::kModelId; }

float kindThreshold() { return m::kKindThreshold; }

void infer(const float* descriptors, float* embedding, float* outputs) {
    float x[m::kInputs], h1[m::kHidden1], h2[m::kHidden2], e[m::kEmbedding];
    for (int i = 0; i < m::kInputs; ++i) {
        float v = descriptors[m::kInputFeature[i]];
        if (!std::isfinite(v)) v = m::kMean[i];
        v = std::clamp(v, m::kLow[i], m::kHigh[i]);
        x[i] = (v - m::kMean[i]) / m::kStd[i];
    }
    float logits[kKinds] = {}, loop = 0.f, sources[3] = {}, qualities[10] = {}, words[m::kWords] = {};
    float out[std::max(kKinds, m::kWords)];
    for (int k = 0; k < m::kMembers; ++k) {
        dense(m::kW1[k], m::kB1[k], x, m::kInputs, m::kHidden1, h1, true);
        dense(m::kW2[k], m::kB2[k], h1, m::kHidden1, m::kHidden2, h2, true);
        dense(m::kW3[k], m::kB3[k], h2, m::kHidden2, m::kEmbedding, e, true);
        if (k == 0) std::copy(e, e + m::kEmbedding, embedding);
        dense(m::kWKind[k], m::kBKind[k], e, m::kEmbedding, kKinds, out, false);
        for (int i = 0; i < kKinds; ++i) logits[i] += out[i];
        dense(m::kWLoop[k], m::kBLoop[k], e, m::kEmbedding, 1, out, false);
        loop += sigmoid(out[0]);
        dense(m::kWSource[k], m::kBSource[k], e, m::kEmbedding, 3, out, false);
        const float top = std::max({out[0], out[1], out[2]});
        float sum = 0.f;
        for (int i = 0; i < 3; ++i) sum += (out[i] = std::exp(out[i] - top));
        for (int i = 0; i < 3; ++i) sources[i] += out[i] / sum;
        dense(m::kWQuality[k], m::kBQuality[k], e, m::kEmbedding, 10, out, false);
        for (int i = 0; i < 10; ++i) qualities[i] += sigmoid(out[i]);
        dense(m::kWWords[k], m::kBWords[k], e, m::kEmbedding, m::kWords, out, false);
        for (int i = 0; i < m::kWords; ++i) words[i] += sigmoid(out[i]);
    }
    const float members = static_cast<float>(m::kMembers);
    // The kinds: the members' mean logits, temperature-scaled, softmax.
    float top = -1e30f;
    for (float& l : logits) top = std::max(top, l = l / members / m::kTemperature);
    float sum = 0.f;
    for (int i = 0; i < kKinds; ++i) sum += (outputs[output::Kinds + i] = std::exp(logits[i] - top));
    for (int i = 0; i < kKinds; ++i) outputs[output::Kinds + i] /= sum;
    outputs[output::Loop] = loop / members;
    for (int i = 0; i < 3; ++i) outputs[output::Sources + i] = sources[i] / members;
    for (int i = 0; i < 10; ++i) outputs[output::Qualities + i] = qualities[i] / members;
    for (int i = 0; i < m::kWords; ++i) outputs[output::Words + i] = words[i] / members;
}

float SoundTags::probabilityOf(uint32_t mask) const {
    float p = 0.f;
    for (const Guess& g : kinds)
        if (g.kind != Kind::None && (mask & kindBit(g.kind))) p += unit(g.probability);
    return p;
}

SoundTags describe(const float* fingerprint) { return describe(fingerprint, fingerprint + kDims + m::kEmbedding); }

SoundTags describe(const float* d, const float* outputs) {
    SoundTags t;
    // The three likeliest kinds.
    std::array<std::pair<float, int>, 3> best{};
    best.fill({-1.f, -1});
    for (int k = 0; k < kKinds; ++k) {
        const float p = outputs[output::Kinds + k];
        if (!(p > best[2].first)) continue;
        best[2] = {p, k};
        for (int j = 2; j > 0 && best[j].first > best[j - 1].first; --j) std::swap(best[j], best[j - 1]);
    }
    for (size_t j = 0; j < best.size(); ++j)
        if (best[j].second >= 0) t.kinds[j] = {static_cast<Kind>(best[j].second), byte(best[j].first)};
    const float topProbability = std::max(best[0].first, 0.f);
    t.sure = best[0].second >= 0 && topProbability >= m::kKindThreshold;
    t.loop = byte(outputs[output::Loop]);
    // Graded among the likeliest kind's sounds (any kind's when that is a guess).
    const Kind kind = t.kinds[0].kind;
    const int row = kind != Kind::None && topProbability >= kGradeRow ? static_cast<int>(kind) : kKinds;
    t.row = static_cast<int8_t>(row);
    auto set = [&t](Attribute a, float v) { t.attributes[static_cast<size_t>(a)] = byte(v); };
    set(Attribute::Bright, graded(d[feature::Centroid], row, Brightness));
    set(Attribute::Hard, 1.f - graded(d[feature::AttackTime], row, AttackTime));
    set(Attribute::Long, graded(d[feature::Duration], row, Duration));
    const float confidence = d[feature::PitchConfidence];
    const bool voiced = confidence > 0.5f;
    float pitch = std::numeric_limits<float>::quiet_NaN();
    if (voiced) pitch = 57.f + 12.f * d[feature::Pitch] / confidence;  // (220 Hz is A3, MIDI 57)
    const bool tonal = row < kKinds ? !isDrum(kind) : voiced;
    set(Attribute::Low, tonal && voiced ? 1.f - graded(pitch, row, PitchNote) : graded(d[feature::SubBass], row, SubBass));
    set(Attribute::Distorted, outputs[output::Qualities + kDistortion]);
    set(Attribute::Wet, outputs[output::Qualities + kReverb]);
    set(Attribute::Acoustic, outputs[output::Sources + 0]);
    set(Attribute::Electric, outputs[output::Sources + 1]);
    set(Attribute::Synthetic, outputs[output::Sources + 2]);
    set(Attribute::Pitched, (confidence - 0.3f) / 0.4f);
    // A pitch is told for what has one: tonal sounds, and drums tuned to a note (kicks, 808s, toms).
    if (voiced && (tonal || kind == Kind::Kick || kind == Kind::Tom || kind == Kind::None)) t.pitch = pitch;
    for (int w = 0; w < m::kWords; ++w) {
        const float score = outputs[output::Words + w];
        t.words[static_cast<size_t>(w)] = byte(score);
        if (score >= m::kWordCut[row * m::kWords + w]) t.heard |= uint64_t{1} << w;
    }
    return t;
}

float wordDegree(const SoundTags& tags, int word) {
    if (word < 0 || word >= m::kWords) return 0.f;
    const float cut = std::clamp(m::kWordCut[tags.row * m::kWords + word], 1e-3f, 0.999f);
    const float p = tags.word(word);
    return p >= cut ? 0.5f + 0.5f * (p - cut) / (1.f - cut) : 0.5f * p / cut;
}

std::string noteName(float midi) {
    if (!std::isfinite(midi)) return {};
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const long n = std::lround(midi);
    if (n < 0 || n > 127) return {};
    return std::string(names[n % 12]) + std::to_string(n / 12 - 1);
}

}  // namespace sub::intelligence::tagging
