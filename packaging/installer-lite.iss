; 直播消音 (LiveSpeechFilter) installer: the standalone program, no OBS. Installs per user into
; %LOCALAPPDATA%\Programs\LiveSpeechFilter without administrator rights. VB-Audio's original
; VBCABLE_Driver_Pack46.zip is installed unmodified in {app}\vbcable (distribution with attribution
; allowed: https://vb-audio.com/Services/licensing.htm). When VB-CABLE is missing, the task
; "vbcable" (ticked) unpacks it to {tmp} and opens VB-Audio's own setup as administrator (UAC, the
; user clicks Install Driver); when VB-CABLE is there afterwards, setup asks for a restart. Silent
; installs never open it. The program's 安装 button uses the same zip (standalone/src/gui/cable-setup.cpp).
; Uninstall leaves VB-CABLE installed (other programs may use it).
;
; Payload: python tools/build-lite-payload.py <version> (after a Release build).
;   ISCC installer-lite.iss
; Test build: own AppId, no shortcuts, never touches the Windows autostart entry:
;   ISCC /DTest installer-lite.iss     then  setup.exe /DIR=<test folder> /VERYSILENT
;
; Settings, custom words, logs and downloaded models live in %APPDATA%\LiveSpeechFilter and are
; kept on uninstall. The uninstaller removes the "start with Windows" entry only when it points
; into this installation.
#define Version "0.10.1"
#define Payload "..\work\installer-payload-lite-" + Version
#define ExeName "LiveSpeechFilter.exe"

