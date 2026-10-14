#pragma once
// The updates' pure part (docs/app/updates.md): versions, channels, and the
// releases the feed lists, without the network.
//
// A version is MAJOR.MINOR.PATCH, and a nightly or a canary build has its
// channel and a build stamp after it (UTC yyyyMMddHHmm): "0.3.0",
// "0.3.0-nightly.202610140700", "0.3.0-canary.202610141532". A release's tag is
// its version after a "v". Ordered by the numbers first; with the same numbers a
// stable release is newer than any build that led up to it, and builds are
// ordered by their stamps, whatever their channel.
//
// The feed is GitHub's list of the repository's releases
// (GET /repos/{owner}/{repo}/releases): each release's tag says its version and
// channel; its assets hold the download for this platform
// (SUBstation-<version>-windows-x64.zip) and its SHA-256 (GitHub's `digest`,
// else a <zip>.sha256 asset beside it).

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <compare>
#include <cstdint>
#include <optional>
#include <vector>

namespace sub::app {

// In the order each takes in more: Stable's releases are Nightly's too, and Canary is offered everything.
enum class UpdateChannel { Stable, Nightly, Canary };

std::optional<UpdateChannel> updateChannelNamed(const QString& name);  // "stable", "nightly", "canary"
QString nameOf(UpdateChannel channel);
// Whether someone on `subscribed` is offered a release of `release`'s.
bool channelOffers(UpdateChannel subscribed, UpdateChannel release);

struct UpdateVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    UpdateChannel channel = UpdateChannel::Stable;
    int64_t build = 0;  // a nightly's or canary's stamp; 0 for a stable release

    // A version or a tag ("v0.3.0-nightly.202610140700"); nullopt if it is neither.
    static std::optional<UpdateVersion> parse(const QString& text);
    QString toString() const;

    std::strong_ordering operator<=>(const UpdateVersion& other) const;
    bool operator==(const UpdateVersion& other) const { return (*this <=> other) == 0; }
};

struct UpdateRelease {
    UpdateVersion version;
    QString tag;
    QString title;    // the release's name (else its tag)
    QString notes;    // its description (Markdown)
    QUrl pageUrl;     // its page on GitHub
    QDateTime published;
    // The download for this platform (empty: none), its size and its SHA-256: in
    // hex, from GitHub's digest, or else where to read it (a .sha256 asset).
    QString assetName;
    QUrl assetUrl;
    qint64 assetSize = 0;
    QByteArray sha256;
    QUrl sha256Url;
};

// The download's name for `version` on this platform's suffix ("-windows-x64.zip").
QString updateAssetName(const UpdateVersion& version, const QString& platformSuffix);
// This platform's suffix: "-windows-x64.zip" on Windows; elsewhere "-linux-x64.zip" (none is released yet).
QString updatePlatformSuffix();

// The releases in a GitHub releases list (drafts and tags that aren't versions
// left out), the assets matched by `platformSuffix`. `error` says why if the
// list isn't one (the empty list is one).
std::vector<UpdateRelease> parseUpdateReleases(const QByteArray& json, const QString& platformSuffix,
                                               QString* error = nullptr);

// The newest release offered on `channel` that is newer than `current`, if any.
std::optional<UpdateRelease> pickUpdate(const std::vector<UpdateRelease>& releases, UpdateChannel channel,
                                        const UpdateVersion& current);

// A <file>.sha256 asset's checksum: its first word, in lower-case hex ("" unless it is 64 hex digits).
QByteArray parseSha256File(const QByteArray& contents);

}  // namespace sub::app
