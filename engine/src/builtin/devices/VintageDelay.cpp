// Built-in "Vintage Delay" device: the Delay's engine, parameters and editor
// (DelayDevice.h) with the Tape character, Valhalla Delay's Tape mode at its
// default preset (DelayCharacter.h): a tape echo whose speed follows the
// time, with wow, flutter, the loop's splice and tape saturation.

#include "builtin/BuiltinRegistry.h"
#include "builtin/DelayDevice.h"

namespace sub {
namespace {

class VintageDelayProcessor final : public DelayDevice {
public:
    VintageDelayProcessor() : DelayDevice(Character::Tape) {}

    std::string typeId() const override { return "builtin:vintage_delay"; }
    std::string name() const override { return "Vintage Delay"; }
};

}  // namespace

SUB_REGISTER_BUILTIN(VintageDelayProcessor, AudioEffect);

}  // namespace sub
