#include "plugins/sandbox/SandboxProtocol.h"

#include <bit>
#include <cstring>

#include "platform/Ipc.h"

namespace sub::sandbox {

namespace {

constexpr uint32_t kHeaderSize = 12;              // kind, instance, request
constexpr uint32_t kMaxMessage = 256u << 20;      // (a plug-in's state may be large, but not this)

bool readExactly(platform::Channel& channel, char* data, size_t size) {
    while (size > 0) {
        const size_t n = channel.read(data, size);
        if (n == 0) return false;
        data += n;
        size -= n;
    }
    return true;
}

}  // namespace

std::string encode(const Message& message) {
    platform::ByteWriter out;
    out.u32(kHeaderSize + static_cast<uint32_t>(message.body.size()));
    out.u32(static_cast<uint32_t>(message.kind));
    out.u32(message.instance);
    out.u32(message.request);
    out.bytes += message.body;
    return std::move(out.bytes);
}

bool writeMessage(platform::Channel& channel, const Message& message) {
    const std::string frame = encode(message);
    return channel.write(frame.data(), frame.size());
}

bool readMessage(platform::Channel& channel, Message& out) {
    char sizeBytes[4];
    if (!readExactly(channel, sizeBytes, sizeof sizeBytes)) return false;
    const uint32_t size = platform::ByteReader(std::string_view(sizeBytes, 4)).u32();
    if (size < kHeaderSize || size > kMaxMessage) return false;
    std::string frame(size, '\0');
    if (!readExactly(channel, frame.data(), size)) return false;
    platform::ByteReader in(frame);
    out.kind = static_cast<Kind>(in.u32());
    out.instance = in.u32();
    out.request = in.u32();
    out.body.assign(frame, kHeaderSize, std::string::npos);
    return in.ok();
}

void writeF64(platform::ByteWriter& out, double value) { out.u64(std::bit_cast<uint64_t>(value)); }

double readF64(platform::ByteReader& in) { return std::bit_cast<double>(in.u64()); }

void writeBytes(platform::ByteWriter& out, const std::vector<uint8_t>& bytes) {
    out.str(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::vector<uint8_t> readBytes(platform::ByteReader& in) {
    const std::string bytes = in.str();
    return {bytes.begin(), bytes.end()};
}

void writeParams(platform::ByteWriter& out, const std::vector<ParamInfo>& params) {
    out.u32(static_cast<uint32_t>(params.size()));
    for (const ParamInfo& p : params) {
        out.str(p.id);
        out.str(p.name);
        out.str(p.unit);
        out.f32(p.minValue);
        out.f32(p.maxValue);
        out.f32(p.defaultValue);
        out.u32(static_cast<uint32_t>(p.steps));
        out.u8(static_cast<uint8_t>((p.logScale ? 1 : 0) | (p.automatable ? 2 : 0) | (p.readOnly ? 4 : 0) |
                                    (p.hidden ? 8 : 0) | (p.ownText ? 16 : 0)));
        out.u32(static_cast<uint32_t>(p.valueLabels.size()));
        for (const std::string& label : p.valueLabels) out.str(label);
    }
}

// (Counts are read, not trusted: the vectors grow only while there is something to read.)
std::vector<ParamInfo> readParams(platform::ByteReader& in) {
    std::vector<ParamInfo> params;
    const uint32_t count = in.u32();
    for (uint32_t i = 0; i < count && in.ok(); ++i) {
        ParamInfo& p = params.emplace_back();
        p.id = in.str();
        p.name = in.str();
        p.unit = in.str();
        p.minValue = in.f32();
        p.maxValue = in.f32();
        p.defaultValue = in.f32();
        p.steps = static_cast<int>(in.u32());
        const uint8_t flags = in.u8();
        p.logScale = (flags & 1) != 0;
        p.automatable = (flags & 2) != 0;
        p.readOnly = (flags & 4) != 0;
        p.hidden = (flags & 8) != 0;
        p.ownText = (flags & 16) != 0;
        const uint32_t labels = in.u32();
        for (uint32_t i = 0; i < labels && in.ok(); ++i) p.valueLabels.push_back(in.str());
    }
    if (!in.ok()) params.clear();
    return params;
}

void writeValues(platform::ByteWriter& out, const std::vector<float>& values) {
    out.u32(static_cast<uint32_t>(values.size()));
    for (const float v : values) out.f32(v);
}

std::vector<float> readValues(platform::ByteReader& in) {
    std::vector<float> values;
    const uint32_t count = in.u32();
    for (uint32_t i = 0; i < count && in.ok(); ++i) values.push_back(in.f32());
    if (!in.ok()) values.clear();
    return values;
}

void writeLoaded(platform::ByteWriter& out, const LoadedInfo& info) {
    out.str(info.name);
    out.u8(static_cast<uint8_t>((info.hasEditor ? 1 : 0) | (info.hasSidechain ? 2 : 0) | (info.acceptsMidi ? 4 : 0)));
    out.u32(static_cast<uint32_t>(info.latency));
    out.u32(static_cast<uint32_t>(info.tail));
    out.str(info.presetName);
    writeParams(out, info.params);
    writeValues(out, info.values);
    out.str(info.problem);
}

LoadedInfo readLoaded(platform::ByteReader& in) {
    LoadedInfo info;
    info.name = in.str();
    const uint8_t flags = in.u8();
    info.hasEditor = (flags & 1) != 0;
    info.hasSidechain = (flags & 2) != 0;
    info.acceptsMidi = (flags & 4) != 0;
    info.latency = static_cast<int>(in.u32());
    info.tail = static_cast<int>(in.u32());
    info.presetName = in.str();
    info.params = readParams(in);
    info.values = readValues(in);
    info.problem = in.str();
    return info;
}

void writeEvent(platform::ByteWriter& out, const ProcessorEvent& event) {
    out.u8(static_cast<uint8_t>(event.type));
    out.u32(static_cast<uint32_t>(event.paramIndex));
    out.f32(event.value);
    out.f32(event.oldValue);
    out.u32(event.gesture);
}

ProcessorEvent readEvent(platform::ByteReader& in) {
    ProcessorEvent event;
    event.type = static_cast<ProcessorEvent::Type>(in.u8());
    event.paramIndex = static_cast<int>(in.u32());
    event.value = in.f32();
    event.oldValue = in.f32();
    event.gesture = in.u32();
    return event;
}

BlockContext BlockContext::from(const ProcessContext& ctx) noexcept {
    BlockContext out;
    out.sampleRate = ctx.sampleRate;
    out.samplePos = ctx.samplePos;
    out.beatPos = ctx.beatPos;
    out.tempo = ctx.tempo;
    out.loopStartBeat = ctx.loopStartBeat;
    out.loopEndBeat = ctx.loopEndBeat;
    out.timeSigNum = ctx.timeSigNum;
    out.timeSigDen = ctx.timeSigDen;
    out.keyTonic = ctx.keyTonic;
    out.playing = ctx.playing ? 1 : 0;
    out.looping = ctx.looping ? 1 : 0;
    out.keyMinor = ctx.keyMinor ? 1 : 0;
    out.offline = ctx.offline ? 1 : 0;
    return out;
}

ProcessContext BlockContext::context() const noexcept {
    ProcessContext ctx;
    ctx.sampleRate = sampleRate;
    ctx.samplePos = samplePos;
    ctx.beatPos = beatPos;
    ctx.tempo = tempo;
    ctx.loopStartBeat = loopStartBeat;
    ctx.loopEndBeat = loopEndBeat;
    ctx.timeSigNum = timeSigNum;
    ctx.timeSigDen = timeSigDen;
    ctx.keyTonic = keyTonic;
    ctx.playing = playing != 0;
    ctx.looping = looping != 0;
    ctx.keyMinor = keyMinor != 0;
    ctx.offline = offline != 0;
    return ctx;
}

}  // namespace sub::sandbox
