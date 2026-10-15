#include "files/ProjectFiles.h"

#include <QHash>
#include <QMap>
#include <QStringList>

#include <algorithm>
#include <functional>

#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/Numbers.h"
#include "model/Paths.h"
#include "model/Project.h"

namespace sub::app {

namespace {

// One use of a file: a clip, a take lane's clip or a device.
struct Use {
    const ClipRef* clip = nullptr;
    const TakeClipRef* takeClip = nullptr;
    const DeviceRef* device = nullptr;
};

// Every use of a file in the project: (its path, the use), in the order met.
void eachUse(const Project& project, const std::function<void(const QString&, const Use&)>& use) {
    const auto plays = [](const Clip& clip) { return clip.isAudio() && !clip.path.isEmpty(); };
    for (const Track* track : project.allTracks()) {
        if (track->isAudio()) {
            for (const Clip& clip : track->clips) {
                if (!plays(clip)) continue;
                const ClipRef ref{track->id, clip.id};
                use(clip.path, {.clip = &ref});
            }
            for (const TakeLane& lane : track->takes) {
                for (const Clip& clip : lane.clips) {
                    if (!plays(clip)) continue;
                    const TakeClipRef ref{track->id, lane.id, clip.id};
                    use(clip.path, {.takeClip = &ref});
                }
            }
        }
        for (const Device* device : iterDevices(track->devices)) {  // (in racks too)
            const QString path = deviceFile(*device);
            if (path.isEmpty()) continue;
            const DeviceRef ref{track->id, device->id};
            use(path, {.device = &ref});
        }
    }
}

void add(FileUses& uses, const Use& use) {
    if (use.clip) uses.clips.append(*use.clip);
    if (use.takeClip) uses.takeClips.append(*use.takeClip);
    if (use.device) uses.devices.append(*use.device);
}

bool takeClipExists(const Project& project, const TakeClipRef& ref) {
    const Track* track = project.findTrack(ref.trackId);
    if (!track) return false;
    for (const TakeLane& lane : track->takes) {
        if (lane.id != ref.laneId) continue;
        return std::any_of(lane.clips.begin(), lane.clips.end(), [&](const Clip& c) { return c.id == ref.clipId; });
    }
    return false;
}

bool frozenTrack(const Project& project, const QString& trackId) {
    return project.hasOwner(trackId) && project.isFrozen(trackId);
}

}  // namespace

QString deviceFile(const Device& device) {
    if (device.isPlugin() || device.isRack() || !device.state) return {};
    return deviceState::fromModel(device.state).value(kDeviceFileKey);
}

std::optional<QString> withDeviceFile(const std::optional<QString>& state, const QString& path) {
    deviceState::Values values = deviceState::fromModel(state);
    if (path.isEmpty())
        values.remove(kDeviceFileKey);
    else
        values.insert(kDeviceFileKey, path);
    return deviceState::toModel(values);
}

std::vector<ProjectFile> projectFiles(const Project& project) {
    std::vector<ProjectFile> files;
    QHash<QString, size_t> byKey;
    eachUse(project, [&](const QString& path, const Use& use) {
        const QString key = pathIdentity(path);
        auto found = byKey.constFind(key);
        if (found == byKey.constEnd()) {
            found = byKey.insert(key, files.size());
            files.push_back({path, {}});
        }
        add(files[*found].uses, use);
    });
    return files;
}

FileUses fileUses(const Project& project, const QString& path) {
    FileUses uses;
    if (path.isEmpty()) return uses;
    const QString key = pathIdentity(path);
    eachUse(project, [&](const QString& used, const Use& use) {
        if (pathIdentity(used) == key) add(uses, use);
    });
    return uses;
}

FileUses existingUses(const Project& project, const FileUses& uses) {
    FileUses existing;
    for (const ClipRef& ref : uses.clips) {
        if (project.findClip(ref.trackId, ref.clipId)) existing.clips.append(ref);
    }
    for (const TakeClipRef& ref : uses.takeClips) {
        if (takeClipExists(project, ref)) existing.takeClips.append(ref);
    }
    for (const DeviceRef& ref : uses.devices) {
        const bool there = project.hasOwner(ref.trackId) && project.findDevice(ref.trackId, ref.deviceId);
        if (there) existing.devices.append(ref);
    }
    return existing;
}

FileUses changeableUses(const Project& project, const FileUses& uses, FileUses* frozen) {
    FileUses changeable;
    const FileUses existing = existingUses(project, uses);
    for (const ClipRef& ref : existing.clips) {
        if (!frozenTrack(project, ref.trackId))
            changeable.clips.append(ref);
        else if (frozen)
            frozen->clips.append(ref);
    }
    changeable.takeClips = existing.takeClips;  // (they don't play: frozen audio doesn't hold them)
    for (const DeviceRef& ref : existing.devices) {
        if (!frozenTrack(project, ref.trackId))
            changeable.devices.append(ref);
        else if (frozen)
            frozen->devices.append(ref);
    }
    return changeable;
}

QString usesText(const Project& project, const FileUses& uses) {
    QStringList parts;
    if (!uses.clips.isEmpty()) {
        const int clips = static_cast<int>(uses.clips.size());
        parts << countText(clips, QStringLiteral("clip"), QStringLiteral("clips"));
    }
    if (!uses.takeClips.isEmpty()) {
        const int clips = static_cast<int>(uses.takeClips.size());
        parts << countText(clips, QStringLiteral("take clip"), QStringLiteral("take clips"));
    }
    // Devices by what they are called ("Sampler", "2 × Sampler").
    QMap<QString, int> devices;
    QStringList order;
    for (const DeviceRef& ref : uses.devices) {
        const Device* device = project.hasOwner(ref.trackId) ? project.findDevice(ref.trackId, ref.deviceId) : nullptr;
        const QString name = device ? deviceName(*device) : QStringLiteral("Device");
        if (!devices.contains(name)) order << name;
        ++devices[name];
    }
    for (const QString& name : order) {
        const int count = devices.value(name);
        parts << (count == 1 ? name : QStringLiteral("%1 × %2").arg(count).arg(name));
    }
    return parts.join(QStringLiteral(", "));
}

}  // namespace sub::app
