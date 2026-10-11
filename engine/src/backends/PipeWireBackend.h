#pragma once
// Audio through PipeWire (Linux), as the "PipeWire" driver: a pw_filter of
// planar float ports, one per open channel, which the backend links to the
// device's own ports itself (as JACK clients do), so the callback gets the
// graph's buffers as they are: no interleaving, no resampling, no buffering.
//
// A device is an output node (an Audio/Sink or Audio/Duplex): its inputs are
// its own capture ports (Duplex), else those of the source on the same card,
// else the default source's. No name follows the system's default output. The
// connection to the daemon is made when a device is first listed or opened;
// with no daemon nothing opens, and AudioBackendsPosix.cpp's "System" driver
// is what the application falls back to.

#include <memory>
#include <string>
#include <vector>

#include "../AudioDevice.h"

namespace sub {

class PipeWireBackend final : public AudioBackend {
public:
    PipeWireBackend();
    ~PipeWireBackend() override;

    static constexpr const char* kName = "PipeWire";
    std::string name() const override { return kName; }
    std::vector<AudioDeviceInfo> devices() override;
    void open(const DeviceConfig& config, AudioCallback* callback) override;
    void start() override;
    void close() override;
    bool isOpen() const override;
    DeviceState state() const override;
    void adjustForReset(DeviceConfig& config) override;

    struct Impl;

private:
    std::unique_ptr<Impl> d_;
};

}  // namespace sub
