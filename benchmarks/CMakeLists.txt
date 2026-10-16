# The benchmarks (README.md here): programs to run by hand, not tests (ctest
# doesn't run them). Built with -DSUBSTATION_BUILD_BENCHMARKS=ON.
#
#   parallel_render_bench   tracks rendered on one thread and on several: the
#                           engine alone, no Qt (with the engine tests' helpers)
#   freeze_bench            tracks rendered before their faders, as freezing
#                           them does: the engine alone, no Qt
#   browser_backend_bench   the browser's backend on a large synthetic library,
#                           checked against the reference the tests use (it
#                           needs the application layer: SUBSTATION_BUILD_APP)
#   builtin_devices_bench   each built-in audio effect alone: what it costs a
#                           core, at its defaults or at settings given
#   warp_bench              an audio clip's playback alone (direct, Re-Pitch,
#                           time stretched): nanoseconds a frame, load on a core
#   fft_bench               Signalsmith Linear's FFTs on KFR's against the
#                           library's own: the transforms alone
#   vital_bench             the built-in Vital against Vital's VST3 plug-in,
#                           the same notes held through each
#   sound_similarity_bench  the sound similarity's fingerprints on a real sample
#                           library: speed, and how often a one-shot's nearest
#                           sounds are of its kind (the intelligence module alone)
#   sound_features_dump     Essentia's descriptors of a list of files, to train
#                           the sound tagger on (tools/sound_tagger)

add_executable(parallel_render_bench parallel_render_bench.cpp Json.h)
target_include_directories(parallel_render_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                           ${PROJECT_SOURCE_DIR}/tests/engine)  # harness/...: never the harness folder, whose Signal.h is <signal.h> where names ignore case
target_link_libraries(parallel_render_bench PRIVATE sub_engine)
sub_project_warnings(parallel_render_bench)
# Live runs play through the fake ASIO driver the tests build (Windows, with the ASIO SDK).
if (TARGET sub_test_asio)
    add_dependencies(parallel_render_bench sub_test_asio)
    target_compile_definitions(parallel_render_bench PRIVATE SUBSTATION_TEST_ASIO_DRIVER="$<TARGET_FILE:sub_test_asio>")
endif()

add_executable(freeze_bench freeze_bench.cpp Json.h)
target_include_directories(freeze_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(freeze_bench PRIVATE sub_engine)
sub_project_warnings(freeze_bench)

add_executable(builtin_devices_bench builtin_devices_bench.cpp Json.h)
target_include_directories(builtin_devices_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(builtin_devices_bench PRIVATE sub_engine)
sub_project_warnings(builtin_devices_bench)

add_executable(warp_bench warp_bench.cpp Json.h)
target_include_directories(warp_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(warp_bench PRIVATE sub_engine)
sub_project_warnings(warp_bench)

add_executable(fft_bench fft_bench.cpp)
target_link_libraries(fft_bench PRIVATE signalsmith_linear)
# The engine's SIMD flags (engine/CMakeLists.txt), so the library's own FFT is
# compiled as the engine compiles it: a fair comparison.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    if (MSVC)
        target_compile_options(fft_bench PRIVATE /arch:AVX2)
    else()
        target_compile_options(fft_bench PRIVATE -mavx2 -mfma)
    endif()
endif()
sub_project_warnings(fft_bench)

# The built-in Vital against Vital's VST3 plug-in (the engine's VST3 host).
add_executable(vital_bench vital_bench.cpp Json.h)
target_include_directories(vital_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vital_bench PRIVATE sub_engine)
sub_project_warnings(vital_bench)

add_executable(sound_similarity_bench sound_similarity_bench.cpp)
target_link_libraries(sound_similarity_bench PRIVATE sub_intelligence)
sub_project_warnings(sound_similarity_bench)

add_executable(sound_features_dump sound_features_dump.cpp)
target_link_libraries(sound_features_dump PRIVATE sub_intelligence)
sub_project_warnings(sound_features_dump)

if (SUBSTATION_BUILD_APP)
    add_executable(browser_backend_bench
        browser_backend_bench.cpp
        Json.h
        LibraryGen.cpp
        LibraryGen.h
        PyRandom.h
        ${PROJECT_SOURCE_DIR}/tests/app/support/BrowserReference.cpp
        ${PROJECT_SOURCE_DIR}/tests/app/support/BrowserReference.h)
    target_include_directories(browser_backend_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                               ${PROJECT_SOURCE_DIR}/tests/app/support ${PROJECT_SOURCE_DIR}/tests/engine)
    target_link_libraries(browser_backend_bench PRIVATE sub_app)
    sub_project_warnings(browser_backend_bench)
    # A real project played live in each plug-in hosting mode (Windows: the fake ASIO driver).
    if (WIN32 AND TARGET sub_test_asio AND TARGET sub_app_test_support)
        add_executable(project_bench project_bench.cpp Json.h)
        target_include_directories(project_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests/engine)
        target_link_libraries(project_bench PRIVATE sub_app sub_app_test_support winmm)
        add_dependencies(project_bench sub_test_asio substation-plugin-host)
        target_compile_definitions(project_bench PRIVATE SUBSTATION_TEST_ASIO_DRIVER="$<TARGET_FILE:sub_test_asio>"
                                                         SUBSTATION_PLUGIN_HOST="$<TARGET_FILE:substation-plugin-host>")
    endif()
else()
    message(STATUS "browser_backend_bench needs the application layer (SUBSTATION_BUILD_APP): not built")
endif()
