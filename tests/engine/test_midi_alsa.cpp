// MIDI input through ALSA's sequencer (Linux, backends/MidiAlsa.cpp): the names
// inputs are listed by, events into messages and timestamps into host times
// (needing no sequencer); then, against the kernel's sequencer, a client of the
// test's own playing to MidiInputDevices: listed, its messages coming in with
// their times, unplugged and plugged in again, closed. Those skip where the
// kernel has no sequencer (/dev/snd/seq: the snd-seq module).

#if SUBSTATION_HAS_ALSA

#include <unistd.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

#include "MidiInput.h"
#include "backends/MidiAlsa.h"
#include "harness/Test.h"
#include "rt/RtUtils.h"

using namespace subtest;
using sub::midi::alsa::Decoder;

namespace {

using Bytes = std::vector<uint8_t>;

std::vector<Bytes> decoded(const snd_seq_event_t& event) {
    std::vector<Bytes> messages;
    Decoder decoder;
    decoder.each(event, [&](const uint8_t* bytes, int size) { messages.emplace_back(bytes, bytes + size); });
    return messages;
}

snd_seq_event_t blank() {
    snd_seq_event_t event;
    snd_seq_ev_clear(&event);
    return event;
}

bool waitUntil(const std::function<bool()>& predicate, double timeout = 5.0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// A sequencer client of the test's own with a port to play from, as a keyboard's would be.
class TestKeys {
public:
    TestKeys() {
        if (access("/dev/snd/seq", R_OK | W_OK) != 0 || snd_seq_open(&seq_, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
            seq_ = nullptr;
            SKIP("no ALSA sequencer (/dev/snd/seq: load the snd-seq module)");
        }
        client_ = "SUBstation Test " + std::to_string(getpid());
        snd_seq_set_client_name(seq_, client_.c_str());
        plugIn();
    }
    ~TestKeys() {
        if (seq_) snd_seq_close(seq_);
    }
    TestKeys(const TestKeys&) = delete;
    TestKeys& operator=(const TestKeys&) = delete;

    // How the engine lists its port.
    std::string name() const { return client_ + ": Keys"; }

    void plugIn() {
        port_ = snd_seq_create_simple_port(seq_, "Keys", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                           SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        REQUIRE(port_ >= 0);
    }
    void unplug() { REQUIRE(snd_seq_delete_simple_port(seq_, port_) >= 0); }

    // Sends to whoever is connected to the port.
    void send(snd_seq_event_t event) {
        snd_seq_ev_set_source(&event, port_);
        snd_seq_ev_set_subs(&event);
        snd_seq_ev_set_direct(&event);
        REQUIRE(snd_seq_event_output_direct(seq_, &event) >= 0);
    }
    void noteOn(int key, int velocity) {
        snd_seq_event_t event = blank();
        snd_seq_ev_set_noteon(&event, 0, key, velocity);
        send(event);
    }

private:
    snd_seq_t* seq_ = nullptr;
    std::string client_;
    int port_ = -1;
};

// What the handler got: each message with its port and host time.
struct Received {
    std::mutex mutex;
    std::vector<std::tuple<uint16_t, Bytes, int64_t>> messages;

    sub::MidiInputDevices::Handler handler() {
        return [this](uint16_t port, const uint8_t* bytes, int size, int64_t time) {
            std::lock_guard lock(mutex);
            messages.emplace_back(port, Bytes(bytes, bytes + size), time);
        };
    }
    size_t count() {
        std::lock_guard lock(mutex);
        return messages.size();
    }
};

}  // namespace

TEST_CASE("alsa: input names") {
    using sub::midi::alsa::inputName;
    CHECK_EQ(inputName("Launchkey MK3", "Launchkey MK3 MIDI 1"), std::string("Launchkey MK3 MIDI 1"));
    CHECK_EQ(inputName("Midi Through", "Midi Through Port-0"), std::string("Midi Through Port-0"));
    CHECK_EQ(inputName("VMPK Output", "out"), std::string("VMPK Output: out"));
}

TEST_CASE("alsa: the sequencer's times into host times") {
    using sub::midi::alsa::hostTime;
    CHECK_EQ(hostTime(9'000'000, 10'000'000, 500'000'000), int64_t{499'000'000});  // a millisecond before the reading
    CHECK_EQ(hostTime(10'000'000, 10'000'000, 500'000'000), int64_t{500'000'000});
    CHECK_EQ(hostTime(11'000'000, 10'000'000, 500'000'000), int64_t{500'000'000});  // never after it
}

TEST_CASE("alsa: events into messages") {
    snd_seq_event_t event = blank();
    snd_seq_ev_set_noteon(&event, 2, 60, 100);
    CHECK(decoded(event) == (std::vector<Bytes>{{0x92, 60, 100}}));
    snd_seq_ev_set_noteoff(&event, 2, 60, 0);
    CHECK(decoded(event) == (std::vector<Bytes>{{0x82, 60, 0}}));
    snd_seq_ev_set_controller(&event, 0, 7, 64);
    CHECK(decoded(event) == (std::vector<Bytes>{{0xB0, 7, 64}}));
    snd_seq_ev_set_pgmchange(&event, 15, 5);
    CHECK(decoded(event) == (std::vector<Bytes>{{0xCF, 5}}));
    snd_seq_ev_set_chanpress(&event, 1, 33);
    CHECK(decoded(event) == (std::vector<Bytes>{{0xD1, 33}}));
    snd_seq_ev_set_pitchbend(&event, 0, 0);  // the middle
    CHECK(decoded(event) == (std::vector<Bytes>{{0xE0, 0x00, 0x40}}));
    event = blank();
    event.type = SND_SEQ_EVENT_CONTROL14;  // a 14-bit controller: both of its controllers
    event.data.control.channel = 0;
    event.data.control.param = 7;
    event.data.control.value = (64 << 7) | 1;
    CHECK(decoded(event) == (std::vector<Bytes>{{0xB0, 7, 64}, {0xB0, 39, 1}}));
    event = blank();
    event.type = SND_SEQ_EVENT_CLOCK;
    CHECK(decoded(event) == (std::vector<Bytes>{{0xF8}}));
    // None: System Exclusive, and the sequencer's own.
    const uint8_t sysex[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    event = blank();
    snd_seq_ev_set_sysex(&event, sizeof sysex, const_cast<uint8_t*>(sysex));
    CHECK(decoded(event).empty());
    event = blank();
    event.type = SND_SEQ_EVENT_PORT_START;
    CHECK(decoded(event).empty());
}

TEST_CASE("alsa: an input listed, its messages in, with their times") {
    TestKeys keys;
    Received received;
    sub::MidiInputDevices devices(received.handler());
    const auto names = devices.available();
    REQUIRE(std::find(names.begin(), names.end(), keys.name()) != names.end());
    devices.open(keys.name(), 7);
    const int64_t before = sub::hostTimeNs();
    keys.noteOn(60, 100);
    snd_seq_event_t event = blank();
    snd_seq_ev_set_controller(&event, 3, 1, 90);
    keys.send(event);
    const uint8_t sysex[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};  // (left out)
    event = blank();
    snd_seq_ev_set_sysex(&event, sizeof sysex, const_cast<uint8_t*>(sysex));
    keys.send(event);
    event = blank();
    snd_seq_ev_set_noteoff(&event, 0, 60, 0);
    keys.send(event);
    REQUIRE(waitUntil([&] { return received.count() >= 3; }));
    const int64_t after = sub::hostTimeNs();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard lock(received.mutex);
    REQUIRE(received.messages.size() == 3);
    CHECK(std::get<1>(received.messages[0]) == (Bytes{0x90, 60, 100}));
    CHECK(std::get<1>(received.messages[1]) == (Bytes{0xB3, 1, 90}));
    CHECK(std::get<1>(received.messages[2]) == (Bytes{0x80, 60, 0}));
    int64_t last = before;
    for (const auto& [port, bytes, time] : received.messages) {
        CHECK_EQ(port, uint16_t{7});
        CHECK(time >= last - 1'000'000);  // in order (the readings' error is microseconds)
        CHECK(time <= after);
        last = time;
    }
}

TEST_CASE("alsa: an input unplugged and plugged in again") {
    TestKeys keys;
    Received received;
    sub::MidiInputDevices devices(received.handler());
    devices.open(keys.name(), 1);
    keys.noteOn(60, 100);
    REQUIRE(waitUntil([&] { return received.count() == 1; }));
    keys.unplug();
    REQUIRE(waitUntil([&] {
        const auto names = devices.available();
        return std::find(names.begin(), names.end(), keys.name()) == names.end();
    }));
    keys.plugIn();
    // Connected again when it is announced: played until it is heard.
    CHECK(waitUntil([&] {
        keys.noteOn(62, 100);
        return received.count() > 1;
    }));
    CHECK(devices.openNames() == std::vector<std::string>{keys.name()});
}

TEST_CASE("alsa: an input closed is heard no more") {
    TestKeys keys;
    Received received;
    sub::MidiInputDevices devices(received.handler());
    devices.open(keys.name(), 1);
    keys.noteOn(60, 100);
    REQUIRE(waitUntil([&] { return received.count() == 1; }));
    devices.close(keys.name());
    keys.noteOn(62, 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(received.count(), size_t{1});
    devices.open(keys.name(), 1);  // and again
    keys.noteOn(64, 100);
    CHECK(waitUntil([&] { return received.count() == 2; }));
}

#endif  // SUBSTATION_HAS_ALSA
