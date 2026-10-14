#include "AppInfo.h"

// (Set by app/CMakeLists.txt; the fallbacks are for a build that doesn't go through it.)
#ifndef SUBSTATION_VERSION
#define SUBSTATION_VERSION "0.0.0"
#endif
#ifndef SUBSTATION_RELEASE_CHANNEL
#define SUBSTATION_RELEASE_CHANNEL "dev"
#endif
#ifndef SUBSTATION_UPDATE_REPOSITORY
#define SUBSTATION_UPDATE_REPOSITORY "OpenMusicStuff/SUBstation"
#endif

namespace sub::app {

QString version() { return QStringLiteral(SUBSTATION_VERSION); }

QString releaseChannel() { return QStringLiteral(SUBSTATION_RELEASE_CHANNEL); }

QString updateRepository() { return QStringLiteral(SUBSTATION_UPDATE_REPOSITORY); }

}  // namespace sub::app
