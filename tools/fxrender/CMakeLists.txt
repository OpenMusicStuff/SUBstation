# substation-fxrender: runs audio files through a VST3 effect offline (see main.cpp). No Qt.
add_executable(substation-fxrender main.cpp)
target_link_libraries(substation-fxrender PRIVATE sub_engine)
sub_project_warnings(substation-fxrender)
if (WIN32)
    set_target_properties(substation-fxrender PROPERTIES WIN32_EXECUTABLE OFF)
endif()
