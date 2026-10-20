# The real-time audio engine: DSP, the routing graph, VST3 hosting, MIDI input,
# recording, rendering and real-time scheduling. A static library with no Qt
# (and no other UI) dependency: the application layer (app/) drives it through
# Engine.h. cmake/CheckBoundaries.cmake checks that nothing here includes Qt.

# miniaudio is compiled once, as C, in its own translation unit.
add_library(miniaudio STATIC src/miniaudio_impl.c)
target_include_directories(miniaudio SYSTEM PUBLIC third_party/miniaudio)
target_compile_definitions(miniaudio PUBLIC
    MA_NO_ENGINE MA_NO_NODE_GRAPH MA_NO_RESOURCE_MANAGER MA_NO_GENERATION)
set_target_properties(miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Signalsmith Linear's FFTs (header-only, MIT): Signalsmith Stretch's and
# Spectral's. Their power-of-two transforms run on KFR's DFT (kfr_fft, below:
# third_party/signalsmith-linear/signalsmith-linear/platform/fft-kfr.h), several
# times faster than the library's own.
add_library(signalsmith_linear INTERFACE)
target_include_directories(signalsmith_linear SYSTEM INTERFACE third_party/signalsmith-linear)
target_compile_definitions(signalsmith_linear INTERFACE SIGNALSMITH_USE_KFR)
target_link_libraries(signalsmith_linear INTERFACE kfr_fft)
if (MSVC)
    target_compile_options(miniaudio PRIVATE /W0)
else()
    target_compile_options(miniaudio PRIVATE -w)
    target_link_libraries(miniaudio PUBLIC ${CMAKE_DL_LIBS} Threads::Threads m)
endif()

# fastpitch (MIT, header-only): pYIN as librosa has it, for GigaTune. Its SIMD follows the
# compiler's flags, so only the engine includes it (builtin/GigaTuneDesign.cpp, built with AVX2).
add_library(fastpitch INTERFACE)
target_include_directories(fastpitch SYSTEM INTERFACE third_party/fastpitch/include)

# --- LAME (LGPL v2): its MP3 encoding library, for exporting MP3 (Mp3Writer.cpp) ---
# Vendored unmodified in third_party/lame (see its VERSION.txt), compiled as C
# with a config.h of ours (what LAME's configure would find).
set(_lame ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lame)
add_library(mp3lame STATIC
    ${_lame}/libmp3lame/bitstream.c
    ${_lame}/libmp3lame/encoder.c
    ${_lame}/libmp3lame/fft.c
    ${_lame}/libmp3lame/gain_analysis.c
    ${_lame}/libmp3lame/id3tag.c
    ${_lame}/libmp3lame/lame.c
    ${_lame}/libmp3lame/newmdct.c
    ${_lame}/libmp3lame/presets.c
    ${_lame}/libmp3lame/psymodel.c
    ${_lame}/libmp3lame/quantize.c
    ${_lame}/libmp3lame/quantize_pvt.c
    ${_lame}/libmp3lame/reservoir.c
    ${_lame}/libmp3lame/set_get.c
    ${_lame}/libmp3lame/tables.c
    ${_lame}/libmp3lame/takehiro.c
    ${_lame}/libmp3lame/util.c
    ${_lame}/libmp3lame/vbrquantize.c
    ${_lame}/libmp3lame/VbrTag.c
    ${_lame}/libmp3lame/version.c)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    target_sources(mp3lame PRIVATE ${_lame}/libmp3lame/vector/xmm_quantize_sub.c)  # (config.h: HAVE_XMMINTRIN_H)
endif()
target_include_directories(mp3lame SYSTEM PUBLIC ${_lame}/include)
target_include_directories(mp3lame PRIVATE ${_lame} ${_lame}/libmp3lame)
target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H)
set_target_properties(mp3lame PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    target_compile_options(mp3lame PRIVATE /W0)
    target_compile_definitions(mp3lame PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(mp3lame PRIVATE -w)
    target_link_libraries(mp3lame PRIVATE m)
endif()
# (Its bit writing shifts negative values left: defined by every compiler we build with, but undefined in
# C. Not checked by UBSan under SUBSTATION_SANITIZE; ASan still checks it.)
if (SUBSTATION_SANITIZE MATCHES "undefined")
    target_compile_options(mp3lame PRIVATE -fno-sanitize=shift)
endif()

# --- KFR (GPLv2+): the real FFT the built-in Vital's oscillators run -------------
# Vendored in third_party/kfr (see its VERSION.txt): KFR's DFT alone, built as its
# own CMake builds it (one architecture: the engine's), without KFR's CMake.
set(_kfr ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kfr)
add_library(kfr_fft STATIC
    ${_kfr}/src/dft/dft.cpp
    ${_kfr}/src/dft/bitrev-impl-f32.cpp ${_kfr}/src/dft/dft-impl-f32.cpp
    ${_kfr}/src/dft/fft-impl-f32.cpp ${_kfr}/src/dft/ngfft-impl-f32.cpp
    # (dft.cpp's dispatch instantiates double precision too)
    ${_kfr}/src/dft/bitrev-impl-f64.cpp ${_kfr}/src/dft/dft-impl-f64.cpp
    ${_kfr}/src/dft/fft-impl-f64.cpp ${_kfr}/src/dft/ngfft-impl-f64.cpp)
target_include_directories(kfr_fft SYSTEM PUBLIC ${_kfr}/include)
target_compile_definitions(kfr_fft PUBLIC KFR_HAVE_DFT)
set_target_properties(kfr_fft PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    target_compile_options(kfr_fft PRIVATE /W0 /bigobj /fp:contract)
    target_compile_definitions(kfr_fft PUBLIC _ENABLE_EXTENDED_ALIGNED_STORAGE)
else()
    target_compile_options(kfr_fft PRIVATE -w -ffp-contract=fast)
    target_compile_options(kfr_fft PUBLIC -flax-vector-conversions)  # (KFR's headers need it)
endif()
# Not instrumented under SUBSTATION_SANITIZE: KFR's SIMD templates take gigabytes a file to compile with
# ASan, and it is a library others test (our calls into it still are checked).
if (SUBSTATION_SANITIZE)
    target_compile_options(kfr_fft PRIVATE -fno-sanitize=all)
endif()
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    if (MSVC)
        target_compile_options(kfr_fft PRIVATE /arch:AVX2)
    else()
        target_compile_options(kfr_fft PRIVATE -mavx2 -mfma)
    endif()
endif()

# --- Vital (GPLv3): the built-in Vital instrument's synthesis engine -------------
# Vendored in third_party/vital (see its README.md): Vital's synthesis code and
# the parts of its src/common the engine and its presets need, without JUCE (a
# stand-in for JuceHeader.h in compat/). Built as Vital builds it, as two unity
# files, with the engine's SIMD flags and its warnings not shown.
set(_vital ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vital)
add_library(vital STATIC ${_vital}/src/unity_build/synthesis.cpp ${_vital}/src/unity_build/common.cpp
    ${_vital}/compat/kfr_fourier_transform.cpp)
file(GLOB _vital_dirs LIST_DIRECTORIES true ${_vital}/src/synthesis/* ${_vital}/src/common/wavetable)
list(FILTER _vital_dirs EXCLUDE REGEX "\\.(cpp|h)$")
target_include_directories(vital SYSTEM PUBLIC
    ${_vital}/compat ${_vital}/third_party ${_vital}/src/common ${_vital}/src/interface/look_and_feel ${_vital_dirs})
set_target_properties(vital PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Its FFT on KFR's (compat/kfr_fourier_transform.cpp), not the KISS FFT it falls back on.
target_compile_definitions(vital PUBLIC SUBSTATION_KFR_FFT=1)  # (everything including its headers: one class)
target_link_libraries(vital PRIVATE kfr_fft)
if (MSVC)
    target_compile_options(vital PRIVATE /W0 /bigobj)
    target_compile_definitions(vital PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    # Vital picks its SIMD by __SSE2__, which GCC and Clang define on x86-64 and
    # MSVC doesn't (every x86-64 CPU has SSE2). Everything including its headers needs it.
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_compile_definitions(vital PUBLIC __SSE2__=1)
    endif()
else()
    target_compile_options(vital PRIVATE -w -fno-math-errno)
endif()
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    if (MSVC)
        target_compile_options(vital PRIVATE /arch:AVX2)
    else()
        target_compile_options(vital PRIVATE -mavx2 -mfma)
    endif()
endif()
# Under SUBSTATION_SANITIZE, ASan checks Vital but UBSan doesn't: it does what compilers define and C++
# doesn't (offsets to a null buffer it doesn't read, float conversions it relies on wrapping), and it is
# vendored nearly as it is upstream.
if (SUBSTATION_SANITIZE MATCHES "undefined")
    target_compile_options(vital PRIVATE -fno-sanitize=undefined)
endif()

# --- VST3 SDK (MIT; the parts we use are vendored in third_party/vst3sdk) --------
set(VST3_SDK ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst3sdk CACHE INTERNAL "The vendored VST 3 SDK")

function(sub_vst3_sdk_library name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} SYSTEM PUBLIC ${VST3_SDK})
    target_compile_definitions(${name} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    set_target_properties(${name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if (MSVC)
        target_compile_options(${name} PRIVATE /W0 /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${name} PRIVATE -w)
    endif()
endfunction()

if (WIN32)
    set(_vst3_threadchecker ${VST3_SDK}/public.sdk/source/common/threadchecker_win32.cpp)
    set(_vst3_module_loader ${VST3_SDK}/public.sdk/source/vst/hosting/module_win32.cpp)
else()
    set(_vst3_threadchecker ${VST3_SDK}/public.sdk/source/common/threadchecker_linux.cpp)
    set(_vst3_module_loader ${VST3_SDK}/public.sdk/source/vst/hosting/module_linux.cpp)
endif()

# Interfaces, the SDK's base library and helpers both sides use.
sub_vst3_sdk_library(vst3_base
    ${VST3_SDK}/base/source/baseiids.cpp
    ${VST3_SDK}/base/source/fbuffer.cpp
    ${VST3_SDK}/base/source/fdebug.cpp
    ${VST3_SDK}/base/source/fdynlib.cpp
    ${VST3_SDK}/base/source/fobject.cpp
    ${VST3_SDK}/base/source/fstreamer.cpp
    ${VST3_SDK}/base/source/fstring.cpp
    ${VST3_SDK}/base/source/timer.cpp
    ${VST3_SDK}/base/source/updatehandler.cpp
    ${VST3_SDK}/base/thread/source/fcondition.cpp
    ${VST3_SDK}/base/thread/source/flock.cpp
    ${VST3_SDK}/pluginterfaces/base/conststringtable.cpp
    ${VST3_SDK}/pluginterfaces/base/coreiids.cpp
    ${VST3_SDK}/pluginterfaces/base/funknown.cpp
    ${VST3_SDK}/pluginterfaces/base/ustring.cpp
    ${VST3_SDK}/public.sdk/source/common/commoniids.cpp
    ${VST3_SDK}/public.sdk/source/common/commonstringconvert.cpp
    ${VST3_SDK}/public.sdk/source/common/memorystream.cpp
    ${_vst3_threadchecker}
    ${VST3_SDK}/public.sdk/source/vst/utility/stringconvert.cpp
    ${VST3_SDK}/public.sdk/source/vst/vstinitiids.cpp
    ${VST3_SDK}/public.sdk/source/vst/vstpresetfile.cpp)
if (NOT WIN32)
    target_link_libraries(vst3_base PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

# The host side: loading modules, host context, component/controller connection.
sub_vst3_sdk_library(vst3_hosting
    ${VST3_SDK}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${VST3_SDK}/public.sdk/source/vst/hosting/module.cpp
    ${_vst3_module_loader}
    ${VST3_SDK}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp)
target_link_libraries(vst3_hosting PUBLIC vst3_base)
if (WIN32)
    target_link_libraries(vst3_hosting PUBLIC ole32 shell32)
endif()
# As C++17 the module loader reads paths as UTF-8 (u8path); as C++20 it would
# use the ANSI code page, and plug-ins with non-ASCII paths would not load.
set_target_properties(vst3_hosting PROPERTIES CXX_STANDARD 17)

# --- VST 2 SDK (BSD-3-Clause; Xaymar's clean-room, header-only vst2sdk in third_party/vst2sdk) ---
# Included only through src/plugins/vst2/Vst2Sdk.h.
add_library(vst2sdk INTERFACE)
target_include_directories(vst2sdk SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2sdk/include)

# --- ASIO SDK (optional, Windows) ------------------------------------------------
# Steinberg's ASIO SDK is not part of the repository: its licence doesn't allow
# passing it on. Unzip it (https://www.steinberg.net/asiosdk) into the project
# folder as it comes (.gitignore keeps asiosdk* folders out of git), or point
# SUBSTATION_ASIO_SDK (a CMake or environment variable) at it. Only its headers
# are used. Without it the engine is built with WASAPI only.
set(SUBSTATION_ASIO_SDK "$ENV{SUBSTATION_ASIO_SDK}" CACHE PATH "Steinberg's ASIO SDK: the folder containing common/iasiodrv.h")
set(ASIO_SDK "")
if (SUBSTATION_ASIO AND WIN32)
    if (SUBSTATION_ASIO_SDK)
        if (NOT EXISTS "${SUBSTATION_ASIO_SDK}/common/iasiodrv.h")
            message(FATAL_ERROR "SUBSTATION_ASIO_SDK (${SUBSTATION_ASIO_SDK}) has no common/iasiodrv.h")
        endif()
        set(ASIO_SDK "${SUBSTATION_ASIO_SDK}")
    else()
        # Any folder in the project folder, or one inside it (some unzip tools add
        # a level); else an asio* folder beside the project or in the drive's root.
        set(_asio_patterns
            "${PROJECT_SOURCE_DIR}/*/common/iasiodrv.h"
            "${PROJECT_SOURCE_DIR}/*/*/common/iasiodrv.h"
            "${PROJECT_SOURCE_DIR}/../asio*/common/iasiodrv.h"
            "${PROJECT_SOURCE_DIR}/../asio*/*/common/iasiodrv.h")
        if (DEFINED ENV{SystemDrive})
            list(APPEND _asio_patterns "$ENV{SystemDrive}/asio*/common/iasiodrv.h" "$ENV{SystemDrive}/asio*/*/common/iasiodrv.h")
        endif()
        file(GLOB _asio_headers ${_asio_patterns})
        if (_asio_headers)
            list(SORT _asio_headers ORDER DESCENDING)  # the newest of several (asiosdk_2.3.3_... after asiosdk2.3)
            list(GET _asio_headers 0 _asio_header)
            cmake_path(GET _asio_header PARENT_PATH _asio_common)
            cmake_path(GET _asio_common PARENT_PATH ASIO_SDK)
            cmake_path(NORMAL_PATH ASIO_SDK)
        endif()
    endif()
    if (ASIO_SDK)
        message(STATUS "ASIO SDK: ${ASIO_SDK}")
    else()
        message(WARNING "ASIO SDK not found, so the engine is built without ASIO. Unzip the SDK from "
                        "https://www.steinberg.net/asiosdk into ${PROJECT_SOURCE_DIR} or set SUBSTATION_ASIO_SDK.")
    endif()
endif()
set(SUBSTATION_ASIO_SDK_DIR "${ASIO_SDK}" CACHE INTERNAL "The ASIO SDK found (empty: none)")

# --- The engine ---------------------------------------------------------------------
# Every built-in device is one .cpp in builtin/devices/; a new file is picked up on the next configure.
file(GLOB BUILTIN_DEVICE_SOURCES CONFIGURE_DEPENDS src/builtin/devices/*.cpp)
# Each defines an empty function named after its file, which BuiltinDevices.cpp
# calls: or a program linking the (static) engine would leave the devices'
# files out, nothing else referring to them (see builtin/BuiltinRegistry.h).
set(_anchor_declarations "")
set(_anchor_calls "")
foreach (_source IN LISTS BUILTIN_DEVICE_SOURCES)
    get_filename_component(_stem "${_source}" NAME_WE)
    string(MAKE_C_IDENTIFIER "subBuiltinAnchor_${_stem}" _anchor)
    set_property(SOURCE "${_source}" APPEND PROPERTY COMPILE_DEFINITIONS "SUB_BUILTIN_ANCHOR=${_anchor}")
    string(APPEND _anchor_declarations "void ${_anchor}();\n")
    string(APPEND _anchor_calls "    ${_anchor}();\n")
endforeach()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/BuiltinDevices.cpp CONTENT
"// Written by engine/CMakeLists.txt from builtin/devices/*.cpp (see builtin/BuiltinRegistry.h).

namespace sub {

@_anchor_declarations@
void linkBuiltinDevices() {
@_anchor_calls@}

}  // namespace sub
" @ONLY)

add_library(sub_engine STATIC
    src/Engine.cpp
    src/EngineChains.cpp
    src/EngineDevice.cpp
    src/EngineInput.cpp
    src/EngineOffline.cpp
    src/EngineSandboxes.cpp
    src/Mp3Writer.cpp
    src/EngineSnapshot.cpp
    src/EngineTracks.cpp
    src/AudioDevice.cpp
    src/backends/MiniaudioBackend.cpp
    src/AudioSource.cpp
    src/CacheFile.cpp
    src/Renderer.cpp
    src/Scheduler.cpp
    src/Recorder.cpp
    src/Metronome.cpp
    src/MidiInput.cpp
    src/EditorHost.cpp
    src/StreamCache.cpp
    src/StreamScheduler.cpp
    src/Warp.cpp
    src/WarpCache.cpp
    src/builtin/BuiltinProcessor.cpp
    src/builtin/BuiltinRegistry.cpp
    src/builtin/ChangelingDesign.cpp
    src/builtin/GigaTuneDesign.cpp
    src/builtin/SwiftF0.cpp
    src/builtin/SampleSlicing.cpp
    src/builtin/VitalPatch.cpp
    ${BUILTIN_DEVICE_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/generated/BuiltinDevices.cpp
    src/plugins/editor/EditorWindow.cpp
    src/plugins/PluginFormats.cpp
    src/plugins/PresetName.cpp
    src/plugins/vst2/Vst2Format.cpp
    src/plugins/vst2/Vst2Preset.cpp
    src/plugins/vst2/Vst2Processor.cpp
    src/plugins/vst3/Vst3Format.cpp
    src/plugins/vst3/Vst3Ids.cpp
    src/plugins/vst3/Vst3Processor.cpp
    src/plugins/sandbox/PluginSandboxes.cpp
    src/plugins/sandbox/Sandbox.cpp
    src/plugins/sandbox/SandboxedProcessor.cpp
    src/plugins/sandbox/SandboxProtocol.cpp
)
# Each system's audio driver types, MIDI inputs and plug-in editor windows are files of their own.
if (WIN32)
    target_sources(sub_engine PRIVATE src/backends/AudioBackendsWin32.cpp src/backends/MidiWinMM.cpp
                                      src/plugins/editor/EditorWindowWin32.cpp src/plugins/vst3/Vst3PlatformWin32.cpp
                                      src/plugins/vst2/Vst2PlatformWin32.cpp)
else()
    target_sources(sub_engine PRIVATE src/backends/AudioBackendsPosix.cpp src/plugins/vst3/Vst3PlatformPosix.cpp
                                      src/plugins/vst2/Vst2PlatformPosix.cpp)
endif()
# Plug-in editors on Linux: X11 windows through xcb (libxcb1-dev). Without it,
# or on other systems but Windows, plug-ins show no editor (EditorWindowNone.cpp).
if (SUBSTATION_X11_EDITORS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(XCB IMPORTED_TARGET GLOBAL xcb)
    endif()
    if (XCB_FOUND)
        target_sources(sub_engine PRIVATE src/plugins/editor/EditorWindowX11.cpp src/plugins/editor/Vst3RunLoop.cpp)
        target_link_libraries(sub_engine PRIVATE PkgConfig::XCB)
        target_compile_definitions(sub_engine PUBLIC SUBSTATION_HAS_X11_EDITORS=1)
    else()
        message(WARNING "xcb not found, so plug-ins show no editor. Install libxcb1-dev, or set "
                        "SUBSTATION_X11_EDITORS=OFF.")
    endif()
endif()
if (NOT WIN32 AND NOT (SUBSTATION_X11_EDITORS AND XCB_FOUND))
    target_sources(sub_engine PRIVATE src/plugins/editor/EditorWindowNone.cpp)
endif()
# MIDI input on Linux: ALSA's sequencer (libasound2-dev). Without it, or on
# other systems but Windows, the engine lists no MIDI inputs (MidiNone.cpp).
if (SUBSTATION_ALSA AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(ALSA IMPORTED_TARGET GLOBAL alsa)
    endif()
    if (ALSA_FOUND)
        message(STATUS "ALSA: ${ALSA_VERSION}")
        target_sources(sub_engine PRIVATE src/backends/MidiAlsa.cpp)
        target_link_libraries(sub_engine PRIVATE PkgConfig::ALSA)
        target_compile_definitions(sub_engine PUBLIC SUBSTATION_HAS_ALSA=1)
    else()
        message(WARNING "ALSA (alsa.pc) not found, so the engine is built without MIDI inputs. Install "
                        "libasound2-dev, or set SUBSTATION_ALSA=OFF.")
    endif()
endif()
if (NOT WIN32 AND NOT (SUBSTATION_ALSA AND ALSA_FOUND))  # (ALSA_FOUND stays cached when the option is turned off)
    target_sources(sub_engine PRIVATE src/backends/MidiNone.cpp)
endif()
# PipeWire (Linux): the driver Linux builds play through, before miniaudio's
# "System". Needs libpipewire's headers (libpipewire-0.3-dev); without them the
# engine is built with "System" alone.
if (SUBSTATION_PIPEWIRE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
    endif()
    if (PIPEWIRE_FOUND)
        message(STATUS "PipeWire: ${PIPEWIRE_VERSION}")
        target_sources(sub_engine PRIVATE src/backends/PipeWireBackend.cpp)
        target_link_libraries(sub_engine PRIVATE PkgConfig::PIPEWIRE)
        target_compile_definitions(sub_engine PUBLIC SUBSTATION_HAS_PIPEWIRE=1)
    else()
        message(WARNING "libpipewire-0.3 not found, so the engine is built without PipeWire (miniaudio's \"System\" "
                        "driver only). Install libpipewire-0.3-dev, or set SUBSTATION_PIPEWIRE=OFF.")
    endif()
endif()
add_library(sub::engine ALIAS sub_engine)
target_include_directories(sub_engine PUBLIC src)
# Signalsmith Stretch (time stretching / pitch shifting) and its FFT library:
# MIT-licensed, header-only, no further dependencies.
target_include_directories(sub_engine SYSTEM PRIVATE third_party/signalsmith-stretch)
target_link_libraries(sub_engine PRIVATE signalsmith_linear kfr_fft mp3lame vital fastpitch)  # (kfr_fft: Hue's FFTs)
target_link_libraries(sub_engine PUBLIC sub_platform sub_music miniaudio vst3_hosting vst2sdk)
if (WIN32)
    target_link_libraries(sub_engine PUBLIC user32 ole32 uuid advapi32 winmm)
    target_compile_definitions(sub_engine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_link_libraries(sub_engine PUBLIC Threads::Threads)
endif()
target_compile_definitions(sub_engine PUBLIC _USE_MATH_DEFINES)
set_target_properties(sub_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (ASIO_SDK)
    target_sources(sub_engine PRIVATE src/backends/AsioBackend.cpp)
    target_include_directories(sub_engine SYSTEM PRIVATE "${ASIO_SDK}/common")
    target_compile_definitions(sub_engine PUBLIC SUBSTATION_HAS_ASIO=1)
endif()
sub_project_warnings(sub_engine)
# The audio path is float loops: let the compiler vectorize them with AVX2 on
# x86-64 (a CPU with AVX2 is needed: Intel Haswell / AMD Zen and later). On
# arm64 (Apple Silicon, Linux on ARM) the compiler's baseline, NEON, is what it
# vectorizes with: no flags. No fast math (/fp:fast, -ffast-math): a DAW's
# results must not depend on the compiler reordering arithmetic or assuming
# there's no NaN or infinity (a broken plug-in's output, a division by a
# parameter at 0). Denormals are flushed where audio is rendered instead
# (ScopedNoDenormals, rt/RtUtils.h). -fno-math-errno only lets GCC and Clang
# inline sqrt and friends: nothing reads errno.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    if (MSVC)
        target_compile_options(sub_engine PRIVATE /arch:AVX2)
    else()
        target_compile_options(sub_engine PRIVATE -mavx2 -mfma)
    endif()
    if (MINGW)
        # GCC on 64-bit Windows can't align the stack to 32 bytes (GCC bug 54412),
        # so an aligned AVX move to a stack slot may crash: the assembler makes
        # them all unaligned ones (as fast on any AVX2 CPU). Needs binutils 2.38.
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-Wa,-muse-unaligned-vector-move" SUB_HAVE_UNALIGNED_VECTOR_MOVE)
        if (SUB_HAVE_UNALIGNED_VECTOR_MOVE)
            target_compile_options(sub_engine PRIVATE -Wa,-muse-unaligned-vector-move)
            target_compile_options(vital PRIVATE -Wa,-muse-unaligned-vector-move)
            target_compile_options(kfr_fft PRIVATE -Wa,-muse-unaligned-vector-move)
        endif()
    endif()
endif()
if (NOT MSVC)
    target_compile_options(sub_engine PRIVATE -fno-math-errno)
endif()
