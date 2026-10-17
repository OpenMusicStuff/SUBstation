// What the processor can do, before anything built for more runs.

#pragma once

namespace sub::platform {

// Whether this processor runs the engine: on x86-64, AVX2 and FMA (Intel Haswell, AMD Zen and
// later; the engine is built for them), with the system saving their registers. Elsewhere
// (arm64) always. Checked first thing at start-up (ui/main.cpp), so a processor without them
// gets a message instead of an "illegal instruction" crash. This file is built without AVX.
bool cpuRunsEngine() noexcept;

// Says why SUBstation can't run here: a message box on Windows, standard error elsewhere.
void reportUnsupportedCpu() noexcept;

}  // namespace sub::platform
