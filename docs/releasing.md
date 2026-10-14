# Releasing

SUBstation is released from GitHub Actions, as GitHub releases of this repository, on three channels. The updater
built into SUBstation ([app/updates.md](app/updates.md), [guide/updates.md](guide/updates.md)) reads those releases
and offers the newest one on the channel the user chose.

| Channel | Made | Tag | Kept |
|---|---|---|---|
| **Stable** | a tag `v<MAJOR.MINOR.PATCH>` pushed | `v0.3.0` | always; the repository's *latest* release |
| **Nightly** | once a night (07:30 UTC), if `master` moved since the last nightly | `v0.3.0-nightly.202610140730` | the newest 7 (a pre-release) |
| **Canary** | each push to `master` (a newer push cancels an older one's build) | `v0.3.0-canary.202610141532` | the newest 10 (a pre-release) |

Each release has one download for now, `SUBstation-<version>-windows-x64.zip`, and its SHA-256 beside it
(`<zip>.sha256`, as `sha256sum` writes it). The zip holds a folder, `SUBstation/`: the programs (`substation.exe`,
`substation-scan.exe`, `substation-plugin-host.exe`), Qt's libraries and QML modules (`windeployqt`), MSVC's runtime,
the licence and `VERSION.txt`.

## Versions

A version is `MAJOR.MINOR.PATCH`; a nightly or canary build adds its channel and a stamp, the UTC time it was built
(`yyyyMMddHHmm`): `0.3.0-nightly.202610140730`. They are ordered by the numbers first; with the same numbers a stable
release is newer than any build that led up to it; builds are ordered by their stamps, whatever their channel. The
tag is the version after a `v`.

`project(SUBstation VERSION ...)` in [CMakeLists.txt](../CMakeLists.txt) is the **next** stable release's version: the
nightlies and canaries are builds leading up to it. So right after releasing `0.3.0`, raise it (`0.3.1` or `0.4.0`):
until then the nightlies are still `0.3.0-nightly...`, older than `0.3.0`, and the updater offers `0.3.0` over them.

The build learns what it is from three CMake variables, which the workflow sets (a build by hand has the defaults):

| Variable | Default | What it is |
|---|---|---|
| `SUBSTATION_RELEASE_CHANNEL` | `dev` | `dev`, `stable`, `nightly` or `canary`. A `dev` build never looks for updates by itself. |
| `SUBSTATION_VERSION_SUFFIX` | (empty) | After the version and a hyphen: `nightly.<stamp>` or `canary.<stamp>` (required for those channels). |
| `SUBSTATION_UPDATE_REPOSITORY` | `OpenMusicStuff/SUBstation` | Whose releases are the updates. |

`AppInfo.h` hands them to the rest (`version()`, `releaseChannel()`, `updateRepository()`); only `AppInfo.cpp` is
compiled with them, so a nightly's stamp rebuilds one file. Help › About shows the version.

## The workflow

[.github/workflows/release.yml](../.github/workflows/release.yml):

1. **Plan** (Linux): the channel (from the event: a tag is stable, the schedule nightly, a push to `master` canary;
   by hand, the one chosen), the version from `CMakeLists.txt`, the stamp, the tag. A stable release must come from its
   own tag, the same as `CMakeLists.txt`'s version: otherwise it fails. A scheduled nightly with `master` where the last
   nightly's tag is stops here.
2. **Build** (Windows, MSVC 2022, Qt 6.8): `-DSUBSTATION_BUILD_TESTS=OFF -DSUBSTATION_TEST_PLUGINS=OFF` and the three
   variables above, then [tools/release/package-windows.ps1](../tools/release/package-windows.ps1) makes the zip and its
   checksum. The tests aren't run here: CI and the Windows nightly run them
   ([testing.md](testing.md#ci)).
3. **Publish** (Linux): `gh release create` with the zip and the checksum. Stable: notes generated from the pull
   requests since the last release, marked latest. Nightly and canary: a pre-release at the commit built, never latest;
   then the oldest of that channel beyond the 7 or 10 kept are deleted, with their tags.

The workflow needs `contents: write` (releases and tags), which it asks for itself.

### ASIO

Steinberg's ASIO SDK isn't redistributable, so it isn't in the repository ([building.md](building.md#asio-sdk)), and
without it the releases have WASAPI only. To build them with ASIO, put a link to a zip of the SDK in the repository
secret `ASIO_SDK_URL`: the build downloads it and points `SUBSTATION_ASIO_SDK` at it.

## Making a stable release

1. On `master`, `CMakeLists.txt`'s version is the release's (say `0.3.0`), and the Windows nightly is green.
2. Tag it and push the tag: `git tag v0.3.0 && git push origin v0.3.0`. The workflow builds and publishes it.
3. Raise `CMakeLists.txt`'s version for the next one (`0.3.1`), in a pull request.

A nightly or canary can be made by hand too: Actions › Release › Run workflow, choosing the channel and the branch.

## Not done yet

- **Code signing.** The zips aren't signed: Windows SmartScreen warns about a program from a download it doesn't know,
  and the SHA-256 the updater checks comes from the same release as the zip, so it guards against a download broken
  on the way, not against a release that was tampered with. Signing (Authenticode, with the certificate in a secret)
  and checking the signature before installing are the next steps.
- **An installer.** The zip is unpacked wherever the user likes; there is no Start menu entry or uninstaller. The
  updater copies the new files over the folder SUBstation runs from, so that folder must be writable by the user (not
  `Program Files`).
- **Linux packages.** Releases are Windows-only: on Linux the updater says a release is out, and links its page.
- **Delta updates, rollback.** An update is the whole zip; the folder it replaced isn't kept.
