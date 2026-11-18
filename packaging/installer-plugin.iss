; Plugin-only installer: installs the plugin into the OBS Studio already on this computer.
; Does not contain OBS and does not touch OBS settings, scenes or profiles.
;
; Layout under C:\ProgramData\obs-studio\plugins\obs-profanity-filter (the folder OBS reads for
; third-party plugins; the existing obs-multi-rtmp plugin on this machine uses the same place):
;   bin\64bit\  DLLs, read by OBS 28-32 and still by OBS 33 (old layout)
;   .\          the same DLLs, OBS 33 layout (OBS 34 is planned to read only this one)
;   data\       dictionary, word list, bundled model; shared by both
; OBS 33 loads one of the two copies and skips the duplicate.
;
; VB-CABLE: data\vbcable holds VB-Audio's original VBCABLE_Driver_Pack46.zip, unmodified (distribution
; with attribution allowed: https://vb-audio.com/Services/licensing.htm). When VB-CABLE is missing, the
; task "vbcable" (not ticked: only companion mode needs it) unpacks it to {tmp} and opens VB-Audio's
; own setup as administrator (the user clicks Install Driver); setup then asks for a restart. Silent
; installs never open it. The plugin's 安装 VB-CABLE button uses the same zip. Uninstall leaves VB-CABLE.
;
; The OBS folder is detected from the registry and shown on its own page, where it can be
; changed (browse or type). It is used for the version check and the duplicate-copy check;
; the plugin location itself does not depend on it. /OBSDIR="<folder>" sets it from the
; command line, also for /SILENT.
;
; Test build (never writes to ProgramData, no UAC):
;   ISCC /DTestRoot=<folder> /DTestObs=<test OBS root> installer-plugin.iss
; /DNoObsCheck (test builds only) skips the "OBS is running" check, for testing while another OBS runs.
#if defined(NoObsCheck) && !defined(TestRoot)
  #error NoObsCheck is only for test builds
#endif
#define PluginName "obs-profanity-filter"
#define Version "0.10.1"
#define Payload "..\work\installer-payload-plugin-" + Version

