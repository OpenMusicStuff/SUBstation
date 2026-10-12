#pragma once
// The parts of MidiAlsa.cpp (MIDI input through ALSA's sequencer, Linux) that
// need no sequencer, for its tests: the names its inputs are listed by, the
// events turned into short messages, and the sequencer's timestamps turned into
// host times.

#include <alsa/asoundlib.h>

#include <cstdint>
#include <string>

#include "MidiDriver.h"

namespace sub::midi::alsa {

// How an input is listed: its port's name where that names its client too (a
// USB device's "Launchkey MK3 MIDI 1"), else "client: port" ("VMPK Output: out").
std::string inputName(const std::string& client, const std::string& port);

// The host time (hostTimeNs()) of an event the sequencer stamped `eventNs`
// on its queue, from a reading of the queue (`queueNs`) taken at `hostNs`:
// as long before that reading as the event was before the queue's. Never
// later than the reading.
int64_t hostTime(int64_t eventNs, int64_t queueNs, int64_t hostNs);

// Sequencer events into short MIDI messages (channel and system messages; not
// System Exclusive, nor the sequencer's own events, which carry none).
class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    // Calls `message(bytes, size)` for each message `event` carries (a 14-bit
    // controller carries two), each with its status byte.
    template <typename F>
    void each(const snd_seq_event_t& event, F&& message) {
        if (event.type == SND_SEQ_EVENT_SYSEX || !decoder_) return;
        unsigned char bytes[12];
        snd_midi_event_reset_decode(decoder_);
        const long size = snd_midi_event_decode(decoder_, bytes, sizeof bytes, &event);
        for (long i = 0; i < size;) {
            if (!(bytes[i] & 0x80)) return;  // (not a message's start)
            const int length = shortMessageSize(bytes[i]);
            if (i + length > size) return;
            message(static_cast<const uint8_t*>(bytes + i), length);
            i += length;
        }
    }

private:
    snd_midi_event_t* decoder_ = nullptr;
};

}  // namespace sub::midi::alsa
