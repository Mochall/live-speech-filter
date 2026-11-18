# 直播消音独立程序（LiveSpeechFilter，开发中称“轻量版”）

不用OBS的独立过滤程序：采集主播麦克风和选定的游戏外语音软件（如KOOK）的播放声音，每路各自延迟约2秒、本地识别、命中处换成静音，混合后输出到 VB-CABLE Input。直播伴侣把 CABLE Output 当麦克风用，并关闭系统声音采集。

范围、步骤和验收见 PLAN.md“轻量版：不用OBS的独立过滤程序”。不处理游戏声音，包括游戏内队伍语音。

## 代码

- 识别、词库匹配、消音和延迟缓存来自插件源码 `upstream/obs-profanity-filter/src/censor-engine.*`（上游补丁0033从插件里拆出，插件和本程序共用），另外用了 `asr-model.cpp`、`utils.cpp`、cpp-pinyin、sherpa-onnx。
- `src/audio-io.*`：WASAPI 设备采集、进程回环采集（`AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`，含子进程，需要 Windows 10 2004 或更新）、输出。
- `src/app-core.*`：每路一个 `Source`（采集 → 连续48kHz立体声时间线 → `CensorEngine` → FIFO），`Mixer` 求和并按FIFO水位做±0.3%以内的微调重采样，抵消设备时钟偏差。程序静音时进程回环不出数据，按实时补静音。
- `src/swr-resampler.*`：送识别前的重采样，和插件里 libobs `audio_resampler` 用同一个 swresample 和相同参数。
- `src/cli.cpp`：命令行原型 `lsf-lite-cli`。
- `src/pipeline.*`：窗口程序用的运行部分（麦克风和所选程序各一个 `Source`、`Mixer`、到 CABLE Input 的输出流）。
- `src/lite-checks.*`：只读检查，列出正在出声的程序、正在往某个播放设备出声的程序。
- `src/gui/`：窗口程序 `LiveSpeechFilter`（Qt 6 Widgets）。另用插件的 `companion-audio.cpp`（VB-CABLE状态、Windows默认设备是否是CABLE）。

许可：链接了 GPL-2.0 的插件代码，本目录按 GPL-2.0 发布。

## 编译

```
cmake -S standalone -B work/build-lite -G "Visual Studio 17 2022" -A x64 ^
  -DSHERPA_ONNX_ROOT=<sherpa-onnx v1.12.10 win-x64 shared 包> ^
  -DFFMPEG_ROOT=upstream/obs-profanity-filter/.deps/obs-deps-2025-07-11-x64
cmake --build work/build-lite --config Release
```

编译后程序旁边有运行所需的DLL、`data/builtin_dirty_words.txt` 和 `data/dict`。模型放在 `models/<模型目录>`，或用 `--model` 指定。

Qt 默认取插件用的 `upstream/obs-profanity-filter/.deps/obs-deps-qt6-2025-07-11-x64`（`-DLSF_QT_ROOT` 可改），编译后Qt的DLL和 `platforms/qwindows.dll`、`styles/qmodernwindowsstyle.dll` 复制到程序旁边。

## 窗口程序

`LiveSpeechFilter.exe`：
- 选麦克风、添加要过滤的程序（可同时多个：在有声音会话的程序列表里勾选，或手动输入exe名，逗号分开；默认KOOK.exe），每路显示电平、状态和已消音次数；程序没运行时自动等它启动再接上。
- 输出选 CABLE Input；消音范围、声音延迟（自动=范围+0.9秒，手动最少范围+0.7秒）、识别路数（4/2/1）、同音字匹配。
- 自定义屏蔽词（每行一个）。默认设置和插件一致：内置词库、没有自定义词、前后各0.5秒、延迟自动1.4秒、静音、同音字也消音。不读取OBS插件的词库（装这个程序的人不一定装了插件）。
- 开播前检查（每2秒刷新，只在用户点按钮时改设置）：VB-CABLE未装（“下载并安装”：从vb-audio.com找最新的Driver Pack，下载后校验 VBCABLE_Setup_x64.exe 是VB-Audio作者的有效Authenticode签名，用tar.exe解压，以管理员打开VB-Audio自己的安装程序，用户点Install Driver，之后提示重启；见 `src/gui/cable-setup.cpp`）或被禁用（“启用”）、CABLE是Windows默认播放或通信设备（“改回”，用插件的 RestoreDefaultPlayback）、别的程序也在往CABLE Input出声、直播伴侣设置（采集了系统声音或别的麦克风、没采CABLE Output、采集了程序声音）、所选程序没运行、没选来源、找不到模型。有红色问题时点开始会再确认一次。
- 关闭窗口时若正在过滤则缩到托盘，托盘菜单“退出”才停止；同一配置目录只允许运行一个。
- 设置、词库和日志在 `%APPDATA%/LiveSpeechFilter`（`config.json`、`custom_words.txt`、`pinyin_words.txt`、`logs/`，保留最近10个日志）。
- 测试用参数：`--config-dir <目录>`、`--companion-config <audioStore.json>`、`--model <目录>`。

