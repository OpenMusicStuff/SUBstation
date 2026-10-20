# Packages a Windows build of SUBstation as a release's downloads (docs/releasing.md), a step at a time
# so the release workflow can sign between them:
#
#   tools/release/package-windows.ps1 -Step Stage     -Build build -Version 0.3.0 -Out dist [-Repository o/r -Commit sha]
#   (sign dist/SUBstation/*.exe)
#   tools/release/package-windows.ps1 -Step Archive   -Version 0.3.0 -Out dist
#   tools/release/package-windows.ps1 -Step Installer -Version 0.3.0 -Out dist
#   (sign dist/*-setup.exe)
#   tools/release/package-windows.ps1 -Step Checksums -Out dist
#
# Stage: dist/SUBstation holds the programs (substation, the plug-in scanner and the plug-in host), the Qt
#   libraries and QML modules they use (windeployqt), the compiler's runtime (MinGW's DLLs, from the folder of
#   the g++ the build used; with MSVC, app-local from VCToolsRedistDir: run it in a Developer shell), the
#   licences (licenses/: the texts the AGPLv3 asks to ship, and where the source is) and VERSION.txt.
# Archive: dist/SUBstation-<version>-windows-x64.zip of that folder (the portable download; the updater
#   copies it over a SUBstation unzipped by hand).
# Installer: dist/SUBstation-<version>-windows-x64-setup.exe (Inno Setup, tools/release/substation.iss): a
#   per-user install, which the updater runs silently to update an installed SUBstation.
# Checksums: <download>.sha256 beside each download, as sha256sum writes it.
param(
    [Parameter(Mandatory = $true)][ValidateSet('Stage', 'Archive', 'Installer', 'Checksums')][string]$Step,
    [string]$Build = 'build',
    [string]$Version = '',
    [string]$Out = 'dist',
    [string]$Repository = 'OpenMusicStuff/SUBstation',
    [string]$Commit = ''
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$stage = Join-Path $Out 'SUBstation'
if ($Step -ne 'Checksums' -and -not $Version) { throw "-Version is needed for the $Step step." }

function Copy-Licences([string]$To) {
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    # Every licence file in the repository's own code and its vendored libraries (not the tests'),
    # named after where it is: engine/third_party/vital/LICENSE -> engine-third_party-vital-LICENSE.txt.
    $files = Get-ChildItem -Path $root -Recurse -File -Include 'LICENSE*', 'COPYING*', 'NOTICE*' |
        Where-Object { $_.FullName -notmatch '[\\/](build[^\\/]*|dist|tests|\.git)[\\/]' }
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length + 1) -replace '[\\/]', '-'
        if ($relative -notmatch '\.(txt|md)$') { $relative += '.txt' }
        Copy-Item $file.FullName (Join-Path $To $relative)
    }
    # Steinberg's ASIO SDK, if the build used it (under its GPLv3 option: docs/licensing.md).
    if ($env:SUBSTATION_ASIO_SDK -and (Test-Path (Join-Path $env:SUBSTATION_ASIO_SDK 'common/LICENSE.txt'))) {
        Copy-Item (Join-Path $env:SUBSTATION_ASIO_SDK 'common/LICENSE.txt') (Join-Path $To 'asio-sdk-LICENSE.txt')
    }
    $source = if ($Commit) { "https://github.com/$Repository/tree/$Commit" } else { "https://github.com/$Repository" }
    $asio = if ($env:SUBSTATION_ASIO_SDK) {
        "`r`nThis build uses Steinberg's ASIO SDK under its GPLv3 option (asio-sdk-LICENSE.txt).`r`n" +
        "ASIO is a registered trademark of Steinberg Media Technologies GmbH.`r`n"
    } else { '' }
    @"
SUBstation $Version

SUBstation's own code is under the MIT licence (LICENSE.txt). This build includes Essentia, which is under
the GNU Affero General Public License version 3 (intelligence-third_party-essentia-COPYING.txt), and Vital,
under the GNU General Public License version 3 (engine-third_party-vital-LICENSE.txt), so the program as a
whole is distributed under the GNU AGPLv3. You may use, study, share and change it under those terms.

