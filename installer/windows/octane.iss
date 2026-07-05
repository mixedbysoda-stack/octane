[Setup]
AppName=OCTANE
AppVersion=1.0.0
AppVerName=OCTANE v1.0.0
AppPublisher=Carbonated Audio
AppPublisherURL=https://carbonatedaudio.com
DefaultDirName={autopf}\Carbonated Audio\OCTANE
DefaultGroupName=Carbonated Audio
OutputDir=..\..\dist\Windows\Installer
OutputBaseFilename=OCTANE-v1.0.0-Windows-Installer
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
PrivilegesRequired=admin

[Files]
; VST3
Source: "..\..\dist\Windows\VST3\OCTANE.vst3\*"; DestDir: "{commoncf64}\VST3\OCTANE.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

; Standalone
Source: "..\..\dist\Windows\Standalone\OCTANE.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\OCTANE"; Filename: "{app}\OCTANE.exe"
Name: "{group}\Uninstall OCTANE"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\OCTANE.exe"; Description: "Launch OCTANE"; Flags: nowait postinstall skipifsilent
