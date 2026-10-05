# 大富翁 4：原生 C++ 移植

目前已在这台 Apple Silicon Mac 上运行原版 v3.11，以及从 `mytbk/rich4` 重新编译的版本。
可玩的完整程序目前使用 **Boxedwine 的 x86 模拟 + 32 位 Wine**。原生引擎正在逐模块迁移，尚不能原生完成一局，也还没有可玩的 Android APK。

新 `rich4_core` 使用 C++20，不依赖 Win32、x86 汇编、32 位进程或原版 EXE。已迁移资源、存档、图片绘制、地面 tile、日期、方向、卡片/道具库存、地租及原版随机数；原版机器码仅供独立测试工具生成对照结果。

每个模块的来源、原地址、保持的行为、明确修正和验收证据见 [迁移记录](docs/migrations/README.md)。提交直接保存在本地 `main`，不推送、不创建 PR。

## 编译原生核心与 Mac 查看器

```sh
brew install cmake sdl3
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug -DRICH4_SANITIZERS=ON
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

没有 SDL3 的环境可加 `-DRICH4_BUILD_VIEWER=OFF`，核心与测试仍可单独编译。
Mac 资源查看器使用 SDL3，实际程序为 ARM64：

```sh
build/native/rich4-resource-viewer.app/Contents/MacOS/rich4-resource-viewer \
  ~/Downloads/rich4-original-game/Data.mkf 1 0
```

左右键切换帧，PageUp/PageDown 切换资源，Escape 退出。这个窗口是资源检查工具，背景中的原版菜单按钮尚不执行游戏动作。

命令行工具可以检查所有图片、导出资源/帧、读取并无损重写存档。例如：

```sh
build/native/rich4-tool inspect ~/Downloads/rich4-original-game/Data.mkf
build/native/rich4-tool frame ~/Downloads/rich4-original-game/Data.mkf 1 0 artifacts/menu.ppm
build/native/rich4-tool terrain ~/Downloads/rich4-original-game/map.mkf 0 artifacts/taiwan.ppm
```

地面导出是按 tile 索引拼接的俯视源纹理，不是游戏中的投影相机画面。

## 原版行为对照

先按下文准备 Wine 基准环境，再生成原版结果。脚本使用独立 prefix，不操作正在玩的游戏或存档。

```sh
python3 scripts/run_asset_oracle.py --native-tool build/native/rich4-tool
cmake -S . -B build/native -DRICH4_ORACLE_DIR="$PWD/.local/oracle/latest"
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

已经捕获过原版结果时，只重做全 MKF 字节比较：

```sh
python3 scripts/run_asset_oracle.py --native-tool build/native/rich4-tool --reuse-oracle
```

金标保存在本机 `.local/oracle/latest`，清单记录 EXE、资源、适配器源文件和输出的 SHA-256。
金标不进入 Git；没有商业素材时仍可运行合成输入的普通单元测试。
详细范围与局限见 [验收记录](docs/migrations/validation.md)。

## Android ARM64 核心验证

使用本机已有 Android SDK/NDK 和已启动的 ARM64 模拟器。以下命令交叉编译同一 C++ 核心，实际在 Android 上运行测试及全部资源比较：

```sh
python3 scripts/test_android_core.py --serial emulator-5554 \
  --oracle .local/oracle/latest --game ~/Downloads/rich4-original-game
```

这是原生库/命令行测试，不会安装 APK。报告位于 `artifacts/android-core/`。后续 Android 应用仍需要 SDL 窗口集成、素材导入、触摸输入、音频与生命周期处理。

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
| 原版资源包 | 7 个 MKF，共 2,673 项；原生与原版解码结果逐字节一致，包含 462 个压缩条目 |
| 声音 | 用户实际试玩确认背景音乐和角色语音正常；日志显示音频设备与 MIDI 播放调用 |
| 原版过场 | **素材不完整**：23 个 AVI 均为相同的 5,112 字节、1 秒视频 |

尚未验收其他三张地图、全部卡片道具、小游戏、破产与胜利结算、长时间稳定性。
因此这里的结论是“基础对局可运行”，不宣称完整通关验证通过。

## 后续原生化

上游并非已完成的跨平台引擎：构建仍使用 101 个 x86 汇编文件（157,977 行）、16 个 C 文件（3,185 行），
并依赖 Win32、DirectDraw、DirectSound、WinMM。新核心已经独立于这些依赖；完整游戏状态机、地图相机/物件层、AI、卡片效果、文字、音频和视频仍需继续迁移。

下一步以原版可重复输入和逐步状态快照驱动第一个完整原生回合，随后覆盖全部地图与规则。进一步路线与源码入口见 [移植调查](docs/porting.md)。

## 来源

- [mytbk/rich4](https://github.com/mytbk/rich4)，固定版本 `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`，GPL-3.0。
- [Boxedwine 26R1.0](https://github.com/danoon2/Boxedwine/releases/tag/26R1.0)，官方 Mac 应用；Wine 11.0 的 32 位文件系统。
- [Noto CJK](https://github.com/notofonts/noto-cjk)，仅在缺少本机细明体时用作替代，SIL Open Font License。

游戏资源、游戏二进制、第三方字体和运行时均保留在本机，不提交到仓库。
