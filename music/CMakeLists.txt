# Musical time every layer shares: the song's tempo and time signature along the
# timeline, and the conversions between its beats, seconds, samples and bars that
# follow from them (TempoMap.h); and groove, swing and feel placed on that time
# (Groove.h). Plain C++: no Qt, the operating system or anything
# of the other layers (cmake/CheckBoundaries.cmake), so the engine, the
# application layer, the UI and the intelligence module all count time the same
# way. Headers are included by their folder: "music/TempoMap.h".
add_library(sub_music STATIC src/music/TempoMap.cpp src/music/Groove.cpp)
add_library(sub::music ALIAS sub_music)
target_include_directories(sub_music PUBLIC src)
set_target_properties(sub_music PROPERTIES POSITION_INDEPENDENT_CODE ON)
sub_project_warnings(sub_music)
