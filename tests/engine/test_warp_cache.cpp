// The warp cache: stretched clips rendered whole in the background, and live
// playback reading the renders (WarpCache.h, Renderer::assignVoices()).

#include <cmath>
#include <memory>
#include <vector>

#include "Engine.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

constexpr int kBlock = 256;

std::shared_ptr<const sub::AudioSource> sineSource(double seconds = 2.0) {
    return sub::AudioSource::load(makeWav(sine(440.0, seconds, 0.5)), kSampleRate);
}

// A clip stretched to `rate` from the source's start, `length` timeline frames long.
sub::ClipRender stretched(std::shared_ptr<const sub::AudioSource> source, double rate, int64_t length) {
    sub::ClipRender clip;
    clip.source = std::move(source);
    clip.length = length;
    clip.rate = rate;
    clip.playback = sub::ClipRender::Playback::Stretch;
    clip.stretchConfig = sub::StretchConfig::Standard;
    clip.key = 7;
    return clip;
}

// The clip's render from a cache that renders at once.
std::shared_ptr<const sub::WarpRender> renderOf(sub::WarpCache& cache, const sub::ClipRender& clip) {
    cache.beginBuild();
    auto render = cache.request(clip, kSampleRate);
    cache.endBuild();
    cache.waitIdle();
    return render;
}

// A snapshot of one track playing `clip` (gain 1, centred) into the master, with one voice to stretch it.
std::shared_ptr<sub::RenderSnapshot> snapshotOf(const sub::ClipRender& clip,
                                                const std::shared_ptr<sub::WarpVoice>& voice) {
    auto snap = std::make_shared<sub::RenderSnapshot>();
    snap->sampleRate = kSampleRate;
    snap->tempo = 120.0;
    sub::TrackRender track;
    track.id = 1;
    track.params = std::make_shared<sub::TrackParams>();
    track.buffers = std::make_shared<sub::TrackBuffers>(sub::Renderer::kMaxBlock);
    track.clips = {clip};
    track.maxClipLength = clip.length;
    track.outgoing = {0};
    snap->tracks.push_back(std::move(track));
    sub::EdgeRender output;  // into the master
    output.state = std::make_shared<sub::EdgeState>(sub::Renderer::kMaxBlock);
    snap->edges.push_back(std::move(output));
    snap->masterInputs = {0};
    snap->warpVoices[static_cast<size_t>(clip.stretchConfig)] = {voice};
    return snap;
}

// Plays `blocks` blocks live; the left channel.
std::vector<float> playLive(sub::Renderer& renderer, sub::SharedState& shared, const sub::RenderSnapshot& snap,
                            int blocks) {
    std::vector<float> out;
    std::vector<float> left(kBlock), right(kBlock);
    float* outputs[2] = {left.data(), right.data()};
    sub::AudioIO io;
    io.outputs = outputs;
    io.numOutputs = 2;
    io.frames = kBlock;
    for (int b = 0; b < blocks; ++b) {
        renderer.processLive(snap, shared, io);
        out.insert(out.end(), left.begin(), left.end());
    }
    return out;
}

void playFrom(sub::Renderer& renderer, int64_t position) {
    renderer.prepare(kSampleRate);
    renderer.setPosition(position);
    renderer.setPlaying(true);
}

double maxDifference(const std::vector<float>& a, const float* b, size_t n) {
    double worst = 0.0;
    for (size_t i = 0; i < n; ++i) worst = std::max(worst, std::abs(static_cast<double>(a[i]) - b[i]));
    return worst;
}

}  // namespace

TEST_CASE("a warp render is what a voice plays from the clip's start") {
    sub::WarpCache cache(sub::WarpCache::kDefaultBudget, std::chrono::milliseconds(0));
    const sub::ClipRender clip = stretched(sineSource(), 0.8, kSampleRate);
    const auto render = renderOf(cache, clip);
    REQUIRE(render);
    REQUIRE(render->ready());

    // The same voice, fed in the cache's blocks: the same samples, every time.
    sub::WarpVoice voice(clip.stretchConfig, kSampleRate);
    std::vector<float> left(static_cast<size_t>(clip.length)), right(left.size());
    for (int64_t at = 0; at < clip.length; at += 1024) {
        const auto n = static_cast<int>(std::min<int64_t>(1024, clip.length - at));
        voice.render(clip, at, n, at > 0, left.data() + at, right.data() + at);
    }
    CHECK_EQ(maxDifference(left, render->left(), left.size()), 0.0);
    CHECK_EQ(maxDifference(right, render->right(), right.size()), 0.0);
    CHECK(rms(left) > 0.2);  // (not silence)
}

