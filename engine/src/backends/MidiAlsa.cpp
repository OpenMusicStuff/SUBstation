// MIDI input through ALSA's sequencer (Linux): the system's side of
// MidiInputDevices (see MidiDriver.h). PipeWire bridges ALSA's MIDI ports both
// ways, so the sequencer lists hardware and software inputs alike.
//
// One sequencer client ("SUBstation") for the process, made when an input is
// first listed or opened, with one port of its own that the inputs opened are
// connected to, and a thread of its own reading it. The inputs listed are the
// ports others can read from (not the system's, nor ours). Its port is stamped
// by a queue of its own: each event carries when the sequencer received it,
// which is turned into a host time (not when the thread got to it). It also
// hears the system's announcements: an input that goes away (unplugged) is
// connected again when a port of the same name comes back. Sequencer
// connections are never exclusive: another application reading an input
// doesn't stop this one.

#include "backends/MidiAlsa.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "backends/MidiDriver.h"
#include "rt/RtUtils.h"

namespace sub::midi {

namespace alsa {

std::string inputName(const std::string& client, const std::string& port) {
    if (port.rfind(client, 0) == 0 || client.empty()) return port;
    return client + ": " + port;
}

int64_t hostTime(int64_t eventNs, int64_t queueNs, int64_t hostNs) {
    return hostNs - std::max<int64_t>(0, queueNs - eventNs);
}

Decoder::Decoder() {
    if (snd_midi_event_new(32, &decoder_) < 0) {
        decoder_ = nullptr;
        return;
    }
    snd_midi_event_no_status(decoder_, 1);  // each message with its status byte
}

Decoder::~Decoder() {
    if (decoder_) snd_midi_event_free(decoder_);
}

}  // namespace alsa

namespace {

int64_t nanoseconds(const snd_seq_real_time_t& time) {
    return static_cast<int64_t>(time.tv_sec) * 1000000000 + static_cast<int64_t>(time.tv_nsec);
}

struct Address {
    int client = -1;
    int port = -1;
    bool operator==(const Address&) const = default;
};

struct Port {
    Address address;
    std::string name;  // inputName()
};

// The process's sequencer client: its port, its queue, its thread, and the inputs connected.
class Sequencer {
public:
    // The client, made the first time it is asked for (null where there is no sequencer: then
    // again the next time, in case it is there by then).
    static Sequencer* get() {
        static std::mutex mutex;
        static std::unique_ptr<Sequencer> instance;
        std::lock_guard lock(mutex);
        if (!instance) {
            auto made = std::make_unique<Sequencer>();
            if (made->seq_) instance = std::move(made);
        }
        return instance.get();
    }

