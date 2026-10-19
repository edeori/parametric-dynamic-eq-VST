#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef RepoRoot
  #error RepoRoot is required
#endif
[Setup]
AppId={{FE779610-2D0F-4C7B-924B-145F49DD9718}
AppName=ParaDyn EQ
AppVersion={#AppVersion}
AppPublisher=Moth Production
AppPublisherURL=https://mothproduction.hu
DefaultDirName={autopf}\Moth Production\ParaDyn EQ
DefaultGroupName=Moth Production
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\ParaDyn EQ.exe

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Files]
Source: "{#RepoRoot}\build\ci\Release\VST3\ParaDyn EQ.vst3\*"; DestDir: "{commoncf64}\VST3\ParaDyn EQ.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#RepoRoot}\build\ci\Release\Standalone\ParaDyn EQ.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\ParaDyn EQ"; Filename: "{app}\ParaDyn EQ.exe"; Components: standalone
