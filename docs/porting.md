# 移植调查记录

## 当前结论

Apple Silicon 上的基础玩法已经通过 Boxedwine + Wine 11.0 32 位运行验证。
这个结果同时提供了原版与重新编译版本的行为对照；尚不能推出 ARM64 原生引擎或 Android 运行结果。

上游 README 明确说明当前构建输出是 Windows EXE，并提醒 Wine 新 WoW64 模式存在启动问题。
本次选择真正的 32 位 Wine 文件系统，由 Boxedwine 模拟 x86，避开该特定路径。
Boxedwine 本身有 ARM64 Mac 可执行切片，游戏 EXE 仍是 x86。

## 源码入口（固定上游版本）

源码在 `.local/upstream`，不修改上游即可复现当前构建。

| 模块 | 入口 | 移植影响 |
| --- | --- | --- |
| 启动与消息循环 | `asm/rich4_main.asm`、`rich4_initialize.asm` | Win32 窗口、消息、键盘钩子；显示模式固定 640×480×16 |
| 图片资源 | `rich4_libmkf.c`、`rich4_libmkf_decompress.c`、`mkf.h` | 解压器可优先独立；资源内偏移与运行时指针需要分离 |
| 图形和字体 | `rich4_drawing.c`、`rich4_text_drawing.asm` | DirectDraw 表面、GDI DrawTextA；字体名 Big5 编码的“細明體”，charset 136 |
| 游戏逻辑 | `rich4_player_core_actions.asm`、`rich4_ai_*`、`rich4_card_*`、`rich4_tool_*` | 核心仍为 x86；需要逐模块转换与行为对照 |
| 持久化 | `rich4_save_files.asm`、`rich4_config_file.c`、`docs/saveload.txt` | 当前格式为固定布局；原生实现应显式序列化，保留原版存档兼容性 |
| 音频与过场 | `rich4_direct_sound_init.asm`、`rich4_media_music.asm`、`rich4_media_video.asm` | DirectSound、WinMM/MCI、MIDI、AVI；需要相应平台后端 |

`csrc` 中的 C 文件不是一套可独立构建的完整游戏。可用构建入口是 `asm/Makefile`。
`graph_st` 等结构直接包含指针，并将文件里的 32 位值改写为指针；在 64 位环境中直接照搬会改变布局。
因此原生化不能只替换 `windows.h`。

## Big5 和字体问题

原版字符串含 Big5 多字节文本，字节长度还参与绘制与排版；不能原地换成 UTF-8。
字体缺失时 Wine 使用了不适合 Big5 的字体，导致选人界面乱码。
安装 CJK 字体后中文恢复，但 Noto Sans 笔画与原版不符。
最终改为本机 Office 自带 `mingliu.ttc`，关闭 FontSmoothing，并显式使用最近邻缩放。

以后原生引擎可采用：原始素材/存档保持既有格式 → 解析边界从 Big5 转为 Unicode → 引擎内部 UTF-8。
字符串长度、换行、字体度量均需要单独验证，不能只做全局字符编码替换。

## 两条 Android 路线

1. **兼容运行验证**：在 ARM64 Android 使用 Windows 兼容环境与 x86 翻译层，例如 [Winlator](https://github.com/brunodev85/winlator)。
   保留原版逻辑的机会较高，但需要实测 32 位游戏、触摸输入、声音和设备图形驱动；Mac 的结果不能替代手机验证。
2. **原生重写**：把资源加载、图像、音频、输入与文件访问抽象为跨平台层，然后逐模块替换汇编。
   Mac ARM64 可作为第一开发平台，Android 共用引擎。先实现并验证资源解码，再扩展到完整回合，最后覆盖所有规则。

当前没有创建只显示静态素材的 APK，也没有把主菜单原型称为完整移植。
下一轮适合以已经能玩的 Wine 版本为对照，明确选择兼容封装还是持续原生重写。

## 剩余验收

- 原版与重编译版本各跑其他三张地图，比较关键规则、卡片与道具。
- 用户已确认背景音乐与角色语音正常；暂停、恢复及全部音效尚未逐项验证。
- 长局、破产、结算与存档恢复；上游 C 配置读取与原版存在日期边界差异，当前测试日期为 2026-10-05。
- 完整过场必须补齐真实素材；当前 23 个 AVI 的 SHA-256 完全相同，无法从这些占位文件还原视频。

本次安装过 Homebrew `mingw-w64`、`nasm` 及依赖；Boxedwine 放在仓库 `.local`，游戏副本与注册表位于其应用沙盒内。
没有修改 Downloads 中的原始文件，没有更改 macOS 的安全设置，也没有启动 Android 移植实验。
