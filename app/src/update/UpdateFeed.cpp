#include "update/UpdateFeed.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace sub::app {

namespace {

struct ChannelName {
    UpdateChannel channel;
    const char* name;
};

constexpr ChannelName kChannels[] = {
    {UpdateChannel::Stable, "stable"},
    {UpdateChannel::Nightly, "nightly"},
    {UpdateChannel::Canary, "canary"},
};

bool isHex(const QByteArray& text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

}  // namespace

std::optional<UpdateChannel> updateChannelNamed(const QString& name) {
    for (const ChannelName& channel : kChannels) {
        if (name == QLatin1String(channel.name)) return channel.channel;
    }
    return std::nullopt;
}

QString nameOf(UpdateChannel channel) {
    for (const ChannelName& c : kChannels) {
        if (c.channel == channel) return QString::fromLatin1(c.name);
    }
    return QStringLiteral("stable");
}

bool channelOffers(UpdateChannel subscribed, UpdateChannel release) { return int(release) <= int(subscribed); }

std::optional<UpdateVersion> UpdateVersion::parse(const QString& text) {
    static const QRegularExpression pattern(
        QStringLiteral("^v?(\\d{1,6})\\.(\\d{1,6})\\.(\\d{1,6})(?:-(nightly|canary)\\.(\\d{1,14}))?$"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch()) return std::nullopt;
    UpdateVersion version;
    version.major = match.captured(1).toInt();
    version.minor = match.captured(2).toInt();
    version.patch = match.captured(3).toInt();
    if (match.hasCaptured(4) && !match.captured(4).isEmpty()) {
        version.channel = *updateChannelNamed(match.captured(4));
        version.build = match.captured(5).toLongLong();
    }
    return version;
}

QString UpdateVersion::toString() const {
    QString text = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (channel != UpdateChannel::Stable) text += QStringLiteral("-%1.%2").arg(nameOf(channel)).arg(build);
    return text;
}

std::strong_ordering UpdateVersion::operator<=>(const UpdateVersion& other) const {
    if (auto order = major <=> other.major; order != 0) return order;
    if (auto order = minor <=> other.minor; order != 0) return order;
    if (auto order = patch <=> other.patch; order != 0) return order;
    // (The release a build led up to is newer than the build.)
    const bool stable = channel == UpdateChannel::Stable;
    const bool otherStable = other.channel == UpdateChannel::Stable;
    if (stable != otherStable) return stable ? std::strong_ordering::greater : std::strong_ordering::less;
    return build <=> other.build;
}

QString updateAssetName(const UpdateVersion& version, const QString& platformSuffix) {
    return QStringLiteral("SUBstation-") + version.toString() + platformSuffix;
}

QString updatePlatformSuffix(bool installer) {
#ifdef _WIN32
    return installer ? QStringLiteral("-windows-x64-setup.exe") : QStringLiteral("-windows-x64.zip");
#else
    (void)installer;
    return QStringLiteral("-linux-x64.zip");
#endif
}

std::vector<UpdateRelease> parseUpdateReleases(const QByteArray& json, const QString& platformSuffix,
                                               QString* error) {
    std::vector<UpdateRelease> releases;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isArray()) {
        if (error) {
            *error = parseError.error != QJsonParseError::NoError
                         ? QStringLiteral("The list of releases isn't JSON: %1.").arg(parseError.errorString())
                         : QStringLiteral("The list of releases isn't a list.");
        }
        return releases;
    }
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("draft")).toBool()) continue;
        const QString tag = object.value(QStringLiteral("tag_name")).toString();
        const std::optional<UpdateVersion> version = UpdateVersion::parse(tag);
        if (!version) continue;
        UpdateRelease release;
        release.version = *version;
        release.tag = tag;
        release.title = object.value(QStringLiteral("name")).toString();
        if (release.title.isEmpty()) release.title = tag;
        release.notes = object.value(QStringLiteral("body")).toString();
        release.pageUrl = QUrl(object.value(QStringLiteral("html_url")).toString());
        release.published =
            QDateTime::fromString(object.value(QStringLiteral("published_at")).toString(), Qt::ISODate);
        const QString assetName = updateAssetName(*version, platformSuffix);
        const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue& assetValue : assets) {
            const QJsonObject asset = assetValue.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
            if (name == assetName) {
                release.assetName = name;
                release.assetUrl = url;
                release.assetSize = asset.value(QStringLiteral("size")).toInteger();
                const QString digest = asset.value(QStringLiteral("digest")).toString();
                if (digest.startsWith(QLatin1String("sha256:"))) {
                    const QByteArray hex = digest.mid(7).toLower().toLatin1();
                    if (hex.size() == 64 && isHex(hex)) release.sha256 = hex;
                }
            } else if (name == assetName + QStringLiteral(".sha256")) {
                release.sha256Url = url;
            }
        }
        releases.push_back(std::move(release));
    }
    return releases;
}

std::optional<UpdateRelease> pickUpdate(const std::vector<UpdateRelease>& releases, UpdateChannel channel,
                                        const UpdateVersion& current) {
    const UpdateRelease* best = nullptr;
    for (const UpdateRelease& release : releases) {
        if (!channelOffers(channel, release.version.channel)) continue;
        if (release.version <= current) continue;
        if (!best || release.version > best->version) best = &release;
    }
    if (!best) return std::nullopt;
    return *best;
}

QByteArray parseSha256File(const QByteArray& contents) {
    const QByteArray word = contents.trimmed().split(' ').value(0).split('\t').value(0).trimmed().toLower();
    return word.size() == 64 && isHex(word) ? word : QByteArray();
}

}  // namespace sub::app
