# substation-plugin-host: a plug-in sandbox, a process the engine runs plug-ins in
# (see main.cpp and docs/engine/sandboxes.md). No Qt. Its main loop is the
# system's: HostLoopWin32.cpp, HostLoopPosix.cpp.
add_executable(substation-plugin-host main.cpp HostServer.cpp)
if (WIN32)
    target_sources(substation-plugin-host PRIVATE HostLoopWin32.cpp)
    set_target_properties(substation-plugin-host PROPERTIES WIN32_EXECUTABLE OFF)  # (started without a window)
else()
    target_sources(substation-plugin-host PRIVATE HostLoopPosix.cpp)
endif()
target_link_libraries(substation-plugin-host PRIVATE sub_engine)
sub_project_warnings(substation-plugin-host)
