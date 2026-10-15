#pragma once
// What VST2 hosting does differently on each system, one file per system:
// Vst2PlatformWin32.cpp, Vst2PlatformPosix.cpp. A VST2 plug-in is a library
// (a .dll on Windows, a .so on Linux) with an entry point that makes an
// instance; the rest of the hosting is the same everywhere.

#include <string>
#include <vector>

namespace sub::vst2 {

// The system's VST2 folders. VST 2 has no standard place: these are where
// installers put them most (on Windows also the folder the registry names,
// HKLM\SOFTWARE\VST\VSTPluginsPath; on Linux VST_PATH, else ~/.vst,
// /usr/lib/vst, /usr/local/lib/vst).
std::vector<std::string> systemPluginFolders();

// Whether a file's name is one of a VST2 plug-in on this system (".dll", ".so").
bool isPluginFileName(const std::string& name);

// A library loaded: its handle, or null with `error` saying why (the system's
// words). unloadLibrary() takes the handle back.
void* loadLibrary(const std::string& path, std::string& error);
void* librarySymbol(void* library, const char* name);
void unloadLibrary(void* library);

}  // namespace sub::vst2
