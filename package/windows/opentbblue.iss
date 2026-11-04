; OpenTBBlue's Windows installer (Inno Setup 6).
;   iscc /DVERSION=0.1.0 /DSRC=path\to\opentbblue-windows-x86_64 package\windows\opentbblue.iss
; Installs for the current user (no administrator needed), with a Start menu
; entry and, if wanted, a desktop icon. The Next's SD card and the media
; repository are made on the first run in %APPDATA%\OpenTBBlue, and an
; uninstall leaves them there.
#ifndef VERSION
  #define VERSION "0.1.0"
#endif
#ifndef SRC
  #define SRC "..\..\opentbblue-windows-x86_64"
#endif

[Setup]
AppId={{6E1F3C52-7B0A-4C8E-9D21-0B7AE5C3F1B4}
AppName=OpenTBBlue
AppVersion={#VERSION}
AppVerName=OpenTBBlue {#VERSION}
AppPublisher=AmigaChrome Team
AppPublisherURL=https://github.com/DalsinAI/opentbblue
DefaultDirName={autopf}\OpenTBBlue
DefaultGroupName=OpenTBBlue
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputBaseFilename=OpenTBBlue-{#VERSION}-windows-setup
OutputDir=..\..\dist
SetupIconFile=..\icons\opentbblue.ico
UninstallDisplayIcon={app}\opentbblue.exe
LicenseFile={#SRC}\LICENSE
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Put an OpenTBBlue icon on the desktop"; GroupDescription: "Icons:"

[Files]
Source: "{#SRC}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\OpenTBBlue"; Filename: "{app}\opentbblue.exe"
Name: "{group}\OpenTBBlue files (SD card, media)"; Filename: "{userappdata}\OpenTBBlue"; Flags: foldershortcut
Name: "{group}\Uninstall OpenTBBlue"; Filename: "{uninstallexe}"
Name: "{autodesktop}\OpenTBBlue"; Filename: "{app}\opentbblue.exe"; Tasks: desktopicon

[Dirs]
Name: "{userappdata}\OpenTBBlue"

[Run]
Filename: "{app}\opentbblue.exe"; Description: "Start OpenTBBlue now (the first start downloads System/Next, about 100 MB)"; Flags: nowait postinstall skipifsilent
