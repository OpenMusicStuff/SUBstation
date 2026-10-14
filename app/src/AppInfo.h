#pragma once
// The application's name and version, and where it keeps its settings. The
// version and the release channel come from the build (CMakeLists.txt,
// docs/releasing.md): "0.3.0" for a stable release, "0.3.0-nightly.202610140700"
// for a nightly, "0.3.0-canary.202610141532" for a canary; a build by hand is
// the project's version on the "dev" channel.

#include <QString>

namespace sub::app {

inline constexpr const char* kAppName = "SUBstation";
inline constexpr const char* kOrganization = "SUBstation";

QString version();
// "dev", "stable", "nightly" or "canary".
QString releaseChannel();
// The GitHub repository whose releases are the updates: "OpenMusicStuff/SUBstation".
QString updateRepository();

}  // namespace sub::app
