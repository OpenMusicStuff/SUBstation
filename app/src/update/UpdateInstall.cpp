#include "update/UpdateInstall.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace sub::app {

namespace {

// (A raw string: what PowerShell reads, as is.)
constexpr const char kScript[] = R"ps(# Installs a SUBstation update: written and started by SUBstation as it quits.
param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$AppDir,
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [string]$Restart = '0',
    [string]$Log = ''
)
$ErrorActionPreference = 'Stop'
if ($Log) { Start-Transcript -LiteralPath $Log -Force | Out-Null }
$staging = Join-Path ([IO.Path]::GetTempPath()) ('substation-update-' + [guid]::NewGuid().ToString('N'))
$code = 0
try {
    # SUBstation (and with it its plug-in sandboxes and scanners) ends first.
    Wait-Process -Id $ProcessId -Timeout 120 -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    Expand-Archive -LiteralPath $Zip -DestinationPath $staging -Force
    $exe = Get-ChildItem -LiteralPath $staging -Recurse -Filter 'substation.exe' | Select-Object -First 1
    if (-not $exe) { throw "The update has no substation.exe." }
    # Retries while a file is still held (an antivirus scanning it, a process ending).
    & robocopy $exe.DirectoryName $AppDir /E /R:20 /W:1 /NP /NFL /NDL | Out-Host
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)." }
    Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue
} catch {
    Write-Output "The update wasn't installed: $_"
    $code = 1
} finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
if ($Restart -eq '1') { Start-Process -FilePath (Join-Path $AppDir 'substation.exe') }
if ($Log) { Stop-Transcript | Out-Null }
exit $code
)ps";

}  // namespace

bool canInstallUpdates() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

QByteArray updateInstallScript() { return QByteArray(kScript); }

UpdateInstallCommand updateInstallCommand(const QString& scriptPath, const QString& zipPath, const QString& appDir,
                                          qint64 pid, bool restart) {
    const QString log = QFileInfo(scriptPath).absolutePath() + QStringLiteral("/install-update.log");
    return {QStringLiteral("powershell.exe"),
            {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
             QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
             QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath),
             QStringLiteral("-Zip"), QDir::toNativeSeparators(zipPath),
             QStringLiteral("-AppDir"), QDir::toNativeSeparators(appDir),
             QStringLiteral("-ProcessId"), QString::number(pid),
             QStringLiteral("-Restart"), restart ? QStringLiteral("1") : QStringLiteral("0"),
             QStringLiteral("-Log"), QDir::toNativeSeparators(log)}};
}

QByteArray sha256OfFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}

}  // namespace sub::app
