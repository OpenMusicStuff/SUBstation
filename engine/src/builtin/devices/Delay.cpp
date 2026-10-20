// Built-in "Delay" device: the delay engine (DelayDevice.h) with the Digital
// character, Valhalla Delay's Digital mode at its default preset
// (DelayCharacter.h).

#include "builtin/BuiltinRegistry.h"
#include "builtin/DelayDevice.h"

namespace sub {
namespace {

class DelayProcessor final : public DelayDevice {
public:
    DelayProcessor() : DelayDevice(Character::Digital) {}

    std::string typeId() const override { return "builtin:delay"; }
    std::string name() const override { return "Delay"; }
};

}  // namespace

SUB_REGISTER_BUILTIN(DelayProcessor, AudioEffect);

}  // namespace sub
