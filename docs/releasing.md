# Releasing

SUBstation is released from GitHub Actions, as GitHub releases of this repository, on three channels. The updater
built into SUBstation ([app/updates.md](app/updates.md), [guide/updates.md](guide/updates.md)) reads those releases
and offers the newest one on the channel the user chose.

| Channel | Made | Tag | Kept |
|---|---|---|---|
| **Stable** | a tag `v<MAJOR.MINOR.PATCH>` pushed | `v0.3.0` | always; the repository's *latest* release |
| **Nightly** | once a night (07:30 UTC), if `master` moved since the last nightly | `v0.3.0-nightly.202610140730` | the newest 7 (a pre-release) |
| **Canary** | each push to `master` (a newer push cancels an older one's build) | `v0.3.0-canary.202610141532` | the newest 10 (a pre-release) |

Each release has two downloads, each with its SHA-256 beside it (`<download>.sha256`, as `sha256sum` writes it):

- **`SUBstation-<version>-windows-x64-setup.exe`**, the installer ([below](#the-installer)): what most people take.
- **`SUBstation-<version>-windows-x64.zip`**, portable: a folder, `SUBstation/`, to unzip anywhere and run from.

Both hold the same files: the programs (`substation.exe`, `substation-scan.exe`, `substation-plugin-host.exe`), Qt's
libraries and QML modules (`windeployqt`), MSVC's runtime, `LICENSE.txt`, `licenses/` (the licence texts the AGPLv3
asks to ship, and where the build's source is: `licenses/README.txt`) and `VERSION.txt`. The releases are built with
Steinberg's ASIO® SDK ([below](#asio)) and signed once code signing is set up ([below](#code-signing)).

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
2. **Build** (Windows, MSVC 2022, Qt 6.8): the ASIO SDK downloaded, then `-DSUBSTATION_BUILD_TESTS=OFF
   -DSUBSTATION_TEST_PLUGINS=OFF` and the three variables above. Then
   [tools/release/package-windows.ps1](../tools/release/package-windows.ps1), a step at a time so the signing comes
   between: **Stage** (`dist/SUBstation`: the programs, `windeployqt`, the runtime, the licences), the programs
   **signed**, **Archive** (the zip) and **Installer** (Inno Setup on
   [tools/release/substation.iss](../tools/release/substation.iss)), the installer **signed**, **Checksums**. The tests
   aren't run here: CI and the Windows nightly run them ([testing.md](testing.md#ci)).
3. **Publish** (Linux): `gh release create` with the downloads and their checksums. Stable: notes generated from the pull
   requests since the last release, marked latest. Nightly and canary: a pre-release at the commit built, never latest;
   then the oldest of that channel beyond the 7 or 10 kept are deleted, with their tags.

The workflow needs `contents: write` (releases and tags), which it asks for itself.

### ASIO

The releases (and the Windows nightly's MSVC build) download Steinberg's ASIO® SDK 2.3.4
(`https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip`), check it against its pinned SHA-256,
unpack it outside the checkout and point `SUBSTATION_ASIO_SDK` at it. 2.3.4 is the first SDK Steinberg offers under
GPLv3 as well as its own licence; SUBstation takes it under GPLv3, which goes with the AGPLv3 the program is shipped
under ([licensing.md](licensing.md)), and ships its licence text (`licenses/asio-sdk-LICENSE.txt`). A newer SDK means a
new URL and SHA-256 in both workflows.

Steinberg's usage guidelines ask for the attribution *"ASIO is a registered trademark of Steinberg Media Technologies
GmbH."* in the product's credits and documentation, and its *ASIO Compatible* logo in the About box and wherever ASIO
is set up (the text where there's no room for the logo). Help › About and Preferences › Audio show the text in a build
with ASIO (`AudioPreferences::trademarkNotice()`), as do `licenses/README.txt` and these docs; the logo isn't shown
yet (it is in the SDK, `Steinberg ASIO Logo Artwork`).

### Code signing

The programs (`substation*.exe`) are signed before they are zipped and put in the installer, and then the installer,
with [Azure Artifact Signing](https://learn.microsoft.com/azure/artifact-signing/) (formerly Trusted Signing), through
`azure/artifact-signing-action` (v2.0.0, pinned by commit). Signed, Windows shows the publisher's name, SmartScreen
learns to trust the downloads, and the updater installs only an update signed by the same publisher as the SUBstation
it updates ([app/updates.md](app/updates.md#installing)).

Until it is set up the releases are made unsigned, with a warning in the run. To set it up:

1. In Azure, create an Artifact Signing account and, after the identity validation, a certificate profile (Public
   Trust). Note the account's endpoint (its region's, e.g. `https://eus.codesigning.azure.net/`), its name and the
   profile's name.
2. Create an App Registration (Microsoft Entra ID), give it a client secret, and give it the *Artifact Signing
   Certificate Profile Signer* role on the certificate profile.
3. In the repository's Settings › Secrets and variables › Actions, add the **secrets** `AZURE_TENANT_ID`,
   `AZURE_CLIENT_ID` and `AZURE_CLIENT_SECRET`, and the **variables** `ARTIFACT_SIGNING_ENDPOINT`,
   `ARTIFACT_SIGNING_ACCOUNT` and `ARTIFACT_SIGNING_PROFILE`.

The next release is signed (the build signs when `AZURE_CLIENT_ID` and `ARTIFACT_SIGNING_PROFILE` are set). Check one
with `Get-AuthenticodeSignature substation.exe` or the file's Properties › Digital Signatures. Qt's and MSVC's DLLs
keep their makers' signatures.

### The installer

[tools/release/substation.iss](../tools/release/substation.iss) (Inno Setup 6, preinstalled on GitHub's Windows
runners; else Chocolatey installs it). Per user, without an administrator: SUBstation goes into
`%LOCALAPPDATA%\Programs\SUBstation`, with a Start menu entry, an optional desktop shortcut, `.gilproj` projects
opening in SUBstation (on by default), and an uninstaller (Settings › Apps). It shows `licenses/README.txt` as its
licence page. Installing over an installed version keeps its folder and choices; Qt's `qml` and `platforms` folders
and `licenses` are replaced whole, so nothing of an older version lingers there. The user's projects, presets and
settings are never touched, by installing or uninstalling.

Its `AppId` must never change: it is how a version finds the installation it updates. Its version information is the
version's numbers (`0.3.0.0` for any `0.3.0` build).

## Making a stable release

1. On `master`, `CMakeLists.txt`'s version is the release's (say `0.3.0`), and the Windows nightly is green.
2. Tag it and push the tag: `git tag v0.3.0 && git push origin v0.3.0`. The workflow builds and publishes it.
3. Raise `CMakeLists.txt`'s version for the next one (`0.3.1`), in a pull request.

A nightly or canary can be made by hand too: Actions › Release › Run workflow, choosing the channel and the branch.

## Not done yet

- **An install for all users** (`Program Files`, needing an administrator): the installer is per user, so the updater
  never needs one.
- **The ASIO Compatible logo** in Help › About and Preferences › Audio ([above](#asio)).
- **An icon of its own for the installer** (it has Inno Setup's): the repository has no `.ico` yet.
- **Linux packages.** Releases are Windows-only: on Linux the updater says a release is out, and links its page.
- **Delta updates, rollback.** An update is the whole zip; the folder it replaced isn't kept.
