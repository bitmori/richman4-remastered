# 大富翁 4：原生 C++ 移植

**当前暂停开发（2026-10-05）。** Mac ARM64 原生原型已能运行对局，用户反馈流畅度明显改善；但 UI 未完整复刻、细节缺失，整体体验尚未验收。当前不能标为完整移植完成。

完整交接见 **[项目状态、UI 缺口与暂停记录](docs/STATUS.md)**。43 项代码检查通过；19 场回放虽各自通过，汇总因缺一张卡的覆盖而失败；扩展后的 20 场在用户要求暂停时中断。规则和操作验证不等于 UI 还原验证。

双击 **`Play Rich4 Native.command`** 可构建并查看当前原生原型。原版 v3.11 的 Wine 环境保留作参考；Android 当前仍是早期核心测试，没有可玩的 APK。暂停期间不自动继续实现或重跑测试。

新 `rich4_core` 使用 C++20，不依赖 Win32、x86 汇编、32 位进程或原版 EXE。已迁移资源、世界/存档、场景、回合、事件、卡片/道具效果、股票、商店、拍卖、彩票、假日与原版随机数；原版机器码仅供独立测试工具生成对照结果。

每个模块的来源、原地址、保持的行为、明确修正和验收证据见 [迁移记录](docs/migrations/README.md)。提交直接保存在本地 `main`，不推送、不创建 PR。

## 编译原生核心与 Mac 查看器

```sh
brew install cmake sdl3 freetype fluid-synth
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug -DRICH4_SANITIZERS=ON
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

只编译核心的环境可加 `-DRICH4_BUILD_VIEWER=OFF -DRICH4_BUILD_GAME=OFF`，核心与测试仍可单独编译。
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

## 不占用桌面的玩法验证

已经配置 Wine 基准环境并编译原生工具后，可以自动测试真实原版的读档、掷骰、走格、电脑回合和回到玩家操作状态：

```sh
python3 scripts/run_playability.py --target wine --slot 1 --turns 3 --timeout 300
```

测试在独立的游戏副本和无窗口 Wine 进程中运行，不操作桌面鼠标/键盘。它保留事件、游戏内存快照和原版存档，再用原生 C++ 逐字节回读并交叉核对状态。报告在 `artifacts/playability/`；原有游戏、存档和配置不会被覆盖。

原生版本使用同一SDL按钮输入路径，无需桌面自动化：

```sh
python3 scripts/run_native_playability.py --turns 30 --seed 42
# 或 python3 scripts/run_playability.py --target native --turns 30
```

它明确拒绝未实现事件、没有移动、存档/随机状态不一致等情况。前后画面、逐事件/输入记录和原文件保护校验位于 `artifacts/native-playability/`。详细范围见[原生应用记录](docs/migrations/018-native-app.md)；Wine检查见[原版玩法验证](docs/migrations/009-playability-hooks.md)。

## Android ARM64 核心验证

使用本机已有 Android SDK/NDK 和已启动的 ARM64 模拟器。以下命令交叉编译同一 C++ 核心，实际在 Android 上运行测试及全部资源比较：

```sh
python3 scripts/test_android_core.py --serial emulator-5554 \
  --oracle .local/oracle/latest --game ~/Downloads/rich4-original-game
```

这是原生库/命令行测试，不会安装 APK。报告位于 `artifacts/android-core/`。后续 Android 应用仍需要 SDL 窗口集成、素材导入、触摸输入、音频与生命周期处理。

## 运行当前原型与原版参考

原生版本双击 **`Play Rich4 Native.command`**，或执行 `./scripts/run_native_mac.sh`。新存档在 `~/Library/Application Support/Rich4Native/saves`，不会覆盖原版。空格/Enter掷骰，F5保存，F9读档；顶部按钮可用卡片、道具、股票等功能。

原版参考程序仍可双击 **`Play Rich4.command`**。也可以在终端执行：

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

## 原版 Wine 基准实测（2026-10-05）

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

这一组 Wine 基准只验证列出的基础流程，没有覆盖其他地图、全部卡片道具、小游戏或完整结局。Mac 原生版本的独立验收范围见上方和[验收记录](docs/migrations/validation.md)，不能把两套程序的证据混用。

## 后续原生化

上游并非已完成的跨平台引擎：构建仍使用 101 个 x86 汇编文件（157,977 行）、16 个 C 文件（3,185 行），
并依赖 Win32、DirectDraw、DirectSound、WinMM。新核心与SDL3应用已经独立于这些依赖。迁移的规则、原地址、差分证据、保留的历史缺陷和未验证范围，逐项保存在 `docs/migrations/`；过场视频和Android应用层尚需后续完成。

恢复工作后应优先建立原版与原生的页面/交互对照，补齐小地图、右侧分页、原图标工具栏和各功能界面，保持已获得的原生流畅度；具体缺口和验收边界见 [交接记录](docs/STATUS.md)。[移植调查](docs/porting.md) 保留最初的源码与路线研究，不是当前完成度声明。

## 来源

- [mytbk/rich4](https://github.com/mytbk/rich4)，固定版本 `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`，GPL-3.0。
- [Boxedwine 26R1.0](https://github.com/danoon2/Boxedwine/releases/tag/26R1.0)，官方 Mac 应用；Wine 11.0 的 32 位文件系统。
- [Noto CJK](https://github.com/notofonts/noto-cjk)，仅在缺少本机细明体时用作替代，SIL Open Font License。

游戏资源、游戏二进制、第三方字体和运行时均保留在本机，不提交到仓库。
