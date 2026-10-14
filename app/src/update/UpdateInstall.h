#pragma once
// Installing a downloaded update (docs/app/updates.md). SUBstation can't
// replace its own files while it runs: as it quits it starts a script that
// waits for it to end, then runs the release's installer silently (a
// SUBstation the installer installed: its folder holds unins000.exe) or
// unpacks the release's zip and copies it over the folder SUBstation runs
// from (one unzipped by hand), and (asked to) starts it again. A signed
// SUBstation takes only an update signed by the same publisher. On Windows
// only: the script is Windows PowerShell's, the copy robocopy's. Elsewhere an
// update is offered, not installed.

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace sub::app {

struct UpdateInstallCommand {
    QString program;
    QStringList arguments;
};

// Whether this system installs updates (Windows).
bool canInstallUpdates();

// Whether the SUBstation in `appDir` was installed by the installer (its uninstaller is there).
bool isInstalledCopy(const QString& appDir);

// The PowerShell script (UTF-8). Its parameters: -Package (the installer or the
// zip), -AppDir, -ProcessId (of the SUBstation to wait for), -Restart (1 or 0),
// -Log (where it writes what it did).
QByteArray updateInstallScript();

// What runs the script at `scriptPath` on `packagePath`, for `appDir`, once process `pid` has ended.
UpdateInstallCommand updateInstallCommand(const QString& scriptPath, const QString& packagePath, const QString& appDir,
                                          qint64 pid, bool restart);

// A file's SHA-256 in lower-case hex ("" if it can't be read).
QByteArray sha256OfFile(const QString& path);

}  // namespace sub::app
