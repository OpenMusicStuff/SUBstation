#pragma once
// Where plug-ins (VST3 and VST2) are looked for and the files found there, the
// scan cache's place, and how a file's errors read for the user.

#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <optional>

namespace sub::app {

inline constexpr int kPluginCacheVersion = 1;
inline constexpr double kPluginScanTimeout = 60.0;  // seconds one file may take (some copy-protected plug-ins are slow)

// The standard plug-in folders: VST3's, then VST2's, each once. VST3's are
// those in SUBSTATION_VST3_PATH when it is set (separated by the system's list
// separator; empty for none: the tests point it at the test plug-ins, so they
// never see the installed ones), else the system's, as the engine knows them
// (Vst3Format::defaultSearchPaths(): on Windows C:\Program Files\Common Files\VST3
// and %LOCALAPPDATA%\Programs\Common\VST3; ~/.vst3, /usr/lib/vst3 and
// /usr/local/lib/vst3 on Linux). VST2's are those in SUBSTATION_VST2_PATH when
// it is set; else none when SUBSTATION_VST3_PATH is (it stands for all of them);
// else the system's (Vst2Format::defaultSearchPaths(): the folder the registry
// names and C:\Program Files\VSTPlugins, ...\Steinberg\VSTPlugins, Common
// Files\VST2 and Common Files\Steinberg\VST2 on Windows; VST_PATH, else ~/.vst,
// /usr/lib/vst and /usr/local/lib/vst on Linux).
QStringList standardPluginFolders();

// The standard folders, then the user's own, each once (compared by pathKey()).
QStringList pluginSearchFolders(const QStringList& custom = {});

// Every .vst3 bundle (a folder) or file under the roots, and every library
// that may be a VST2 plug-in (a .dll on Windows, a .so elsewhere); nothing inside
// a bundle is listed. Linked folders (symbolic links, junctions) are followed,
// each real folder once, so a link back up can't loop. A root that is itself a
// .vst3 (or a VST2's library) is listed as it is. Unreadable folders are skipped. Sorted ignoring
// case (as Python's str.lower), one entry per path key.
QStringList findPluginFiles(const QStringList& roots);
QStringList findPluginFiles();  // under pluginSearchFolders()

// vst3-cache.json in localDataDir(), or SUBSTATION_PLUGIN_CACHE if set.
QString pluginCachePath();

// The file a VST3 bundle's code is in (the path itself for a single file, or a
// bundle without it), where the engine says it is on this system
// (Vst3Format::binaryInBundle(): Contents/x86_64-win/<name>.vst3 on Windows,
// Contents/<arch>-linux/<name>.so on Linux).
QString pluginBinary(const QString& path);

// When the file a plug-in's code is in changed, and its size: [modification
// time in ns since 1970, size], as Python's [st_mtime_ns, st_size]. A file is
// read again when it changes. Nothing if it can't be read.
std::optional<std::array<int64_t, 2>> pluginSignature(const QString& path);

// The loader's error as a line for the user: whitespace collapsed; Windows'
// LoadLibraryW error numbers named ("Windows could not load it: ..."), or only
// Windows' message kept. Other reasons pass as they are.
QString friendlyScanReason(const QString& reason);

}  // namespace sub::app
