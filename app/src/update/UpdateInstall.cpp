#include "update/UpdateInstall.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace sub::app {

namespace {

// (A raw string: what PowerShell reads, as is.)
constexpr const char kScript[] = R"ps(# Installs a SUBstation update: written and started by SUBstation as it quits.
# -Package is the release's installer (SUBstation-<version>-windows-x64-setup.exe), run silently, for a
# SUBstation the installer installed; or its zip, copied over -AppDir, for one unzipped by hand.
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$AppDir,
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [string]$Restart = '0',
    [string]$Log = ''
)
$ErrorActionPreference = 'Stop'
if ($Log) { Start-Transcript -LiteralPath $Log -Force | Out-Null }

# A signed SUBstation installs only what the same publisher signed (an unsigned
# build, one built by hand, has nothing to compare with).
function Assert-SameSigner([string]$Candidate) {
    $current = Get-AuthenticodeSignature -LiteralPath (Join-Path $AppDir 'substation.exe')
    if ($current.Status -ne 'Valid') { return }
    $new = Get-AuthenticodeSignature -LiteralPath $Candidate
    if ($new.Status -ne 'Valid') { throw "$Candidate isn't validly signed ($($new.Status))." }
    if ($new.SignerCertificate.Subject -ne $current.SignerCertificate.Subject) {
        throw "$Candidate is signed by $($new.SignerCertificate.Subject), not $($current.SignerCertificate.Subject)."
    }
}

$staging = Join-Path ([IO.Path]::GetTempPath()) ('substation-update-' + [guid]::NewGuid().ToString('N'))
$code = 0
try {
    # SUBstation (and with it its plug-in sandboxes and scanners) ends first.
    Wait-Process -Id $ProcessId -Timeout 120 -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    if ($Package -like '*.exe') {
        Assert-SameSigner $Package
        # (Into the folder and with the choices of the installation it updates.)
        $setup = Start-Process -FilePath $Package -Wait -PassThru `
            -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/CLOSEAPPLICATIONS'
        if ($setup.ExitCode -ne 0) { throw "The installer failed ($($setup.ExitCode))." }
    } else {
        Expand-Archive -LiteralPath $Package -DestinationPath $staging -Force
        $exe = Get-ChildItem -LiteralPath $staging -Recurse -Filter 'substation.exe' | Select-Object -First 1
        if (-not $exe) { throw "The update has no substation.exe." }
        Assert-SameSigner $exe.FullName
        # Retries while a file is still held (an antivirus scanning it, a process ending).
        & robocopy $exe.DirectoryName $AppDir /E /R:20 /W:1 /NP /NFL /NDL | Out-Host
        if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)." }
    }
    Remove-Item -LiteralPath $Package -Force -ErrorAction SilentlyContinue
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

bool isInstalledCopy(const QString& appDir) {
    return QFileInfo::exists(appDir + QStringLiteral("/unins000.exe"));
}

UpdateInstallCommand updateInstallCommand(const QString& scriptPath, const QString& packagePath, const QString& appDir,
                                          qint64 pid, bool restart) {
    const QString log = QFileInfo(scriptPath).absolutePath() + QStringLiteral("/install-update.log");
    return {QStringLiteral("powershell.exe"),
            {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
             QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
             QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath),
             QStringLiteral("-Package"), QDir::toNativeSeparators(packagePath),
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
