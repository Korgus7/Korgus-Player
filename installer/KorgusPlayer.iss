; Inštalátor Korgus Player (Inno Setup 6). Zostavuje CI: ISCC /DAppVersion=1.0.x installer\KorgusPlayer.iss
; Inštaluje sa pre aktuálneho používateľa (bez administrátora), aby sa program vedel sám aktualizovať.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{FEEC5D6D-CC90-4B3A-9576-BDBC87D55E23}
AppName=Korgus Player
AppVersion={#AppVersion}
AppVerName=Korgus Player {#AppVersion}
AppPublisher=Korgus
AppPublisherURL=https://cami.sk/korgusplayer
AppSupportURL=https://cami.sk/korgusplayer
DefaultDirName={localappdata}\Programs\Korgus Player
DefaultGroupName=Korgus Player
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\out
OutputBaseFilename=KorgusPlayer-Setup
SetupIconFile=..\Resources\KorgusPlayer.ico
UninstallDisplayIcon={app}\Korgus Player.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes

[Languages]
Name: "sk"; MessagesFile: "compiler:Languages\Slovak.isl"
Name: "cs"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\build\KorgusPlayer_artefacts\Release\Korgus Player.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\mpv\libmpv-2.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Korgus Player"; Filename: "{app}\Korgus Player.exe"
Name: "{userdesktop}\Korgus Player"; Filename: "{app}\Korgus Player.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Korgus Player.exe"; Description: "{cm:LaunchProgram,Korgus Player}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\*.old"

[Registry]
; „Otvoriť v Korgus Player“ v Prieskumníkovi sa pri odinštalovaní odstráni
Root: HKCU; Subkey: "Software\Classes\Directory\shell\KorgusPlayer"; Flags: uninsdeletekey dontcreatekey
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\KorgusPlayer"; Flags: uninsdeletekey dontcreatekey
Root: HKCU; Subkey: "Software\Classes\*\shell\KorgusPlayer"; Flags: uninsdeletekey dontcreatekey