[Setup]
#ifdef TestRoot
AppId=LiveSpeechFilterOBSPluginInstallerTest
AppName=中文直播消音 OBS 插件（安装程序测试）
DefaultDirName={#TestRoot}\{#PluginName}
PrivilegesRequired=lowest
OutputBaseFilename=LiveSpeechFilter-OBS-Plugin-{#Version}-INSTALLER-TEST
#else
AppId=LiveSpeechFilterOBSPlugin
AppName=中文直播消音 OBS 插件
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginName}
PrivilegesRequired=admin
OutputBaseFilename=LiveSpeechFilter-OBS-Plugin-{#Version}-Windows-x64
#endif
AppVersion={#Version}
AppPublisher=漠秋
AppPublisherURL=https://github.com/Mochall/live-speech-filter
AppSupportURL=https://github.com/Mochall/live-speech-filter
DisableDirPage=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\work\installer-output
Compression=lzma2/fast
SolidCompression=yes
CloseApplications=no
Uninstallable=yes
UninstallFilesDir={app}\uninstall
UninstallDisplayName=中文直播消音 OBS 插件 {#Version}
WizardStyle=modern
InfoAfterFile=使用说明-插件.txt

[Languages]
Name: "zh"; MessagesFile: "ChineseSimplified.isl"

[Tasks]
Name: vbcable; Description: "安装 VB-CABLE 虚拟声卡（用直播伴侣、直播姬、虎牙、斗鱼等推流软件开播时需要；OBS 直接推流不需要。VB-Audio 的捐赠软件 www.vb-cable.com；Windows 询问时点“是”，再在它的窗口里点“Install Driver”，装完要重启）"; Flags: unchecked; Check: not CableInstalled

[InstallDelete]
; An older version of this plugin in the same place: remove its files first so no stale DLL or
; model stays behind. Nothing outside the plugin folder is touched.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\licenses"
Type: files; Name: "{app}\*.dll"

[Files]
Source: "使用说明-插件.txt"; DestDir: "{app}"; DestName: "使用说明.txt"; Flags: ignoreversion
Source: "{#Payload}\*"; DestDir: "{app}"; Excludes: "\data\vbcable"; Flags: ignoreversion recursesubdirs createallsubdirs
; Last entry: VB-Audio's setup is opened right after this file is copied (NeedRestart is asked while
; files are installed, before ssPostInstall; seen with the 直播消音 installer 2026-11-14).
Source: "{#Payload}\data\vbcable\VBCABLE_Driver_Pack46.zip"; DestDir: "{app}\data\vbcable"; Flags: ignoreversion; AfterInstall: CableAfterInstall

[Code]
const
  // Written by VB-Audio's setup (Pack46, checked 2026-11-14).
  CableUninstallKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VB:VBCABLE {87459874-1236-4469}';
  CableZip = 'VBCABLE_Driver_Pack46.zip';
  CableSetup = 'VBCABLE_Setup_x64.exe';

var
  ObsRoot, ObsVersion: String;
  ObsPage: TInputDirWizardPage;
  CableRestart: Boolean;

// Only the uninstall entry: VB-Audio's uninstaller leaves VBCABLE_Setup_x64.exe and the .inf in
// Program Files\VB\CABLE (seen 2026-11-14), so the folder says nothing.
function CableInstalled: Boolean;
begin
  Result := RegKeyExists(HKLM64, CableUninstallKey);
end;

// Unpacks the original pack with tar.exe (Windows 10 1803+) and opens VB-Audio's setup as
// administrator. Whatever happens there, the plugin is already installed.
procedure InstallCable;
var
  Dir: String;
  Code: Integer;
begin
  Dir := ExpandConstant('{tmp}\vbcable');
  ForceDirectories(Dir);
  WizardForm.StatusLabel.Caption := '正在打开 VB-CABLE 安装程序：Windows 询问时点“是”，再点“Install Driver”…';
  if not Exec(ExpandConstant('{sys}\tar.exe'), '-xf "' + ExpandConstant('{app}\data\vbcable\') + CableZip + '"', Dir,
              SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) or not FileExists(Dir + '\' + CableSetup) then
  begin
    Log('VB-CABLE: unpacking failed (' + IntToStr(Code) + ')');
    MsgBox('VB-CABLE 没能解压。以后可以在插件设置里点“安装 VB-CABLE…”再试。', mbError, MB_OK);
    Exit;
  end;
  if not ShellExec('runas', Dir + '\' + CableSetup, '', Dir, SW_SHOWNORMAL, ewWaitUntilTerminated, Code) then
  begin
    Log('VB-CABLE: setup not started (' + IntToStr(Code) + ')');
    if Code <> 1223 then // 1223: the user said no at the UAC prompt
      MsgBox('没能打开 VB-CABLE 安装程序（' + SysErrorMessage(Code) + '）。以后可以在插件设置里点“安装 VB-CABLE…”。',
             mbError, MB_OK);
    Exit;
  end;
  Log('VB-CABLE: setup closed (' + IntToStr(Code) + ')');
  CableRestart := CableInstalled;
  if not CableRestart then
    MsgBox('VB-CABLE 没有装上。以后可以在插件设置里点“安装 VB-CABLE…”再试；如果刚卸载过 VB-CABLE，请先重启电脑。',
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

function FindObsRoot(): String;
begin
#ifdef TestObs
  Result := '{#TestObs}';
#else
  Result := '';
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) then
    RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Result);
#endif
end;

function ObsRunning(): Boolean;
var
  Locator, Service, Found: Variant;
begin
  Result := False;
#ifdef NoObsCheck
  exit;
#endif
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Found := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
    Result := Found.Count > 0;
  except
    Result := False;
  end;
end;

function WaitForObsClosed(What: String): Boolean;
begin
  Result := True;
  while ObsRunning() do
    if SuppressibleMsgBox('OBS 正在运行。请先完全退出 OBS（包括托盘图标），再点“重试”' + What + '。' + #13#10 +
              '（任何一个 OBS 都算，包括便携版。）', mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
    begin
      Result := False;
      exit;
    end;
end;

// Accepts the OBS folder itself, or its bin\64bit folder or obs64.exe picked by mistake.
function NormalizeObsDir(Dir: String): String;
begin
  Result := Trim(Dir);
  if (Length(Result) >= 2) and (Result[1] = '"') and (Result[Length(Result)] = '"') then
    Result := Copy(Result, 2, Length(Result) - 2);
  Result := RemoveBackslashUnlessRoot(Result);
  if CompareText(ExtractFileName(Result), 'obs64.exe') = 0 then
    Result := ExtractFileDir(Result);
  if (CompareText(ExtractFileName(Result), '64bit') = 0) and
     (CompareText(ExtractFileName(ExtractFileDir(Result)), 'bin') = 0) then
    Result := ExtractFileDir(ExtractFileDir(Result));
end;

function IsPortableObs(Dir: String): Boolean;
begin
  Dir := AddBackslash(Dir);
  Result := FileExists(Dir + 'portable_mode.txt') or FileExists(Dir + 'obs_portable_mode.txt') or
            FileExists(Dir + 'portable_mode') or FileExists(Dir + 'obs_portable_mode');
end;

// Empty result: Dir is a usable OBS Studio, and ObsRoot / ObsVersion are set to it.
function CheckObsDir(Dir: String): String;
var
  Exe: String;
  Major, Minor, Rev, Build: Word;
begin
  Result := '';
  Dir := NormalizeObsDir(Dir);
  Exe := AddBackslash(Dir) + 'bin\64bit\obs64.exe';
  if (Dir = '') or not FileExists(Exe) then
  begin
    Result := '这个文件夹里没有 OBS Studio：' + #13#10 + Dir + #13#10 + #13#10 +
              '请选择 OBS Studio 的安装文件夹，也就是里面有 bin\64bit\obs64.exe 的那一层，' +
              '通常是 C:\Program Files\obs-studio。';
    exit;
  end;
  if not GetVersionComponents(Exe, Major, Minor, Rev, Build) or (Major < 28) then
  begin
    Result := '这个 OBS Studio 版本太旧（' + Dir + '）。本插件需要 OBS 28 或更新版本。';
    exit;
  end;
#ifndef TestRoot
  // Portable OBS 30 and later ignore C:\ProgramData\obs-studio\plugins. (The test build
  // installs into a test folder and is checked against portable test copies of OBS.)
  if (Major >= 30) and IsPortableObs(Dir) then
  begin
    Result := '这是便携版 OBS ' + Format('%d.%d', [Major, Minor]) + '（' + Dir + '）。' + #13#10 +
              '便携版 OBS 30 及以后不读取本安装程序放插件的位置（C:\ProgramData\obs-studio\plugins），装了也不会出现。' + #13#10 +
              '请选择安装版 OBS Studio。';
    exit;
  end;
#endif
  if FileExists(AddBackslash(Dir) + 'obs-plugins\64bit\{#PluginName}.dll') then
  begin
    Result := 'OBS 程序文件夹里已经有一份本插件：' + #13#10 +
              AddBackslash(Dir) + 'obs-plugins\64bit\{#PluginName}.dll' + #13#10 +
              '两份同时存在会冲突。请先删除那份（以及 data\obs-plugins\{#PluginName} 文件夹），再运行本安装程序。';
    exit;
  end;
  ObsRoot := Dir;
  ObsVersion := Format('%d.%d.%d', [Major, Minor, Rev]);
end;

function InitializeSetup(): Boolean;
begin
  Result := WaitForObsClosed('安装');
end;

procedure InitializeWizard();
var
  Found, Given, Value, Caption: String;
begin
  Found := FindObsRoot();
  Given := ExpandConstant('{param:OBSDIR|}');
  if Given <> '' then
  begin
    Value := Given;
    Caption := '使用命令行 /OBSDIR 指定的文件夹。';
  end
  else if (Found <> '') and (CheckObsDir(Found) = '') then
  begin
    Value := ObsRoot;
    Caption := '自动找到了下面这个 OBS Studio（版本 ' + ObsVersion + '）。' + #13#10 +
      '如果你平时用的不是它，点“浏览”另选，或直接修改路径。';
  end
  else
  begin
    Value := Found;
    Caption := '没有自动找到可用的 OBS Studio。请点“浏览”选择 OBS Studio 的安装文件夹，' +
      '通常是 C:\Program Files\obs-studio。' + #13#10 +
      '还没装 OBS 的话，请先从 obsproject.com 安装 OBS Studio 28 或更新版本。';
  end;
  // The sub-caption is passed at creation so the page sizes it for all its lines.
  ObsPage := CreateInputDirPage(wpWelcome, '选择 OBS Studio', '插件要装进哪一个 OBS Studio？', Caption, False, '');
  ObsPage.Add('OBS Studio 安装文件夹（里面有 bin\64bit\obs64.exe）：');
  ObsPage.Values[0] := Value;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Problem: String;
begin
  Result := True;
  if CurPageID = ObsPage.ID then
  begin
    Problem := CheckObsDir(ObsPage.Values[0]);
    if Problem <> '' then
    begin
      SuppressibleMsgBox(Problem, mbError, MB_OK, IDOK);
      Result := False;
    end
    else
      ObsPage.Values[0] := ObsRoot;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  // Checked again here because the OBS page is skipped in silent mode.
  Result := CheckObsDir(ObsPage.Values[0]);
  if (Result = '') and ObsRunning() then
    Result := 'OBS 正在运行。请退出 OBS 后重新运行安装程序。';
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := '装进这个 OBS Studio：' + NewLine + Space + ObsRoot + '（' + ObsVersion + '）' + NewLine + NewLine +
            '插件安装位置（OBS 读取第三方插件的固定位置）：' + NewLine + Space + ExpandConstant('{app}') + NewLine + NewLine +
            '不修改 OBS 的设置、场景和配置文件。';
  if WizardIsTaskSelected('vbcable') then
    Result := Result + NewLine + NewLine + '装完插件后打开 VB-Audio 的 VB-CABLE 安装程序（Windows 询问时点“是”，再点“Install Driver”）。'
  else
    Result := Result + '不安装驱动。';
end;

function InitializeUninstall(): Boolean;
begin
  Result := WaitForObsClosed('卸载');
end;
