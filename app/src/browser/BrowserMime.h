#pragma once
// What a drag from the browser carries, and reading it where it is dropped.
//
// Audio files go as file URLs (text/uri-list); the other kinds as JSON under
// types of their own:
//   application/x-substation-plugin  a list of plug-ins (PluginRef fields:
//                                    format, uid, name, vendor, path, instrument)
//   application/x-substation-device  a list of built-in device kinds
//   application/x-substation-preset  a list of preset file paths
//
// Devices dragged from a track's chain in the device view, to move them (along
// its chain, into a rack's, or onto another track in the arrangement), go as
// plain text under a type of their own:
//   application/x-substation-device-move  the track's id, then the devices' ids, a line each
//
// An audio clip dragged out of the arrangement (onto a Sampler, a Drum Rack's
// pad, the desktop) goes as its file's URL, and its part of the file as JSON:
//   application/x-substation-clip  {"path", "offset_sec", "duration_sec", "source_sec"}

#include <QByteArray>
#include <QMimeData>
#include <QStringList>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <vector>

#include "browser/BrowserItem.h"
#include "plugins/PluginInfo.h"

namespace sub::app {

inline constexpr const char* kPluginMime = "application/x-substation-plugin";
inline constexpr const char* kDeviceMime = "application/x-substation-device";
inline constexpr const char* kPresetMime = "application/x-substation-preset";
inline constexpr const char* kDeviceMoveMime = "application/x-substation-device-move";
inline constexpr const char* kClipMime = "application/x-substation-clip";

// The types a drag from the browser may carry.
QStringList browserMimeTypes();

// The drag's data for these items.
std::unique_ptr<QMimeData> browserMimeData(const std::vector<BrowserItem>& items);
// The same as {mime type: text}, for QML (Drag.mimeData).
QVariantMap browserDragData(const std::vector<BrowserItem>& items);

// Plug-ins dragged from the browser, if any (format, uid, name, vendor, path,
// instrument filled in). Nothing if the data is not as written.
std::vector<PluginInfo> pluginRefs(const QMimeData* mime);
std::vector<PluginInfo> pluginRefs(const QByteArray& data);
// Built-in device kinds dragged from the browser, if any.
QStringList deviceKinds(const QMimeData* mime);
QStringList deviceKinds(const QByteArray& data);
// Presets (their files) dragged from the browser, if any.
QStringList presetPaths(const QMimeData* mime);
QStringList presetPaths(const QByteArray& data);

// Devices dragged from a track's chain: the track and the devices (in its order).
struct MovedDevices {
    QString trackId;
    QStringList deviceIds;

    friend bool operator==(const MovedDevices&, const MovedDevices&) = default;
};
// The drag's data for devices of a track moved.
QByteArray movedDevicesData(const QString& trackId, const QStringList& deviceIds);
// Devices dragged from a track's chain, if the drag is of those.
std::optional<MovedDevices> movedDevices(const QMimeData* mime);
std::optional<MovedDevices> movedDevices(const QByteArray& data);

// A sound dropped where samples go (a Sampler, a Drum Rack's pad): an audio
// file, the first dragged (from the browser, the desktop), or a clip's part of
// one: from `start` to `end`, fractions of the file (0 and 1: the whole of it).
struct DroppedAudio {
    QString path;
    double start = 0.0;
    double end = 1.0;

    friend bool operator==(const DroppedAudio&, const DroppedAudio&) = default;
};
std::optional<DroppedAudio> droppedAudio(const QMimeData* mime);
// The drag's data for an audio clip's part of its file (seconds; `sourceSec`: the file's length).
std::unique_ptr<QMimeData> clipMimeData(const QString& path, double offsetSec, double durationSec, double sourceSec);

}  // namespace sub::app
