#pragma once
// The user's own plug-in folders (VST3's and VST2's alike), kept in QSettings
// (plugins/vst3_folders, named so from before VST2).

#include <QString>
#include <QStringList>

namespace sub::app {

inline constexpr const char* kPluginFoldersKey = "plugins/vst3_folders";

// The folders the user added, searched after the standard ones.
QStringList customPluginFolders();
void setCustomPluginFolders(const QStringList& folders);

// Every folder a scan looks in: pluginSearchFolders(customPluginFolders()).
QStringList pluginFolders();

}  // namespace sub::app
