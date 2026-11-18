# 直播消音 / 中文直播消音 OBS 插件

中文直播敏感词消音：在本机实时识别中文语音，在声音播出前把屏蔽词那一小段换成静音或替换音效。识别全在本机进行，不上传。

内置词库是常见脏话；其他要屏蔽的词（如带货直播的违禁词、平台敏感词）在设置里自己添加，添加后立即生效。

- 作者：漠秋　反馈 QQ：549466926
- 项目地址：https://github.com/Mochall/live-speech-filter
- 当前版本：**0.10.1**（两个版本同号，用的是同一份识别和消音代码）

## 两个版本

| | OBS 插件 | 直播消音（独立程序） |
| --- | --- | --- |
| 适合 | 用 OBS 采集；需要音画同步（露脸、过滤游戏等程序里的语音） | 不用 OBS，只过滤主播麦克风和 KOOK 等语音软件 |
| 画面 | 自动把画面和未过滤的声音一起延迟，保持同步 | 不处理画面，过滤后的声音比画面晚约 1.4 秒 |
| 开播方式 | OBS 直接推流；或经虚拟摄像头＋VB-CABLE 进入抖音直播伴侣、B 站直播姬、虎牙、斗鱼 | 输出到 VB-CABLE，推流软件的麦克风选 CABLE Output |
| 安装 | 装进本机已有的 OBS Studio 28 及以上（需要管理员） | 装到用户目录，不需要管理员；Windows 10 2004 及以上 |

两个版本都随附 [VB-CABLE](https://vb-audio.com/Cable/) 原版安装包，安装时由用户选择是否打开 VB-Audio 自己的安装程序。VB-CABLE 是 VB-Audio Software 的捐赠软件（www.vb-cable.com），不属于本项目。VB-CABLE is a donationware, all participations are welcome.

使用说明：[插件](packaging/使用说明-插件.txt)、[直播消音](packaging/使用说明-直播消音.txt)。

## 验证情况

已验证（本机）：文件回放消音、KOOK 实时语音消音的本地录制、主播麦克风本地录制、抖音直播伴侣预览收到消音后的声音、一次用测试词的短时真实开播（非游戏画面）、OBS 28.0.3–33.0.1 加载插件、两个安装包在测试目录的安装和卸载。

未验证：其他电脑上的安装运行、真实游戏中的音画同步、平台观众端长时间直播。识别不是百分之百，第一次使用请先本地录一段确认效果，不要直接用于重要的正式直播。

## 仓库内容

- `patches/`：对上游 [Cloud370/obs-profanity-filter](https://github.com/Cloud370/obs-profanity-filter) `7f0d445`（v0.3.0-beta.1）的补丁 0001–0044，见 [patches/README.md](patches/README.md)。插件源码＝上游＋按编号依次 `git apply` 这些补丁（0001–0011 是普通 diff，其余是 `git format-patch` 格式）。
- `standalone/`：直播消音的源码（Qt 6），识别和消音部分直接编译打过补丁的上游源码，见 [standalone/README.md](standalone/README.md)。
- `packaging/`：两个安装程序的 Inno Setup 脚本和使用说明。
- `tools/`：组装安装包内容、生成 OBS 28 SDK、检查插件导入、单元测试的脚本。
- `docs/design/app-icon/`：程序图标源文件。

## 编译（Windows，Visual Studio 2026 生成工具）

```
git clone https://github.com/Cloud370/obs-profanity-filter upstream/obs-profanity-filter
cd upstream/obs-profanity-filter
git checkout 7f0d445
for p in ../../patches/*.patch; do git apply "$p" || break; done
cd ../..
bash tools/make-obs28-sdk.sh        # 按 OBS 28.0.3 头文件配置 work/build-obs28
cmake --build work/build-obs28 --config Release --target obs-profanity-filter
```

直播消音的编译步骤见 [standalone/README.md](standalone/README.md)。识别模型（sherpa-onnx X-ASR zh-en 160ms int8，Apache-2.0，来源 https://github.com/Gilgamesh-J/X-ASR ）不在仓库里，组装安装包前需另行下载。赞赏码图片不在仓库里，不设置 `SPONSOR_IMAGE` / `LSF_SPONSOR_IMAGE` 时编译出的程序不显示赞赏码。

## 许可

GPL-2.0（见 [LICENSE](LICENSE)），与上游相同。替换音效为 CC0（见 `standalone/data/sounds/README.md`）。第三方组件的许可随安装包放在 `licenses` 文件夹。
