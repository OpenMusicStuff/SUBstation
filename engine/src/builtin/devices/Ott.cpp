// Built-in "Over The Top" device: multiband upward + downward compression, the
// sound of every drop since 2014 (the squash itself, fitted to Live's OTT
// preset, is in OttDesign.h, which Snarl's Squash shares). One knob
// (Soundgoodize) scales every band's gain in dB, as Live's Amount scales its
// OTT preset's: 50 % is half the dB, not half the processed signal.

#include <algorithm>
#include <atomic>
#include <cmath>

#include "builtin/BuiltinProcessor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/OttDesign.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

class OttProcessor final : public BuiltinProcessor {
public:
    enum Param { Depth = 0, Output, NumParams };

    OttProcessor() : BuiltinProcessor(infos()) {}

    std::string typeId() const override { return "builtin:ott"; }
    std::string name() const override { return "Over The Top"; }

    void prepare(double sampleRate, int) override {
        squash_.prepare(sampleRate);
        depth_.reset(sampleRate, 0.03);
        output_.reset(sampleRate, 0.03);
        depth_.snapTo(param(Depth) / 100.f);
        output_.snapTo(dbToGain(param(Output)));
        reset();
    }

    void reset() override { squash_.reset(); }

protected:
    void render(const ProcessContext&, float* const* ch, int numChannels, int numFrames) override {
        depth_.setTarget(param(Depth) / 100.f);
        output_.setTarget(dbToGain(param(Output)));
        const int n = std::min(numChannels, 2);
        if (n <= 0) return;

        for (int i = 0; i < numFrames; ++i) {
            float x[2];
            for (int c = 0; c < n; ++c) x[c] = ch[c][i];
            const float depth = depth_.next();
            const float out = output_.next();
            squash_.tick(x, n, depth);
            for (int c = 0; c < n; ++c) ch[c][i] = x[c] * out;
        }
    }

private:
    static const std::vector<ParamInfo>& infos() {
        static const std::vector<ParamInfo> kInfos = {
            {"depth", "Soundgoodize", "%", 0.f, 100.f, 100.f},
            {"output", "Output", "dB", -24.f, 24.f, 0.f},
        };
        return kInfos;
    }

    ott::Squash squash_;
    SmoothedValue depth_, output_;
};

}  // namespace

SUB_REGISTER_BUILTIN(OttProcessor, AudioEffect);

}  // namespace sub
