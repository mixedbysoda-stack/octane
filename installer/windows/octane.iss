; FIZZFUEL Windows installer (Inno Setup 6)
; Version is passed by CI: ISCC /DAppVersion=x.y.z octane.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppName=FIZZFUEL
AppVersion={#AppVersion}
AppVerName=FIZZFUEL v{#AppVersion}
AppPublisher=Carbonated Audio
AppPublisherURL=https://carbonatedaudio.com
AppSupportURL=https://carbonatedaudio.com/faq
DefaultDirName={autopf}\Carbonated Audio\FIZZFUEL
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\..\dist\Windows\Installer
OutputBaseFilename=FIZZFUEL-v{#AppVersion}-Windows-Installer
Compression=lzma
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=FIZZFUEL v{#AppVersion}

[Files]
; VST3 bundle -> system-wide 64-bit VST3 folder (C:\Program Files\Common Files\VST3)
Source: "..\..\dist\Windows\VST3\FIZZFUEL.vst3\*"; DestDir: "{commoncf64}\VST3\FIZZFUEL.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
FinishedLabel=FIZZFUEL has been installed to your VST3 folder.%n%nRescan plugins in your DAW and look for FIZZFUEL under Carbonated Audio.
