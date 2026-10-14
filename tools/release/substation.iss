; SUBstation's installer (Inno Setup 6), built by tools/release/package-windows.ps1 -Step Installer from the
; staged folder (docs/releasing.md):
;
;   ISCC /DAppVersion=0.3.0 /DNumericVersion=0.3.0.0 /DStageDir=...\dist\SUBstation /DOutputDir=...\dist
;        /DOutputName=SUBstation-0.3.0-windows-x64-setup tools\release\substation.iss
;
; A per-user install (no administrator): %LOCALAPPDATA%\Programs\SUBstation, a Start menu entry, an optional
; desktop shortcut, .gilproj projects opening in SUBstation, an uninstaller (Settings > Apps). The folder is the
; user's, so the updater can update it: it runs the next version's installer silently
; (/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CLOSEAPPLICATIONS), which installs over this one in the same
; folder with the same choices. The user's projects, presets and settings are never touched.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\dist\SUBstation"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
#ifndef OutputName
  #define OutputName "SUBstation-setup"
#endif
#ifndef Repository
  #define Repository "OpenMusicStuff/SUBstation"
#endif

[Setup]
; (Never change the AppId: it is how a new version finds the installation it updates.)
AppId={{4E88C426-BA86-4752-A5BA-6EA4DEEAD69F}
AppName=SUBstation
AppVersion={#AppVersion}
AppVerName=SUBstation {#AppVersion}
AppPublisher=The SUBstation contributors
AppPublisherURL=https://github.com/{#Repository}
AppSupportURL=https://github.com/{#Repository}/issues
AppUpdatesURL=https://github.com/{#Repository}/releases
VersionInfoVersion={#NumericVersion}
VersionInfoProductTextVersion={#AppVersion}
DefaultDirName={autopf}\SUBstation
DefaultGroupName=SUBstation
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile={#StageDir}\licenses\README.txt
UninstallDisplayIcon={app}\substation.exe
UninstallDisplayName=SUBstation
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "Open SUBstation projects (.gilproj) in SUBstation"; GroupDescription: "File types:"

[InstallDelete]
; Qt's plug-ins and QML modules come whole with each version: what an older one had and this one hasn't goes.
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\licenses"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\SUBstation"; Filename: "{app}\substation.exe"
Name: "{autodesktop}\SUBstation"; Filename: "{app}\substation.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.gilproj\OpenWithProgids"; ValueType: string; ValueName: "SUBstation.Project"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\SUBstation.Project"; ValueType: string; ValueName: ""; ValueData: "SUBstation Project"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\SUBstation.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\substation.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\SUBstation.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\substation.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\substation.exe"; Description: "{cm:LaunchProgram,SUBstation}"; Flags: nowait postinstall skipifsilent
