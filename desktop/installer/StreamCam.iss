#ifndef AppVersion
  #define AppVersion "0.2.2-preview"
#endif
#ifndef PackageDir
  #define PackageDir "..\..\dist\StreamCam-preview"
#endif
#ifndef OutputDirPath
  #define OutputDirPath "..\..\dist"
#endif

[Setup]
AppId={{D8C48776-49C9-4B11-986A-21CB99676586}
AppName=StreamCam
AppVersion={#AppVersion}
VersionInfoVersion=0.2.2.0
AppPublisher=StreamCam
AppPublisherURL=https://github.com/julianzanutti/StreamCam
AppSupportURL=https://github.com/julianzanutti/StreamCam/issues
DefaultDirName={localappdata}\Programs\StreamCam
DefaultGroupName=StreamCam
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDirPath}
OutputBaseFilename=StreamCam-{#AppVersion}-windows-x64-setup
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\..\LICENSE
UninstallDisplayIcon={app}\StreamCam.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"
Name: "obsplugin"; Description: "Instalar la fuente StreamCam para OBS (cerrá OBS antes de continuar)"; GroupDescription: "Integración opcional con OBS Studio 33.0.1 de 64 bits:"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\obs-plugin\streamcam-source.dll"; DestDir: "{userappdata}\obs-studio\plugins\streamcam-source\bin\64bit"; Tasks: obsplugin; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\StreamCam\StreamCam"; Filename: "{app}\StreamCam.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\StreamCam\Cómo empezar"; Filename: "{app}\EMPEZAR.txt"
Name: "{autodesktop}\StreamCam"; Filename: "{app}\StreamCam.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\StreamCam.exe"; Description: "Abrir StreamCam"; Flags: nowait postinstall skipifsilent

; Identity and trusted-device data are deliberately outside {app} and preserved.