TEST_CASE("live playback plays a stretched clip's render once it is ready") {
    sub::WarpCache cache(sub::WarpCache::kDefaultBudget, std::chrono::milliseconds(0));
    sub::ClipRender clip = stretched(sineSource(), 0.8, kSampleRate);
    clip.rendered = renderOf(cache, clip);
    REQUIRE(clip.rendered->ready());
    auto voice = std::make_shared<sub::WarpVoice>(clip.stretchConfig, kSampleRate);
    const auto snap = snapshotOf(clip, voice);

    sub::Renderer renderer;
    sub::SharedState shared;
    playFrom(renderer, 0);
    const std::vector<float> out = playLive(renderer, shared, *snap, 40);
    CHECK(maxDifference(out, clip.rendered->left(), out.size()) < 1e-6);
    CHECK_EQ(voice->lastUsed, 0u);  // no stretcher ran

    // Offline renders stretch all the same (so that they don't depend on what is ready).
    sub::Renderer offline;
    playFrom(offline, 0);
    std::vector<float> stereo(kBlock * 2);
    offline.renderOffline(*snap, stereo.data(), kBlock);
    CHECK(voice->lastUsed != 0u);
}

TEST_CASE("a stretch under way goes on, and the render takes over where it would start again") {
    sub::WarpCache cache(sub::WarpCache::kDefaultBudget, std::chrono::milliseconds(0));
    sub::ClipRender clip = stretched(sineSource(), 1.25, kSampleRate);
    auto voice = std::make_shared<sub::WarpVoice>(clip.stretchConfig, kSampleRate);
    const auto unrendered = snapshotOf(clip, voice);  // its render not ready yet
    clip.rendered = renderOf(cache, clip);
    const auto rendered = snapshotOf(clip, voice);

    // A reference that stretches all along.
    auto otherVoice = std::make_shared<sub::WarpVoice>(clip.stretchConfig, kSampleRate);
    sub::ClipRender plain = clip;
    plain.rendered = nullptr;
    const auto reference = snapshotOf(plain, otherVoice);
    sub::Renderer referenceRenderer;
    sub::SharedState referenceShared;
    playFrom(referenceRenderer, 0);
    const std::vector<float> stretchedAllAlong = playLive(referenceRenderer, referenceShared, *reference, 40);

    sub::Renderer renderer;
    sub::SharedState shared;
    playFrom(renderer, 0);
    std::vector<float> out = playLive(renderer, shared, *unrendered, 20);
    const std::vector<float> after = playLive(renderer, shared, *rendered, 20);  // the render is ready now
    out.insert(out.end(), after.begin(), after.end());
    CHECK_EQ(maxDifference(out, stretchedAllAlong.data(), out.size()), 0.0);  // the voice went on

    // A locate: the voice would start again, so the render plays.
    renderer.setPosition(kSampleRate / 4);
    const std::vector<float> located = playLive(renderer, shared, *rendered, 20);
    CHECK(maxDifference(located, clip.rendered->left() + kSampleRate / 4, located.size()) < 1e-6);
}

TEST_CASE("the warp cache forgets what no snapshot asks for, and keeps to its budget") {
    const auto source = sineSource();
    const sub::ClipRender clip = stretched(source, 0.8, kSampleRate);
    {
        sub::WarpCache cache(sub::WarpCache::kDefaultBudget, std::chrono::milliseconds(0));
        const auto first = renderOf(cache, clip);
        CHECK(renderOf(cache, clip) == first);  // asked for again: the same render
        cache.beginBuild();
        cache.endBuild();  // nothing asked for: forgotten
        const auto again = renderOf(cache, clip);
        CHECK(again != first);
        CHECK(again->ready());
        // A different clip is a different render; one past the end of the build budget gets none.
        sub::ClipRender transposed = clip;
        transposed.transpose = 3.f;
        CHECK(renderOf(cache, transposed) != again);
    }
    {
        const size_t oneClip = static_cast<size_t>(kSampleRate) * 2 * sizeof(float);
        sub::WarpCache cache(oneClip + oneClip / 2, std::chrono::milliseconds(0));
        sub::ClipRender other = clip;
        other.rate = 0.9;
        cache.beginBuild();
        CHECK(cache.request(clip, kSampleRate) != nullptr);
        CHECK(cache.request(other, kSampleRate) == nullptr);  // over budget: stretched live
        cache.endBuild();
        cache.waitIdle();
    }
}

TEST_CASE("an engine renders its stretched clips in the background") {
    sub::Engine engine;
    const std::string path = makeWav(sine(440.0, 2.0, 0.5));
    engine.loadSource(path);
    const uint32_t track = engine.addTrack();
    sub::ClipDesc desc = clip(path, 0.0, 2.0);
    desc.warp = true;
    desc.segmentBpm = 100.0;
    engine.setTrackClips(track, {desc});
    const Samples before = engine.renderOffline(0.0, 4 * kBeat);
    engine.waitForWarpRenders();
    engine.setTempo(130.0);  // another render
    engine.waitForWarpRenders();
    engine.setTempo(120.0);
    engine.waitForWarpRenders();
    // Exports stretch as before: the renders don't change them.
    const Samples after = engine.renderOffline(0.0, 4 * kBeat);
    REQUIRE(before.size() == after.size());
    CHECK_EQ(maxDifference(before, after.data(), after.size()), 0.0);
}
