# Packages a Windows build of SUBstation as a release's download (docs/releasing.md):
#
#   tools/release/package-windows.ps1 -Build build -Version 0.3.0-nightly.202610140700 -Out dist
#
# Copies the programs (substation, the plug-in scanner and the plug-in host) into
# dist/SUBstation, adds the Qt libraries and QML modules they use (windeployqt) and
# MSVC's runtime (app-local, from VCToolsRedistDir: run it in a Developer shell), and
# zips that folder as dist/SUBstation-<version>-windows-x64.zip, its SHA-256 beside it
# (<zip>.sha256, as sha256sum writes it). The updater installs the zip's SUBstation
# folder over the one it runs from.
param(
    [Parameter(Mandatory = $true)][string]$Build,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Out = 'dist'
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bin = Join-Path $Build 'bin'
$stage = Join-Path $Out 'SUBstation'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$programs = 'substation.exe', 'substation-scan.exe', 'substation-plugin-host.exe'
foreach ($program in $programs) {
    $path = Join-Path $bin $program
    if (-not (Test-Path $path)) { throw "$path isn't built." }
    Copy-Item $path $stage
}
# (DLLs the build put next to them, if any.)
Get-ChildItem -Path $bin -Filter '*.dll' -ErrorAction SilentlyContinue | Copy-Item -Destination $stage

# Qt: the libraries, the platform plug-in, and the QML modules ui/ imports.
& windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    --qmldir (Join-Path $root 'ui') (Join-Path $stage 'substation.exe')
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)." }

# MSVC's runtime, beside the programs (no installer to run first).
if ($env:VCToolsRedistDir) {
    $crt = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
        Select-Object -First 1
    if ($crt) { Copy-Item (Join-Path $crt.FullName '*.dll') $stage }
}

Copy-Item (Join-Path $root 'LICENSE') $stage
Set-Content -Path (Join-Path $stage 'VERSION.txt') -Value $Version -Encoding ascii

$name = "SUBstation-$Version-windows-x64.zip"
$zip = Join-Path $Out $name
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
Set-Content -Path "$zip.sha256" -Value "$hash  $name" -Encoding ascii -NoNewline
Write-Output "$name ($hash)"
