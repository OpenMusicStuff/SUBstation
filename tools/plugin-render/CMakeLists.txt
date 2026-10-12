# substation-plugin-render: runs audio through a VST3 effect offline, for
# studying how a plug-in sounds (see main.cpp). No Qt.
add_executable(substation-plugin-render main.cpp)
target_link_libraries(substation-plugin-render PRIVATE sub_engine)
sub_project_warnings(substation-plugin-render)
if (WIN32)
    set_target_properties(substation-plugin-render PROPERTIES WIN32_EXECUTABLE OFF)
endif()
