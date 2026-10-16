# The layers' boundaries: the platform layer, the engine, the browser and the intelligence module include
# nothing of Qt; the application layer nothing of Qt Quick or QML (cmake/CheckBoundaries.cmake).
add_test(NAME boundaries
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/CheckBoundaries.cmake)

# --- Test plug-ins ------------------------------------------------------------------
# One VST3 bundle with a test instrument and effects (vst3_plugins/), next to the
# test executables, laid out as a VST3 bundle is on this platform.
if (SUBSTATION_TEST_PLUGINS)
    include(TestPlugins.cmake)
endif()

# --- Engine tests: Qt-free -------------------------------------------------------------
file(GLOB ENGINE_TEST_SOURCES CONFIGURE_DEPENDS engine/*.cpp)
add_executable(engine_tests engine/harness/Main.cpp ${ENGINE_TEST_SOURCES})
target_include_directories(engine_tests PRIVATE engine)
target_link_libraries(engine_tests PRIVATE sub_engine)
target_link_libraries(engine_tests PRIVATE vital)  # (test_vital_fft.cpp: Vital's FFT itself)
target_link_libraries(engine_tests PRIVATE signalsmith_linear)  # (test_signalsmith_kfr_fft.cpp: its FFTs on KFR's)
if (TARGET PkgConfig::XCB)  # (test_editors_x11.cpp's X client, harness/XDisplay.h)
    find_package(PkgConfig QUIET)
    pkg_check_modules(XCB_XTEST IMPORTED_TARGET GLOBAL xcb-xtest)
    if (XCB_XTEST_FOUND)
        target_link_libraries(engine_tests PRIVATE PkgConfig::XCB PkgConfig::XCB_XTEST)
    else()
        message(WARNING "xcb-xtest not found (libxcb-xtest0-dev): the plug-in editor tests aren't built.")
        target_compile_definitions(engine_tests PRIVATE SUBSTATION_NO_EDITOR_TESTS=1)
    endif()
endif()
if (TARGET PkgConfig::ALSA)
    target_link_libraries(engine_tests PRIVATE PkgConfig::ALSA)  # (test_midi_alsa.cpp's sequencer client)
endif()
sub_project_warnings(engine_tests)
# The plug-in sandboxes' process (test_plugin_sandboxes.cpp).
add_dependencies(engine_tests substation-plugin-host)
target_compile_definitions(engine_tests PRIVATE SUBSTATION_PLUGIN_HOST="$<TARGET_FILE:substation-plugin-host>")
if (TARGET sub_test_plugins)
    add_dependencies(engine_tests sub_test_plugins)
    target_compile_definitions(engine_tests PRIVATE SUBSTATION_TEST_PLUGINS_BUNDLE="${SUBSTATION_TEST_PLUGINS_BUNDLE}")
endif()
if (SUBSTATION_TEST_VST2_TARGETS)
    add_dependencies(engine_tests ${SUBSTATION_TEST_VST2_TARGETS})
    target_compile_definitions(engine_tests PRIVATE SUBSTATION_TEST_VST2_DIR="${SUBSTATION_TEST_VST2_DIR}")
endif()
if (TARGET sub_test_asio)
    add_dependencies(engine_tests sub_test_asio)
    target_compile_definitions(engine_tests PRIVATE SUBSTATION_TEST_ASIO_DRIVER="$<TARGET_FILE:sub_test_asio>")
endif()
add_test(NAME engine COMMAND engine_tests)
if (WIN32)
    set_tests_properties(engine PROPERTIES RESOURCE_LOCK desktop)  # (it opens plug-in editors: see app/)
endif()

# --- Platform tests: Qt-free, with the engine tests' harness ------------------------------
file(GLOB PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS platform/*.cpp)
add_executable(platform_tests engine/harness/Main.cpp ${PLATFORM_TEST_SOURCES})
target_include_directories(platform_tests PRIVATE engine)  # (harness/Test.h)
target_link_libraries(platform_tests PRIVATE sub_platform)
sub_project_warnings(platform_tests)
# The child process the IPC tests start (test_ipc.cpp).
add_executable(ipc_test_child platform/child/ipc_child.cpp)
target_link_libraries(ipc_test_child PRIVATE sub_platform)
sub_project_warnings(ipc_test_child)
add_dependencies(platform_tests ipc_test_child)
target_compile_definitions(platform_tests PRIVATE SUBSTATION_IPC_CHILD="$<TARGET_FILE:ipc_test_child>")
add_test(NAME platform COMMAND platform_tests)

# --- Intelligence tests: Qt-free, with the engine tests' harness (Test.h, Main.cpp) --------
file(GLOB INTELLIGENCE_TEST_SOURCES CONFIGURE_DEPENDS intelligence/*.cpp)
add_executable(intelligence_tests engine/harness/Main.cpp ${INTELLIGENCE_TEST_SOURCES} intelligence/Sounds.h)
target_include_directories(intelligence_tests PRIVATE engine intelligence)  # (harness/Test.h: never the harness folder itself, whose Signal.h is <signal.h> where names ignore case)
target_link_libraries(intelligence_tests PRIVATE sub_intelligence)
target_compile_definitions(intelligence_tests PRIVATE SUBSTATION_VELOCITY_MODEL="${PROJECT_SOURCE_DIR}/intelligence/models/velocity.hbm")
sub_project_warnings(intelligence_tests)
add_test(NAME intelligence COMMAND intelligence_tests)

# --- Application-layer and UI tests: Qt Test, one executable per file ---------------------
if (SUBSTATION_BUILD_APP)
    add_subdirectory(app)
endif()
