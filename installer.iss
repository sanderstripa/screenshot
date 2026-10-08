#define MyAppName "Screenshot"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Sander Stripa"
#define MyAppExeName "Screenshot.exe"
[Setup]
AppId={{82B2B1BC-D0E4-48B6-9B12-F38CFBDA38C2}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\Screenshot
DefaultGroupName=Screenshot
DisableProgramGroupPage=yes
DisableWelcomePage=yes
DisableDirPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
SetupIconFile=assets\Screenshot.ico
UninstallDisplayIcon={app}\Screenshot.exe
OutputDir=dist
OutputBaseFilename=Screenshot-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UsePreviousAppDir=yes
[Files]
Source: "build\Release\Screenshot.exe"; DestDir: "{app}"; Flags: ignoreversion
[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Screenshot"; ValueData: """{app}\Screenshot.exe"""; Flags: uninsdeletevalue
[Run]
Filename: "{app}\Screenshot.exe"; Description: "Start Screenshot"; Flags: nowait
[UninstallDelete]
Type: dirifempty; Name: "{app}"
