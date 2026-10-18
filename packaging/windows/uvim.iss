; Inno Setup script for the uVim Windows installer.
; Built by scripts/build_windows_installer.ps1, which passes:
;   /DAppVersion=0.3.0  /DSourceDir=<repo>  /DBinary=<uvim.exe>
;   /DOutputDir=<dir>
;
; Installs uvim.exe and the themes, optionally adds uvim to PATH, and
; creates a "uVim" shortcut that opens uVim in its own window: a Windows
; Terminal window with the uVim profile (default) or a PowerShell window.
; The choice is asked in the wizard and can be changed later with the
; "uVim launcher settings" shortcut.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\.."
#endif
#ifndef Binary
  #define Binary SourceDir + "\build-portable\Release\uvim.exe"
#endif
#ifndef OutputDir
  #define OutputDir SourceDir + "\dist"
#endif

#define PowerShell "{sys}\WindowsPowerShell\v1.0\powershell.exe"
#define HiddenScript "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "

[Setup]
AppId={{F28C0A50-BB63-44E8-B482-4C9DEF07C20B}
AppName=uVim
AppVersion={#AppVersion}
AppVerName=uVim {#AppVersion}
AppPublisher=uVim
AppPublisherURL=https://github.com/mattilaa/uvim
AppSupportURL=https://github.com/mattilaa/uvim/issues
DefaultDirName={autopf}\uVim
DefaultGroupName=uVim
DisableProgramGroupPage=yes
; Per-user install by default (no admin needed); the wizard offers an
; all-users install too.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#SourceDir}\LICENSE
SetupIconFile={#SourceDir}\packaging\windows\uvim.ico
UninstallDisplayIcon={app}\uvim.ico
UninstallDisplayName=uVim {#AppVersion}
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
ChangesEnvironment=yes
OutputDir={#OutputDir}
OutputBaseFilename=uvim-v{#AppVersion}-windows-x86_64-setup

[Tasks]
Name: "addtopath"; Description: "Add uvim to &PATH (run it from any terminal)"
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked

[Files]
Source: "{#Binary}"; DestDir: "{app}"; DestName: "uvim.exe"; Flags: ignoreversion
Source: "{#SourceDir}\themes\*"; DestDir: "{app}\themes"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\packaging\windows\uvim.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\packaging\windows\uvim-launch.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\packaging\windows\uvim-launcher-settings.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\uVim"; Filename: "{#PowerShell}"; \
    Parameters: "{#HiddenScript}""{app}\uvim-launch.ps1"""; \
    WorkingDir: "{%USERPROFILE}"; IconFilename: "{app}\uvim.ico"; \
    Comment: "uVim terminal editor"
Name: "{autoprograms}\uVim launcher settings"; Filename: "{#PowerShell}"; \
    Parameters: "{#HiddenScript}""{app}\uvim-launcher-settings.ps1"""; \
    IconFilename: "{app}\uvim.ico"; \
    Comment: "Choose how the uVim shortcut opens uVim"
Name: "{autodesktop}\uVim"; Filename: "{#PowerShell}"; \
    Parameters: "{#HiddenScript}""{app}\uvim-launch.ps1"""; \
    WorkingDir: "{%USERPROFILE}"; IconFilename: "{app}\uvim.ico"; \
    Comment: "uVim terminal editor"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\uVim"; ValueType: string; ValueName: "Launcher"; \
    ValueData: "{code:LauncherValue}"; Flags: uninsdeletekey

[Run]
Filename: "{#PowerShell}"; Parameters: "{#HiddenScript}""{app}\uvim-launch.ps1"""; \
    Description: "Start uVim now"; Flags: postinstall nowait skipifsilent unchecked

[Code]
var
  LauncherPage: TInputOptionWizardPage;

function TerminalInstalled: Boolean;
begin
  Result := FileExists(ExpandConstant('{localappdata}\Microsoft\WindowsApps\wt.exe'));
end;

procedure InitializeWizard;
var
  Previous: String;
begin
  LauncherPage := CreateInputOptionPage(wpSelectTasks,
    'uVim window', 'How should the uVim shortcut open uVim?',
    'uVim runs inside Windows PowerShell (with your PowerShell profile) in ' +
    'its own window. You can change this later with the ' +
    '"uVim launcher settings" shortcut.',
    True, False);
  LauncherPage.Add('Windows Terminal window with a uVim profile (recommended)');
  LauncherPage.Add('Separate Windows PowerShell window');

  Previous := GetPreviousData('Launcher', '');
  if Previous = 'powershell' then
    LauncherPage.SelectedValueIndex := 1
  else if (Previous = 'terminal') or TerminalInstalled then
    LauncherPage.SelectedValueIndex := 0
  else
    LauncherPage.SelectedValueIndex := 1;
  if not TerminalInstalled then
    { No line may start with '#' here: ISPP would read it as a directive. }
    LauncherPage.SubCaptionLabel.Caption :=
      LauncherPage.SubCaptionLabel.Caption + #13#10#13#10 +
      'Windows Terminal was not found; until it is installed ' +
      '(Microsoft Store), the shortcut uses a PowerShell window.';
end;

function LauncherValue(Param: String): String;
begin
  if LauncherPage.SelectedValueIndex = 0 then
    Result := 'terminal'
  else
    Result := 'powershell';
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Launcher', LauncherValue(''));
end;

{ Windows Terminal profile, installed as a settings fragment so the user's
  own Windows Terminal settings are never edited. }
function FragmentDir: String;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commonappdata}')
  else
    Result := ExpandConstant('{localappdata}');
  Result := Result + '\Microsoft\Windows Terminal\Fragments\uVim';
end;

function JsonEscape(Value: String): String;
begin
  Result := Value;
  StringChangeEx(Result, '\', '\\', True);
  StringChangeEx(Result, '"', '\"', True);
end;

procedure WriteTerminalProfile;
var
  App, Uvim, Json: String;
begin
  App := ExpandConstant('{app}');
  Uvim := App + '\uvim.exe';
  StringChangeEx(Uvim, '''', '''''', True);
  Json :=
    '{' + #13#10 +
    '  "profiles": [' + #13#10 +
    '    {' + #13#10 +
    '      "name": "uVim",' + #13#10 +
    '      "commandline": "' + JsonEscape(ExpandConstant('{#PowerShell}') +
      ' -NoLogo -NoExit -Command "& ''' + Uvim + '''"') + '",' + #13#10 +
    '      "icon": "' + JsonEscape(App + '\uvim.ico') + '",' + #13#10 +
    '      "startingDirectory": "%USERPROFILE%",' + #13#10 +
    '      "tabTitle": "uVim",' + #13#10 +
    '      "font": { "face": "Cascadia Mono" }' + #13#10 +
    '    }' + #13#10 +
    '  ]' + #13#10 +
    '}' + #13#10;
  ForceDirectories(FragmentDir);
  SaveStringToFile(FragmentDir + '\uvim.json', Json, False);
end;

{ PATH: the user PATH for a per-user install, the system PATH for an
  all-users install. }
function EnvironmentRoot: Integer;
begin
  if IsAdminInstallMode then
    Result := HKEY_LOCAL_MACHINE
  else
    Result := HKEY_CURRENT_USER;
end;

function EnvironmentKey: String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

procedure AddToPath(Dir: String);
var
  Path: String;
begin
  if not RegQueryStringValue(EnvironmentRoot, EnvironmentKey, 'Path', Path) then
    Path := '';
  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';') > 0 then
    exit;
  if (Path <> '') and (Path[Length(Path)] <> ';') then
    Path := Path + ';';
  RegWriteExpandStringValue(EnvironmentRoot, EnvironmentKey, 'Path', Path + Dir);
end;

procedure RemoveFromPath(Dir: String);
var
  Path: String;
  P: Integer;
begin
  if not RegQueryStringValue(EnvironmentRoot, EnvironmentKey, 'Path', Path) then
    exit;
  Path := ';' + Path + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Uppercase(Path));
  if P = 0 then
    exit;
  Delete(Path, P, Length(Dir) + 1);
  Path := Copy(Path, 2, Length(Path) - 2);
  RegWriteExpandStringValue(EnvironmentRoot, EnvironmentKey, 'Path', Path);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    WriteTerminalProfile;
    if WizardIsTaskSelected('addtopath') then
      AddToPath(ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveFromPath(ExpandConstant('{app}'));
    DelTree(FragmentDir, True, True, True);
  end;
end;