    Sequencer() {
        // (Not tried where the kernel has no sequencer, which alsa-lib would complain about each time.)
        if (access("/dev/snd/seq", R_OK | W_OK) != 0) return;
        if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
            seq_ = nullptr;
            return;
        }
        snd_seq_set_client_name(seq_, "SUBstation");
        self_ = snd_seq_client_id(seq_);
        queue_ = snd_seq_alloc_named_queue(seq_, "SUBstation");
        snd_seq_port_info_t* info = nullptr;
        snd_seq_port_info_alloca(&info);
        snd_seq_port_info_set_name(info, "Input");
        // Written to by those it connects, and by no one else.
        snd_seq_port_info_set_capability(info, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_NO_EXPORT);
        snd_seq_port_info_set_type(info, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (queue_ >= 0) {
            snd_seq_port_info_set_timestamping(info, 1);
            snd_seq_port_info_set_timestamp_real(info, 1);
            snd_seq_port_info_set_timestamp_queue(info, queue_);
        }
        if (snd_seq_create_port(seq_, info) < 0) {
            snd_seq_close(seq_);
            seq_ = nullptr;
            return;
        }
        port_ = snd_seq_port_info_get_port(info);
        if (queue_ >= 0) {
            snd_seq_start_queue(seq_, queue_, nullptr);
            snd_seq_drain_output(seq_);
        }
        snd_seq_connect_from(seq_, port_, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
        wake_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        thread_ = std::thread([this] { run(); });
    }

    ~Sequencer() {
        if (!seq_) return;
        const uint64_t one = 1;
        [[maybe_unused]] const ssize_t written = write(wake_, &one, sizeof one);
        thread_.join();
        close(wake_);
        snd_seq_close(seq_);
    }

    Sequencer(const Sequencer&) = delete;
    Sequencer& operator=(const Sequencer&) = delete;

    // The ports others can read from, in the sequencer's order (by client, then port).
    std::vector<Port> inputs() {
        std::vector<Port> ports;
        snd_seq_client_info_t* client = nullptr;
        snd_seq_port_info_t* port = nullptr;
        snd_seq_client_info_alloca(&client);
        snd_seq_port_info_alloca(&port);
        snd_seq_client_info_set_client(client, -1);
        while (snd_seq_query_next_client(seq_, client) >= 0) {
            const int id = snd_seq_client_info_get_client(client);
            if (id == SND_SEQ_CLIENT_SYSTEM || id == self_) continue;
            snd_seq_port_info_set_client(port, id);
            snd_seq_port_info_set_port(port, -1);
            while (snd_seq_query_next_port(seq_, port) >= 0) {
                if (!readable(port)) continue;
                ports.push_back({{id, snd_seq_port_info_get_port(port)},
                                 alsa::inputName(snd_seq_client_info_get_name(client), snd_seq_port_info_get_name(port))});
            }
        }
        return ports;
    }

    // Connects `input` to this client's port: its messages go to `handler` with `port` until disconnect().
    void connect(const Port& input, const std::string& name, uint16_t port, const MidiInputDevices::Handler& handler,
                 const void* owner) {
        std::lock_guard lock(mutex_);
        const int result = snd_seq_connect_from(seq_, port_, input.address.client, input.address.port);
        if (result < 0) {
            throw std::runtime_error("The MIDI input \"" + name + "\" could not be opened: " + snd_strerror(result));
        }
        connections_.push_back({owner, input, port, &handler, false});
    }

    // Once this returns the handler isn't called again (for `owner`'s input).
    void disconnect(const void* owner) {
        std::lock_guard lock(mutex_);
        const auto it = std::find_if(connections_.begin(), connections_.end(),
                                     [&](const Connection& c) { return c.owner == owner; });
        if (it == connections_.end()) return;
        if (!it->lost) snd_seq_disconnect_from(seq_, port_, it->input.address.client, it->input.address.port);
        connections_.erase(it);
    }

private:
    struct Connection {
        const void* owner;
        Port input;
        uint16_t port;
        const MidiInputDevices::Handler* handler;
        bool lost;  // its port went away: connected again when one of its name comes back
    };

    static bool readable(const snd_seq_port_info_t* port) {
        const unsigned caps = snd_seq_port_info_get_capability(port);
        constexpr unsigned kRead = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
        return (caps & kRead) == kRead && !(caps & SND_SEQ_PORT_CAP_NO_EXPORT);
    }

    void run() {
        const int count = snd_seq_poll_descriptors_count(seq_, POLLIN);
        std::vector<pollfd> fds(static_cast<size_t>(count) + 1);
        snd_seq_poll_descriptors(seq_, fds.data(), static_cast<unsigned>(count), POLLIN);
        fds[static_cast<size_t>(count)] = {wake_, POLLIN, 0};
        for (;;) {
            if (poll(fds.data(), fds.size(), -1) < 0 && errno != EINTR) return;
            if (fds.back().revents & POLLIN) return;
            read();
        }
    }

    // What has come in, each event's age on the queue from a reading of it
    // (read again for an event that came after it).
    void read() {
        int64_t queueNs = 0;
        int64_t hostNs = queueTime(queueNs);
        std::lock_guard lock(mutex_);
        for (;;) {
            snd_seq_event_t* event = nullptr;
            const int result = snd_seq_event_input(seq_, &event);
            if (result == -ENOSPC) continue;  // (the input overran: what is left is read on)
            if (result < 0 || !event) return;
            if (event->source.client == SND_SEQ_CLIENT_SYSTEM) {
                announced(*event);
                continue;
            }
            const Address source{event->source.client, event->source.port};
            const auto it = std::find_if(connections_.begin(), connections_.end(),
                                         [&](const Connection& c) { return !c.lost && c.input.address == source; });
            if (it == connections_.end()) continue;
            int64_t when = hostTimeNs();
            if (queueNs != 0 && snd_seq_ev_is_real(event)) {
                const int64_t eventNs = nanoseconds(event->time.time);
                if (eventNs > queueNs) hostNs = queueTime(queueNs);
                if (queueNs != 0) when = alsa::hostTime(eventNs, queueNs, hostNs);
            }
            decoder_.each(*event, [&](const uint8_t* bytes, int size) { (*it->handler)(it->port, bytes, size, when); });
        }
    }

    // The queue's time now (0 if it can't be read), and the host time read with it.
    int64_t queueTime(int64_t& queueNs) {
        queueNs = 0;
        if (queue_ < 0) return hostTimeNs();
        snd_seq_queue_status_t* status = nullptr;
        snd_seq_queue_status_alloca(&status);
        if (snd_seq_get_queue_status(seq_, queue_, status) < 0) return hostTimeNs();
        const int64_t host = hostTimeNs();
        queueNs = nanoseconds(*snd_seq_queue_status_get_real_time(status));
        return host;
    }

    // The system's announcements: ports and clients gone, ports come.
    void announced(const snd_seq_event_t& event) {
        switch (event.type) {
            case SND_SEQ_EVENT_PORT_EXIT:
            case SND_SEQ_EVENT_CLIENT_EXIT:
                for (Connection& c : connections_) {
                    if (c.input.address.client == event.data.addr.client &&
                        (event.type == SND_SEQ_EVENT_CLIENT_EXIT || c.input.address.port == event.data.addr.port)) {
                        c.lost = true;
                    }
                }
                break;
            case SND_SEQ_EVENT_PORT_START: reconnect(); break;
            default: break;
        }
    }

    // Each lost input to a port of its name that none is connected to.
    void reconnect() {
        if (std::none_of(connections_.begin(), connections_.end(), [](const Connection& c) { return c.lost; })) return;
        for (const Port& input : inputs()) {
            const auto taken = [&](const Connection& c) { return !c.lost && c.input.address == input.address; };
            if (std::any_of(connections_.begin(), connections_.end(), taken)) continue;
            const auto it = std::find_if(connections_.begin(), connections_.end(),
                                         [&](const Connection& c) { return c.lost && c.input.name == input.name; });
            if (it == connections_.end()) continue;
            if (snd_seq_connect_from(seq_, port_, input.address.client, input.address.port) < 0) continue;
            it->input = input;
            it->lost = false;
        }
    }

    snd_seq_t* seq_ = nullptr;
    int self_ = -1;
    int port_ = -1;
    int queue_ = -1;
    int wake_ = -1;
    std::thread thread_;
    std::mutex mutex_;  // connections_, and the handlers' calls (disconnect() waits for one under way)
    std::vector<Connection> connections_;
    alsa::Decoder decoder_;  // (the thread's)
};

class AlsaInput final : public InputConnection {
public:
    AlsaInput(Sequencer& sequencer, const Port& input, const std::string& name, uint16_t port,
              const MidiInputDevices::Handler& handler)
        : sequencer_(sequencer) {
        sequencer_.connect(input, name, port, handler, this);
    }
    ~AlsaInput() override { sequencer_.disconnect(this); }

    AlsaInput(const AlsaInput&) = delete;
    AlsaInput& operator=(const AlsaInput&) = delete;

private:
    Sequencer& sequencer_;
};

}  // namespace

std::vector<std::string> systemInputNames() {
    std::vector<std::string> names;
    if (Sequencer* sequencer = Sequencer::get()) {
        for (Port& input : sequencer->inputs()) names.push_back(std::move(input.name));
    }
    return names;
}

std::unique_ptr<InputConnection> openSystemInput(size_t index, const std::string& name, uint16_t port,
                                                 const MidiInputDevices::Handler& handler) {
    Sequencer* sequencer = Sequencer::get();
    if (!sequencer) throw std::runtime_error("The MIDI input \"" + name + "\" is not connected");
    const std::vector<Port> inputs = sequencer->inputs();
    if (index >= inputs.size()) throw std::runtime_error("The MIDI input \"" + name + "\" is not connected");
    return std::make_unique<AlsaInput>(*sequencer, inputs[index], name, port, handler);
}

}  // namespace sub::midi
