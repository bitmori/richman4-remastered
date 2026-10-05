# 大富翁 4：Mac 运行验证与移植调查

目前已在这台 Apple Silicon Mac 上运行原版 v3.11，以及从 `mytbk/rich4` 重新编译的版本。
运行方式是 **Boxedwine 的 x86 模拟 + 32 位 Wine**。这不是原生 macOS 游戏，也还不是 Android 应用。

## 直接玩

双击仓库里的 **`Play Rich4.command`**，运行原版。也可以在终端执行：

```sh
./scripts/run_mac.sh rich4.exe
```

从源码编译的版本：

```sh
./scripts/run_mac.sh rich4_mscrt.exe
```

一次只开一个游戏进程。画面保持 640×480，使用最近邻缩放，关闭字体平滑。
Big5 保持不变。已从本机 Microsoft Word 的字体目录安装 `mingliu.ttc`，让 Wine 使用游戏要求的细明体。
该字体只复制到本机 Wine 目录，没有纳入 Git。

## 重建环境

在 macOS 上安装编译依赖后，按顺序执行。运行安装脚本前关闭 Boxedwine。

```sh
brew install mingw-w64 nasm
./scripts/build_upstream.sh
python3 scripts/setup_mac.py ~/Downloads/rich4-original-game
./scripts/run_mac.sh rich4.exe
```

脚本固定上游源码版本及运行时校验和。默认查找已安装的 Word、Excel、PowerPoint 中的细明体，
也可使用 `--font /path/to/mingliu.ttc` 指定本机字体。找不到时采用开源 Noto Serif CJK TC；替代字体的字形不与原版完全相同。

Boxedwine 官方 Mac 应用启用了沙盒，因此游戏、字体、Wine 文件系统均放在其容器内；不需要关闭系统或应用沙盒。
原始 Downloads 文件夹保持不变。安装脚本不覆盖已有游戏素材或存档。

## 存档位置

```text
~/Library/Containers/org.boxedwine/Data/Library/Application Support/Boxedwine/Rich4/game/
```

`SAVE0.DAT` 是自动存档，`SAVE1.DAT` 等是手动存档。两种 EXE 共用该目录。
当前槽位 1 是验证用对局：台湾地图，小丹尼，已买下台中土地，现金 98,000。

日志在 `artifacts/mac-runtime.log`；重新运行会覆盖上次日志。
只读素材检查：`python3 scripts/audit_assets.py`。

## 实测范围（2026-10-05）

| 项目 | 结果 |
| --- | --- |
| 原版 EXE 版本匹配 | SHA-256 与上游针对的 v3.11 完全一致 |
| 上游源码构建 | MinGW + NASM 成功生成 `rich4_mscrt.exe` |
| 两种 EXE 启动 | 均显示主菜单 |
| 重编译版本基础玩法 | 选人、台湾地图、掷骰走格、银行、买地、电脑回合通过 |
| 中文字体 | Big5 + 本机细明体显示正常，恢复接近原版的细笔画 |
| 存取档 | 手动存档成功；重启后原版成功读取重编译版本存档，现金与地图状态一致 |
| 原版资源包 | 7 个 MKF，共 2,673 项，索引和边界检查通过；不是逐项解码验证 |
| 声音 | 用户实际试玩确认背景音乐和角色语音正常；日志显示音频设备与 MIDI 播放调用 |
| 原版过场 | **素材不完整**：23 个 AVI 均为相同的 5,112 字节、1 秒视频 |

尚未验收其他三张地图、全部卡片道具、小游戏、破产与胜利结算、长时间稳定性。
因此这里的结论是“基础对局可运行”，不宣称完整通关验证通过。

## Android 与原生化

按当前约定，先确保 Mac 上的 Wine 对照版本正常，Android 实验暂不启动。
上游并非已完成的跨平台引擎：构建仍使用 101 个 x86 汇编文件（157,977 行）、16 个 C 文件（3,185 行），
并依赖 Win32、DirectDraw、DirectSound、WinMM。不能换一个 NDK 编译器就得到 ARM64 APK。

进一步路线与源码入口见 [移植调查](docs/porting.md)。

## 来源

- [mytbk/rich4](https://github.com/mytbk/rich4)，固定版本 `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`，GPL-3.0。
- [Boxedwine 26R1.0](https://github.com/danoon2/Boxedwine/releases/tag/26R1.0)，官方 Mac 应用；Wine 11.0 的 32 位文件系统。
- [Noto CJK](https://github.com/notofonts/noto-cjk)，仅在缺少本机细明体时用作替代，SIL Open Font License。

游戏资源、游戏二进制、第三方字体和运行时均保留在本机，不提交到仓库。
