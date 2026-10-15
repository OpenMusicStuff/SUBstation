#include "browser/BrowserMime.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QUrl>

#include <algorithm>
#include <optional>

#include "audio/AudioFiles.h"
#include "io/Json.h"
#include "io/VitalPresets.h"

namespace sub::app {

namespace {

struct Payload {
    QList<QUrl> urls;
    QJsonArray plugins, devices, presets;
};

Payload payload(const std::vector<BrowserItem>& items) {
    Payload out;
    for (const BrowserItem& item : items) {
        switch (item.kind) {
            case ItemKind::Audio: out.urls << QUrl::fromLocalFile(item.path); break;
            case ItemKind::Plugin:
                if (item.plugin) out.plugins.append(QJsonObject::fromVariantMap(item.plugin->toRef()));
                break;
            case ItemKind::Device: out.devices.append(item.path); break;
            case ItemKind::Preset: out.presets.append(item.path); break;
        }
    }
    return out;
}

QByteArray compact(const QJsonArray& array) { return QJsonDocument(array).toJson(QJsonDocument::Compact); }

QStringList strings(const QByteArray& data) {
    QStringList out;
    if (const auto array = parseJsonArray(data))
        for (const QJsonValue& value : *array)
            if (value.isString()) out << value.toString();
    return out;
}

QByteArray dataOf(const QMimeData* mime, const char* type) {
    const QString format = QString::fromLatin1(type);
    return mime && mime->hasFormat(format) ? mime->data(format) : QByteArray();
}

}  // namespace

QStringList browserMimeTypes() {
    return {QStringLiteral("text/uri-list"), QString::fromLatin1(kPluginMime), QString::fromLatin1(kDeviceMime),
            QString::fromLatin1(kPresetMime)};
}

std::unique_ptr<QMimeData> browserMimeData(const std::vector<BrowserItem>& items) {
    auto mime = std::make_unique<QMimeData>();
    const Payload p = payload(items);
    if (!p.urls.isEmpty()) mime->setUrls(p.urls);
    if (!p.plugins.isEmpty()) mime->setData(QString::fromLatin1(kPluginMime), compact(p.plugins));
    if (!p.devices.isEmpty()) mime->setData(QString::fromLatin1(kDeviceMime), compact(p.devices));
    if (!p.presets.isEmpty()) mime->setData(QString::fromLatin1(kPresetMime), compact(p.presets));
    return mime;
}

QVariantMap browserDragData(const std::vector<BrowserItem>& items) {
    QVariantMap out;
    const Payload p = payload(items);
    if (!p.urls.isEmpty()) {
        QString list;
        for (const QUrl& url : p.urls) list += url.toString(QUrl::FullyEncoded) + QStringLiteral("\r\n");
        out.insert(QStringLiteral("text/uri-list"), list);
    }
    if (!p.plugins.isEmpty()) out.insert(QString::fromLatin1(kPluginMime), QString::fromUtf8(compact(p.plugins)));
    if (!p.devices.isEmpty()) out.insert(QString::fromLatin1(kDeviceMime), QString::fromUtf8(compact(p.devices)));
    if (!p.presets.isEmpty()) out.insert(QString::fromLatin1(kPresetMime), QString::fromUtf8(compact(p.presets)));
    return out;
}

std::vector<PluginInfo> pluginRefs(const QByteArray& data) {
    std::vector<PluginInfo> refs;
    const auto array = parseJsonArray(data);
    if (!array) return {};
    for (const QJsonValue& value : *array) {
        const QJsonObject item = value.toObject();
        // format, uid and name are needed; the rest has defaults.
        if (!value.isObject() || !item.contains(QStringLiteral("format")) || !item.contains(QStringLiteral("uid")) ||
            !item.contains(QStringLiteral("name")))
            return {};
        PluginInfo ref;
        ref.format = item.value(QStringLiteral("format")).toVariant().toString();
        ref.uid = item.value(QStringLiteral("uid")).toVariant().toString();
        ref.name = item.value(QStringLiteral("name")).toVariant().toString();
        ref.vendor = item.value(QStringLiteral("vendor")).toVariant().toString();
        ref.path = item.value(QStringLiteral("path")).toVariant().toString();
        ref.instrument = item.value(QStringLiteral("instrument")).toVariant().toBool();
        refs.push_back(std::move(ref));
    }
    return refs;
}

std::vector<PluginInfo> pluginRefs(const QMimeData* mime) { return pluginRefs(dataOf(mime, kPluginMime)); }

QStringList deviceKinds(const QByteArray& data) { return strings(data); }

QStringList deviceKinds(const QMimeData* mime) { return deviceKinds(dataOf(mime, kDeviceMime)); }

QStringList presetPaths(const QByteArray& data) { return strings(data); }

QStringList presetPaths(const QMimeData* mime) {
    QStringList paths = presetPaths(dataOf(mime, kPresetMime));
    // Vital's presets dragged in as files (from a file manager): presets too.
    if (mime && mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile() && isVitalPreset(url.toLocalFile()) && !paths.contains(url.toLocalFile())) {
                paths << url.toLocalFile();
            }
        }
    }
    return paths;
}

