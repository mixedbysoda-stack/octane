[Setup]
AppName=FIZZFUEL
AppVersion=1.0.0
AppVerName=FIZZFUEL v1.0.0
AppPublisher=Carbonated Audio
AppPublisherURL=https://carbonatedaudio.com
DefaultDirName={autopf}\Carbonated Audio\FIZZFUEL
DefaultGroupName=Carbonated Audio
OutputDir=..\..\dist\Windows\Installer
OutputBaseFilename=FIZZFUEL-v1.0.0-Windows-Installer
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
PrivilegesRequired=admin

[Files]
; VST3
Source: "..\..\dist\Windows\VST3\FIZZFUEL.vst3\*"; DestDir: "{commoncf64}\VST3\FIZZFUEL.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

; Standalone
Source: "..\..\dist\Windows\Standalone\FIZZFUEL.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\FIZZFUEL"; Filename: "{app}\FIZZFUEL.exe"
Name: "{group}\Uninstall FIZZFUEL"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\FIZZFUEL.exe"; Description: "Launch FIZZFUEL"; Flags: nowait postinstall skipifsilent
