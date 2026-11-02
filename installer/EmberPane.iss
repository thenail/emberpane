; Per-user installer for Ember Pane: installs into %LOCALAPPDATA%\Programs
; and registers its uninstaller under HKCU, so it needs no admin rights.
;
; Built by build.ps1 -Installer, which passes:
;   /DPlatform=x64|ARM64
;   /DSourceDir=<the build's EmberPane folder>
;   /DOutputDir=<where the setup exe goes>
;
; Nothing here needs admin at install time: the app is self-contained (the
; Windows App SDK runtime is in its folder) and uses the hybrid CRT (see
; Directory.Build.props), so there are no redistributables to install.
; WebView2, which WinUI uses, is part of Windows 11 and of current Windows 10.

#ifndef Platform
  #define Platform "x64"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\" + Platform + "\Release\EmberPane"
#endif
#ifndef OutputDir
  #define OutputDir "..\build\installer"
#endif

#define AppName "Ember Pane"
#define AppExe "EmberPane.exe"
; Identifies the app to Windows across versions: never change it.
#define AppGuid "34DB0303-69EB-4CF0-9357-50E9A840CD1F"
; One version, from EmberPane.rc via the built exe.
#define AppVersion GetVersionNumbersString(SourceDir + "\" + AppExe)

#if Platform == "ARM64"
  #define Arch "arm64"
#else
  #define Arch "x64compatible"
#endif

[Setup]
AppId={{{#AppGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
VersionInfoVersion={#AppVersion}
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}

PrivilegesRequired=lowest
; With lowest privileges {autopf} is %LOCALAPPDATA%\Programs.
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
DisableReadyPage=yes

ArchitecturesAllowed={#Arch}
ArchitecturesInstallIn64BitMode={#Arch}
; The same floor as the app (WindowsTargetPlatformMinVersion): the border and
; corner attributes start at build 22000.
MinVersion=10.0.22000

; A tray app is usually running during an upgrade. StopApp below ends it at
; once; Restart Manager is the fallback, but it waits ~30 s for the app to
; answer a shutdown request it does not handle before closing it anyway.
CloseApplications=force
RestartApplications=no

WizardStyle=modern dynamic
SetupIconFile=..\src\EmberPane\Assets\EmberPane.ico
ShowLanguageDialog=auto
LanguageDetectionMethod=uilanguage

OutputDir={#OutputDir}
; x64 is the plain name, the one most people want. GitHub lists release files
; alphabetically, and digits sort before letters, so it also comes first there.
#if Platform == "ARM64"
OutputBaseFilename=EmberPane-setup-ARM64-{#AppVersion}
#else
OutputBaseFilename=EmberPane-setup-{#AppVersion}
#endif
Compression=lzma2/max
SolidCompression=yes

; The same languages as the app (src\EmberPane\Strings).
[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "da"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fi"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "ko"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "nb"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "nl"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "ptBR"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "ptPT"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "sv"; MessagesFile: "compiler:Languages\Swedish.isl"
Name: "tr"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "zhHans"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "zhHant"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"

; The app's own "RunAtLogin" string, so the installer and Settings agree.
[CustomMessages]
en.StartWithWindows=Start with Windows
da.StartWithWindows=Start med Windows
de.StartWithWindows=Mit Windows starten
es.StartWithWindows=Iniciar con Windows
fi.StartWithWindows=Käynnistä Windowsin mukana
fr.StartWithWindows=Démarrer avec Windows
it.StartWithWindows=Avvia con Windows
ja.StartWithWindows=Windows と同時に起動
ko.StartWithWindows=Windows 시작 시 실행
nb.StartWithWindows=Start med Windows
nl.StartWithWindows=Starten met Windows
pl.StartWithWindows=Uruchamiaj z systemem Windows
ptBR.StartWithWindows=Iniciar com o Windows
ptPT.StartWithWindows=Iniciar com o Windows
ru.StartWithWindows=Запускать вместе с Windows
sv.StartWithWindows=Starta med Windows
tr.StartWithWindows=Windows ile başlat
zhHans.StartWithWindows=随 Windows 启动
zhHant.StartWithWindows=隨 Windows 啟動

[Tasks]
; Offered on first install only: on an upgrade, whatever the user has since
; chosen in Settings stands.
Name: "startup"; Description: "{cm:StartWithWindows}"; Check: not IsUpgrade
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole build folder, less the linker's and the app's own by-products.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs; \
  Excludes: "*.pdb,*.lib,*.exp,\trace.txt,\diagnostics.txt,\windows-probe.txt"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; The same value Settings writes (Native\Shell.cpp), so its toggle shows it.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
  ValueName: "EmberPane"; ValueData: """{app}\{#AppExe}"""; Tasks: startup
; Removed on uninstall however it was set, by this installer or by Settings.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; \
  ValueName: "EmberPane"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Logs the app writes beside itself. Settings in %APPDATA% are kept.
Type: files; Name: "{app}\trace.txt"
Type: files; Name: "{app}\diagnostics.txt"
Type: files; Name: "{app}\windows-probe.txt"

[Code]
function IsUpgrade: Boolean;
begin
  Result := RegValueExists(HKCU,
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#AppGuid}}_is1',
    'UninstallString');
end;

// Ends the installed copy, if running, so its files can be replaced or
// removed. Asked first: a border colour outlives the process that set it, so a
// copy that is killed leaves every window coloured until it closes. The rest is
// for one that does not answer. Matched by path, not name, so a development
// build running from elsewhere is left alone.
procedure StopApp;
var
  Exe: String;
  ResultCode: Integer;
begin
  Exe := ExpandConstant('{app}\{#AppExe}');
  if FileExists(Exe) then
  begin
    Exec(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1500);
  end;
  StringChangeEx(Exe, '''', '''''', True);
  // Setup is 32-bit, and a 32-bit PowerShell cannot see a 64-bit process's
  // path: {sysnative} runs the native one.
  Exec(ExpandConstant('{sysnative}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -Command "Get-Process EmberPane -ErrorAction Ignore | ' +
    'Where-Object Path -eq ''' + Exe + ''' | Stop-Process -Force; ' +
    'Get-Process EmberPane -ErrorAction Ignore | Where-Object Path -eq ''' + Exe + ''' | Wait-Process"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopApp;
  Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    StopApp;
end;