The complete corresponding source of this build:
  $source

The other files here are the licences of the libraries SUBstation includes (named after where they are in
the source). Qt is used under the GNU LGPLv3 (https://www.qt.io/licensing). The sound tagger's model was
trained on NSynth, TinySOL, LibriSpeech and VocalSet (CC BY 4.0) and Freesound sounds (CC0 and CC BY 3.0):
see tools/sound_tagger/README.md in the source. The piano roll's progression ideas start from free-midi-chords'
progressions (MIT: intelligence-third_party-free-midi-chords-LICENSE.txt) and draw on chord changes counted from the
McGill Billboard Project's annotations (CC0; Burgoyne, Wild and Fujinaga, ISMIR 2011).
$asio
VST is a registered trademark of Steinberg Media Technologies GmbH.
"@ | Set-Content -Path (Join-Path $To 'README.txt') -Encoding utf8
}

function Get-Sha256([string]$Path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant() }

switch ($Step) {
    'Stage' {
        $bin = Join-Path $Build 'bin'
        if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        foreach ($program in 'substation.exe', 'substation-scan.exe', 'substation-plugin-host.exe') {
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

        # The compiler's runtime, beside the programs (no installer to run first). MinGW's: from the folder of the
        # g++ the build used (windeployqt brings them for substation.exe, but only if it finds that MinGW; the
        # scanner and the plug-in host need them too). MSVC's: from its redistributable folder.
        $cache = Join-Path $Build 'CMakeCache.txt'
        $compiler = if (Test-Path $cache) {
            (Select-String -Path $cache -Pattern '^CMAKE_CXX_COMPILER:[A-Z]+=(.+)$' | Select-Object -First 1).Matches.Groups[1].Value
        } else { '' }
        if ($compiler -match '(g\+\+|c\+\+)(\.exe)?$') {
            $mingw = Split-Path -Parent (Get-Command $compiler).Source
            foreach ($dll in 'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll') {
                $from = Join-Path $mingw $dll
                if (-not (Test-Path $from)) { throw "MinGW's $dll isn't in $mingw." }
                Copy-Item $from $stage -Force
            }
        } elseif ($env:VCToolsRedistDir) {
            $crt = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
                Select-Object -First 1
            if ($crt) { Copy-Item (Join-Path $crt.FullName '*.dll') $stage }
        }

        Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
        Copy-Licences (Join-Path $stage 'licenses')
        Set-Content -Path (Join-Path $stage 'VERSION.txt') -Value $Version -Encoding ascii
        Write-Output "Staged $stage"
    }
    'Archive' {
        $zip = Join-Path $Out "SUBstation-$Version-windows-x64.zip"
        if (Test-Path $zip) { Remove-Item -Force $zip }
        Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
        Write-Output "Archived $zip"
    }
    'Installer' {
        $iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
        if (-not $iscc) { $iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
        if (-not (Test-Path $iscc)) {
            & choco install innosetup --no-progress -y | Out-Host
            $iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
        }
        $numeric = ($Version -split '-')[0] + '.0'
        & $iscc /Qp "/DAppVersion=$Version" "/DNumericVersion=$numeric" "/DStageDir=$((Resolve-Path $stage).Path)" `
            "/DOutputDir=$((Resolve-Path $Out).Path)" "/DOutputName=SUBstation-$Version-windows-x64-setup" `
            "/DRepository=$Repository" (Join-Path $PSScriptRoot 'substation.iss')
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)." }
        Write-Output "Built $(Join-Path $Out "SUBstation-$Version-windows-x64-setup.exe")"
    }
    'Checksums' {
        foreach ($download in Get-ChildItem -Path $Out -File | Where-Object { $_.Extension -in '.zip', '.exe' }) {
            $hash = Get-Sha256 $download.FullName
            Set-Content -Path "$($download.FullName).sha256" -Value "$hash  $($download.Name)" -Encoding ascii -NoNewline
            Write-Output "$($download.Name) ($hash)"
        }
    }
}