QByteArray movedDevicesData(const QString& trackId, const QStringList& deviceIds) {
    return (QStringList{trackId} + deviceIds).join(u'\n').toUtf8();
}

std::optional<MovedDevices> movedDevices(const QByteArray& data) {
    QStringList lines = QString::fromUtf8(data).split(u'\n');
    if (lines.isEmpty() || lines.front().isEmpty()) return std::nullopt;
    MovedDevices moved;
    moved.trackId = lines.takeFirst();
    for (const QString& line : std::as_const(lines)) {
        if (!line.isEmpty()) moved.deviceIds << line;
    }
    return moved;
}

std::optional<MovedDevices> movedDevices(const QMimeData* mime) {
    if (mime == nullptr || !mime->hasFormat(QString::fromLatin1(kDeviceMoveMime))) return std::nullopt;
    return movedDevices(mime->data(QString::fromLatin1(kDeviceMoveMime)));
}

std::optional<DroppedAudio> droppedAudio(const QMimeData* mime) {
    if (mime == nullptr) return std::nullopt;
    if (mime->hasFormat(QLatin1String(kClipMime))) {  // a clip's part of its file
        const QJsonObject clip = QJsonDocument::fromJson(mime->data(QLatin1String(kClipMime))).object();
        const QString path = clip.value(QStringLiteral("path")).toString();
        const double offset = clip.value(QStringLiteral("offset_sec")).toDouble();
        const double duration = clip.value(QStringLiteral("duration_sec")).toDouble();
        const double source = clip.value(QStringLiteral("source_sec")).toDouble();
        if (!path.isEmpty() && isAudioFile(path)) {
            DroppedAudio audio{path};
            if (source > 0.0 && duration > 0.0) {
                audio.start = std::clamp(offset / source, 0.0, 1.0);
                audio.end = std::clamp((offset + duration) / source, audio.start, 1.0);
            }
            return audio;
        }
    }
    for (const QUrl& url : mime->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && isAudioFile(path)) return DroppedAudio{path};
    }
    return std::nullopt;
}

std::unique_ptr<QMimeData> clipMimeData(const QString& path, double offsetSec, double durationSec, double sourceSec) {
    auto mime = std::make_unique<QMimeData>();
    mime->setUrls({QUrl::fromLocalFile(path)});
    const QJsonObject clip{{QStringLiteral("path"), path},
                           {QStringLiteral("offset_sec"), offsetSec},
                           {QStringLiteral("duration_sec"), durationSec},
                           {QStringLiteral("source_sec"), sourceSec}};
    mime->setData(QLatin1String(kClipMime), QJsonDocument(clip).toJson(QJsonDocument::Compact));
    return mime;
}

}  // namespace sub::app