## 命令行原型

```
lsf-lite-cli --list
lsf-lite-cli [--mic <名称片段>|default|none] [--app <exe名>]... [--out <名称片段>|none]
             [--model <目录>] [--custom <自定义词文件>] [--pinyin <补充拼音词文件>]
             [--padding <毫秒>] [--delay <秒>] [--seconds <n>] [--log <文件>] [--debug]
             [--lanes <路数>] [--lane-step <毫秒>] [--chunk <毫秒>] [--dump <目录>]
             [--delay-test <秒>] [--effect <n>] [--clip <wav> [--clip-gap <毫秒>]]
lsf-lite-cli --file <输入.wav> --write <输出.wav> [选项]
```

- `--lanes`/`--lane-step`：同一路声音并行跑几路识别，各路的模型解码块位置错开指定毫秒，任一路识别到就消音（见 PLAN.md 进展记录）。
- `--chunk`：每次送给识别模型的声音长度，默认40毫秒。`--delay-test`：测试用，延迟不受最小值限制。
- `--effect`：0 哔声，1 静音（默认），3 电报音。`--clip`：消音时放一段 48kHz 单声道 16位 wav（如 `data/sounds/dog.wav`），`--clip-gap` 是两次之间的空隙。
- `--dump`：把每路采到的原始声音写成 48kHz 立体声 f32le 文件，测试用。`--out none` 只采集不输出（例如录直播伴侣的监听声音）。
- `--file`：按实时速度读 48kHz 立体声 16位 wav、过滤后写文件，不用任何设备。

例：`lsf-lite-cli --app KOOK.exe` 过滤默认麦克风和KOOK，输出到 CABLE Input。每5秒输出一次各路状态（是否挂上、模型、电平、已消音次数、识别积压、FIFO水位、补静音时长）。

程序图标（2026-11-14 用户选定的方向A，源文件和导出脚本在 `docs/design/app-icon/`）：`resources/app.ico` 编进 exe（资源管理器、开始菜单、卸载列表），版本信息“直播消音 0.9.0”（版本号在 CMakeLists.txt 的 `project()`）；`resources/icon/` 的各尺寸 PNG 编进 Qt 资源，窗口和托盘用它们，托盘右下角叠状态点（灰未开始、橙加载中、绿过滤中、红有问题）。16–32 像素用只有声波柱的简化版，48 像素起用带蓝色底板的完整版。

识别路数默认“自动”：12个及以上逻辑核用4路，否则2路（每路约0.8核；1路会漏词，只能手动选）。

窗口程序和命令行启动时都关掉 Windows 对本进程的节能限速（EcoQoS），日志写 “Windows power throttling off”。不关时，程序在托盘、前台是别的程序，4路识别会跟不上（文件测试每个词晚3–18秒）；关掉后0次晚。优先级不变。

窗口程序的消音方式里有5种动物叫声，声音文件在程序旁的 `data/sounds/`（CC0，来源见 `standalone/data/sounds/README.md`），缺文件时按静音处理。输出设备列表只列 VB-CABLE 的 CABLE Input（显示为“虚拟声卡 CABLE Input”）；同一声卡的 CABLE In 16ch 只在没有 CABLE Input 时列出。

启动时检查：输出设备被禁用、CABLE是Windows默认播放设备、麦克风选成了CABLE。

`lsf-play-test [--normal] <48kHz立体声16位wav>`：在默认播放设备上播放测试素材。默认用Windows raw模式，绕过Nahimic等音效，便于逐位比对。

测试：
- `tools/lite-file-test.py`：`--file` 方式，不用设备。
- `tools/lite-playback-record-test.py`：文件回放，录 CABLE Output，需要临时启用 CABLE Input。
- 两者都用 `tools/mute_spans.py` 找静音区间，用 `tools/score-mutes.py` 和离线参照逐词对比。
