; Geekatplay TerraForge - Inno Setup script.
;
; Built by packaging\windows\make_installer.ps1, which passes the version, the
; staged folder and the output folder in. Compile it by hand with:
;   ISCC.exe /DAppVersion=2.0.0 /DStageDir=..\..\build\package\TerraForge /DOutDir=..\..\dist terraforge.iss
;
; Two deliberate choices worth knowing about:
;
;   PrivilegesRequired=lowest - TerraForge installs for one user, into
;   %LOCALAPPDATA%\Programs. No administrator prompt, and the install folder
;   stays writable, which is where the logs and crash reports go. An install
;   under Program Files would need elevation and then fail to write its own
;   log the first time something went wrong.
;
;   The uninstaller leaves %LOCALAPPDATA%\GeekatplayTerraForge alone. That is
;   the user's preferences, autosaves and downloaded material cache; removing
;   the program should not remove their work.

#ifndef AppVersion
  #define AppVersion "2.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\build\package\TerraForge"
#endif
#ifndef OutDir
  #define OutDir "..\..\dist"
#endif

[Setup]
AppId={{7C4E2A18-0F2B-4B7E-9D4B-9C3B2F1A6E31}
AppName=Geekatplay TerraForge
AppVersion={#AppVersion}
AppVerName=Geekatplay TerraForge {#AppVersion}
AppPublisher=Geekatplay Studio
AppPublisherURL=https://www.geekatplay.com
AppSupportURL=https://github.com/GeekatplayStudio/TerraForge.art
DefaultDirName={localappdata}\Programs\TerraForge
DefaultGroupName=TerraForge
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutDir}
OutputBaseFilename=TerraForge-{#AppVersion}-Setup
SetupIconFile={#StageDir}\resources\terraforge.ico
UninstallDisplayIcon={app}\geekatplay_studio.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
MinVersion=10.0
; The licence page appears only when the repository actually has a LICENSE
; file. Inno Setup treats a missing LicenseFile as a hard error, and a
; packaging script that fails because of a file nobody has written yet is a
; packaging script people stop running.
#if FileExists(AddBackslash(StageDir) + "LICENSE")
LicenseFile={#StageDir}\LICENSE
#elif FileExists(AddBackslash(StageDir) + "LICENSE.txt")
LicenseFile={#StageDir}\LICENSE.txt
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "assocgpxt"; Description: "Open .gpxt project files with TerraForge"; GroupDescription: "File types:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\TerraForge"; Filename: "{app}\geekatplay_studio.exe"; WorkingDir: "{app}"; IconFilename: "{app}\resources\terraforge.ico"
Name: "{group}\TerraForge node reference"; Filename: "{app}\NODES.md"
Name: "{group}\Uninstall TerraForge"; Filename: "{uninstallexe}"
Name: "{autodesktop}\TerraForge"; Filename: "{app}\geekatplay_studio.exe"; WorkingDir: "{app}"; IconFilename: "{app}\resources\terraforge.ico"; Tasks: desktopicon

[Registry]
; Per-user file association, so no elevation is needed for it either.
Root: HKCU; Subkey: "Software\Classes\.gpxt"; ValueType: string; ValueName: ""; ValueData: "TerraForge.Project"; Flags: uninsdeletevalue; Tasks: assocgpxt
Root: HKCU; Subkey: "Software\Classes\TerraForge.Project"; ValueType: string; ValueName: ""; ValueData: "TerraForge project"; Flags: uninsdeletekey; Tasks: assocgpxt
Root: HKCU; Subkey: "Software\Classes\TerraForge.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\resources\terraforge.ico"; Tasks: assocgpxt
Root: HKCU; Subkey: "Software\Classes\TerraForge.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\geekatplay_studio.exe"" ""%1"""; Tasks: assocgpxt

[Run]
Filename: "{app}\geekatplay_studio.exe"; WorkingDir: "{app}"; Description: "Start TerraForge"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Written by the application beside its executable, so the uninstaller has to
; name them: without this the folder is left behind holding only logs.
Type: filesandordirs; Name: "{app}\logs"
Type: filesandordirs; Name: "{app}\__pycache__"
; The settings below normally live in %LOCALAPPDATA%\GeekatplayTerraForge, but
; a file of the same name sitting beside the executable takes precedence
; (studio/paths.cpp) - so an install folder someone has run from can hold them,
; and without these lines the uninstaller leaves the folder behind holding one
; stray JSON file.
Type: files; Name: "{app}\terraforge_prefs.json"
Type: files; Name: "{app}\geekatplay_studio_layout.ini"
Type: files; Name: "{app}\geekatplay_graph_view*.json"
Type: files; Name: "{app}\*.wal"