[Setup]
#ifdef Test
AppId=LiveSpeechFilterInstallerTest
AppName=直播消音（安装程序测试）
OutputBaseFilename=LiveSpeechFilter-{#Version}-INSTALLER-TEST
#else
AppId=LiveSpeechFilter
AppName=直播消音
OutputBaseFilename=LiveSpeechFilter-{#Version}-Windows-x64
#endif
AppVersion={#Version}
AppVerName=直播消音 {#Version}
AppPublisher=漠秋
AppPublisherURL=https://github.com/Mochall/live-speech-filter
AppSupportURL=https://github.com/Mochall/live-speech-filter
VersionInfoVersion={#Version}
VersionInfoDescription=直播消音 安装程序
DefaultDirName={localappdata}\Programs\LiveSpeechFilter
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
DisableWelcomePage=no
DisableDirPage=auto
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir=..\work\installer-output
Compression=lzma2/max
SolidCompression=yes
; The program creates this mutex; setup and uninstall ask to quit it first (right-click the tray
; icon, 退出) instead of killing it, so it can disable CABLE Input on the way out.
AppMutex=LiveSpeechFilter.Running
CloseApplications=no
Uninstallable=yes
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName=直播消音 {#Version}
SetupIconFile=..\standalone\resources\app.ico
WizardStyle=modern
InfoAfterFile=使用说明-直播消音.txt

[Languages]
Name: "zh"; MessagesFile: "ChineseSimplified.isl"

[Messages]
zh.SetupAppRunningError=直播消音正在运行。%n%n请右键任务栏右下角的托盘图标，选“退出”，再点“确定”继续。
zh.UninstallAppRunningError=直播消音正在运行。%n%n请右键任务栏右下角的托盘图标，选“退出”，再点“确定”继续。

[Tasks]
#ifndef Test
Name: desktopicon; Description: "在桌面创建快捷方式"; Flags: unchecked
#endif
Name: vbcable; Description: "安装 VB-CABLE 虚拟声卡（必需。VB-Audio 的捐赠软件 www.vb-cable.com；Windows 询问时点“是”，再在它的窗口里点“Install Driver”，装完要重启）"; Check: not CableInstalled

[InstallDelete]
; An older version in the same folder: remove its program files first so no stale DLL, model or
; plugin stays behind. User settings are elsewhere (%APPDATA%) and are not touched.
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\models"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\vbcable"
Type: files; Name: "{app}\*.dll"

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Excludes: "\vbcable"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "使用说明-直播消音.txt"; DestDir: "{app}"; DestName: "使用说明.txt"; Flags: ignoreversion
; Last entry: VB-Audio's setup is opened right after this file is copied. Setup asks NeedRestart while
; the files are installed, before ssPostInstall (seen 2026-11-14), so ssPostInstall would be too late.
Source: "{#Payload}\vbcable\VBCABLE_Driver_Pack46.zip"; DestDir: "{app}\vbcable"; Flags: ignoreversion; AfterInstall: CableAfterInstall

[Icons]
#ifndef Test
Name: "{autoprograms}\直播消音"; Filename: "{app}\{#ExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\直播消音"; Filename: "{app}\{#ExeName}"; WorkingDir: "{app}"; Tasks: desktopicon
#endif

[Run]
Filename: "{app}\{#ExeName}"; Description: "启动直播消音"; Flags: nowait postinstall skipifsilent

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunName = 'LiveSpeechFilter';
  // Written by VB-Audio's setup (Pack46, checked 2026-11-14).
  CableUninstallKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VB:VBCABLE {87459874-1236-4469}';
  CableZip = 'VBCABLE_Driver_Pack46.zip';
  CableSetup = 'VBCABLE_Setup_x64.exe';

var
  CableRestart: Boolean;

// Only the uninstall entry: VB-Audio's uninstaller leaves VBCABLE_Setup_x64.exe and the .inf in
// Program Files\VB\CABLE (seen 2026-11-14), so the folder says nothing.
function CableInstalled: Boolean;
begin
  Result := RegKeyExists(HKLM64, CableUninstallKey);
end;

// Unpacks the original pack with tar.exe (Windows 10 1803+) and opens VB-Audio's setup as
// administrator. Whatever happens there, our own installation is already complete.
procedure InstallCable;
var
  Dir: String;
  Code: Integer;
begin
  Dir := ExpandConstant('{tmp}\vbcable');
  ForceDirectories(Dir);
  WizardForm.StatusLabel.Caption := '正在打开 VB-CABLE 安装程序：Windows 询问时点“是”，再点“Install Driver”…';
  if not Exec(ExpandConstant('{sys}\tar.exe'), '-xf "' + ExpandConstant('{app}\vbcable\') + CableZip + '"', Dir,
              SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) or not FileExists(Dir + '\' + CableSetup) then
  begin
    Log('VB-CABLE: unpacking failed (' + IntToStr(Code) + ')');
    MsgBox('VB-CABLE 没能解压。装好后可以在直播消音的“开播前检查”里点“安装”再试。', mbError, MB_OK);
    Exit;
  end;
  if not ShellExec('runas', Dir + '\' + CableSetup, '', Dir, SW_SHOWNORMAL, ewWaitUntilTerminated, Code) then
  begin
    Log('VB-CABLE: setup not started (' + IntToStr(Code) + ')');
    if Code <> 1223 then // 1223: the user said no at the UAC prompt
      MsgBox('没能打开 VB-CABLE 安装程序（' + SysErrorMessage(Code) + '）。可以稍后在直播消音的“开播前检查”里点“安装”。',
             mbError, MB_OK);
    Exit;
  end;
  Log('VB-CABLE: setup closed (' + IntToStr(Code) + ')');
  CableRestart := CableInstalled;
  if not CableRestart then
    MsgBox('VB-CABLE 没有装上。可以稍后在直播消音的“开播前检查”里点“安装”再试；如果刚卸载过 VB-CABLE，请先重启电脑。',
           mbInformation, MB_OK);
end;

procedure CableAfterInstall;
begin
  if WizardIsTaskSelected('vbcable') then
    if WizardSilent then
      Log('VB-CABLE: not opened in a silent installation')
    else
      InstallCable;
end;

// VB-Audio asks for a restart after installing the driver.
function NeedRestart: Boolean;
begin
  Result := CableRestart;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Value: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
#ifndef Test
  // "开机后自动缩在托盘里": the program writes "<exe>" --tray here. Remove it only when it is ours.
  if RegQueryStringValue(HKCU, RunKey, RunName, Value) then
    if Pos(Lowercase(ExpandConstant('{app}\{#ExeName}')), Lowercase(Value)) > 0 then
      RegDeleteValue(HKCU, RunKey, RunName);
#endif
end;
