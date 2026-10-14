# Updates

[app/src/update](../../app/src/update): finding a newer SUBstation, downloading it and installing it, for
Preferences › Updates ([guide/updates.md](../guide/updates.md)). The releases themselves are made by the release
workflow ([releasing.md](../releasing.md)).

| File | What it is |
|---|---|
| [UpdateFeed.h](../../app/src/update/UpdateFeed.h) | the pure part: `UpdateChannel`, `UpdateVersion` (parsed, written, ordered), `UpdateRelease`, reading GitHub's list of releases (`parseUpdateReleases`), picking the update (`pickUpdate`), reading a `.sha256` file |
| [UpdateInstall.h](../../app/src/update/UpdateInstall.h) | installing: the PowerShell script, the command that starts it, a file's SHA-256 |
| [Updater.h](../../app/src/update/Updater.h) | the `QObject` behind the page (`Session.updates`): the settings, looking, downloading, installing as SUBstation quits |

## Channels and versions

`UpdateChannel` is `Stable`, `Nightly` or `Canary`, each offered what the one before it is and more
(`channelOffers`): Stable is offered stable releases, Nightly those and the nightlies, Canary everything.

`UpdateVersion::parse` reads `0.3.0`, `0.3.0-nightly.202610140730` and `0.3.0-canary.202610141532`, with or without
a `v` before (a tag); anything else is no version. They are ordered by the numbers, then a stable release above the
builds of the same numbers, then by stamp, whatever the channel (`operator<=>`): so a canary built after a nightly is
newer than it, and someone on a nightly before `0.3.0` is offered `0.3.0` on Nightly as on Stable.

## The feed

The feed is GitHub's REST list of the repository's releases, `https://api.github.com/repos/<repo>/releases?per_page=50`
(`updateRepository()`, [AppInfo.h](../../app/src/AppInfo.h)), unauthenticated (GitHub allows 60 requests an hour from
an address: one look a day is far below it). `SUBSTATION_UPDATE_FEED` replaces it with any URL, `file:` included (the
tests'). `parseUpdateReleases` keeps each release whose tag is a version and that isn't a draft, with its title,
notes, page and date, and the asset named `SUBstation-<version><suffix>`, the suffix being this platform's
(`updatePlatformSuffix()`: `-windows-x64.zip` on Windows; `-linux-x64.zip` elsewhere, which no release has yet). The
asset's checksum is GitHub's `digest` (`sha256:<hex>`) when there is one, else the asset `<zip>.sha256` beside it.

`pickUpdate` takes the newest release the channel offers that is newer than this build. Releases with no download for
this platform are still offered (the page then says so and links the release).

## Updater

Its properties are the page's: `channel`, `channels` (`[{value, label, description}]`), `autoCheck`, `autoInstall`
(read and written, kept in QSettings at once); `currentVersion`, `buildChannel`, `automaticChecks` (not a `dev`
build), `canInstall` (Windows); `state`, `status`, `progress`, `availableVersion`, `releaseTitle`, `releaseUrl`,
`installPending`, `lastChecked`; and the settings in words, `summaryTitle` ("Selected: Stable") and `summary`.

`state` goes:

```
idle ──checkNow──▶ checking ──▶ upToDate
                       │  └───▶ error           (no feed, not JSON: status says why)
                       ▼
                   available ──download──▶ downloading ──▶ ready ──installNow / quit──▶ (installer)
                                                   └────▶ error (no checksum, a mismatch, a failed write)
```

- **Looking.** `checkNow()` GETs the feed (30 s without a byte is a failure; redirects followed if no less safe),
  records the time (`updates/last_check`) when it read a list, and picks. An update found is `available`, and says so
  (`updateAvailable`, which the session puts on the status line: *SUBstation 0.3.0 is available: Options › Preferences ›
  Updates.*). While `checking`, `downloading` or `ready`, it does nothing.
- **By itself.** With `automaticChecks` and `autoCheck`, it looks `firstCheckDelayMs` (15 s) after it is made, and an
  hourly timer looks again when a day has passed since the last (`isDue`: never looked, a day since, or a clock that
  went back). A dev build doesn't: Check Now (and Help › Check for Updates…) still does. A channel chosen drops what was
  found and, looking by itself, looks again at once; `autoCheck` turned on looks if due.
- **Downloading.** `download()` (at once after a look with `autoInstall`; or when `autoInstall` is turned on with an
  update `available`) refuses a release with no checksum. It fetches the `.sha256` first if there's no digest, then the
  zip into `AppLocalDataLocation/updates/<zip>.part` (earlier zips there deleted), hashing as it comes; a hash that
  doesn't match deletes it (`error`); one that does renames it to `<zip>` and is `ready`.
- **Installing.** `installNow()` (Restart to Install) marks it to install and restart and emits `quitRequested`; the
  window closes as Quit closes it, asking about unsaved changes (a close cancelled leaves it to install at the next).
  `applyOnQuit()`, called by `main()` after the session has shut down, installs if `installPending` (ready, on
  Windows, and `autoInstall` or `installNow()`): it checks the zip's hash again, writes the script beside it
  (`install-update.ps1`) and starts it detached (`Options::launch`; `QProcess::startDetached`), with this process's id,
  the zip, the application's folder, and whether to start SUBstation again (only after `installNow()`).

The script ([UpdateInstall.cpp](../../app/src/update/UpdateInstall.cpp)) waits for that process to end (up to two
minutes), unpacks the zip into a temporary folder, finds `substation.exe` in it, and `robocopy`s its folder over the
application's (retrying files still held for 20 s), deletes the zip and the temporary folder, and starts SUBstation
again if asked (the old one, if the copy failed). It writes what it did to `install-update.log` beside the script.

`Options` is for the tests: the feed, the version and channel to be, the platform suffix, the folders, whether it looks
by itself and installs, the first look's delay, and what starts the installer.

## Settings

| Key | Holds | Default |
|---|---|---|
| `updates/channel` | `stable`, `nightly` or `canary` | the build's channel (a `dev` build's: `stable`) |
| `updates/auto_check` | look by itself | `true` |
| `updates/auto_install` | download at once, install on closing | `false` |
| `updates/last_check` | when it last read the feed (UTC) | none |

## Tests

- [test_updates.cpp](../../tests/app/test_updates.cpp): versions and their order, the channels, GitHub's list read
  and a release picked on each channel, `.sha256` files, `isDue`; the `Updater` on feeds and zips in files: the
  settings (kept, the defaults, the summary), Check Now finding none and one, a download checked and the installer
  started only once asked (its command and script), Install updates automatically, a checksum that doesn't match, no
  checksum, no feed, looking by itself (and not when not due, or not on).
- [test_ui_dialogs.cpp](../../tests/app/test_ui_dialogs.cpp) `updatesPage`: the page driven with the mouse.
- `prepareApplication()` points `SUBSTATION_UPDATE_FEED` at a file that isn't there, so no test reads GitHub.
