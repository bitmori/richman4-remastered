# 009：无窗口玩法验证与状态采集

本阶段新增验证设施，没有迁移完整游戏主循环。原生 C++ 核心仍不能开始或继续一局；`--target native` 明确返回 3（unavailable），不能用模块测试通过来代替可玩性验收。

## 两个独立边界

`scripts/run_playability.py --target wine` 在隔离的真实原版进程中操作菜单、存档槽与骰子按钮。掷骰、走格、电脑操作和日期推进都由原版执行。每个回合必须分别观察到骰子动作开始、玩家位置变化、其他玩家的回合、日期变化及回到原玩家等待输入，才算成功。轮询检测的是游戏状态，不是画面中的骰子动画识别。

`rich4-state-check` 使用平台无关的存档模块读取前后快照，要求重新编码后每个字节完全一致。它导出日期、玩家、现金/存款/贷款、位置、库存和土地字段的变化。宿主检查器再把日期、地图、阶段、当前玩家及活动玩家的 8 个字段，与钩子从游戏内存独立采集的 JSON 比较。

这证明指定流程在 Wine 基准版中运行、指定字段可由原生代码正确读取。它不证明完整原生回合、AI 或事件规则与原版一致。报告明确将 `native_gameplay_verified`、`original_turn_rules_verified`、`migrated_function_replay_performed` 保持为 false。

## 隔离与无窗口配置

每次运行将既有 `Rich4/root` 和 `Rich4/game` 复制到 Boxedwine 容器下唯一的 `Rich4-playability/<run-id>/`。正式游戏目录仅用作读取来源。测试前后校验其 EXE、存档与配置哈希；所有输入和保存动作只作用于副本。源 EXE 必须匹配 v3.11 的 SHA-256，不能把固定地址用于未知版本。

宿主设置 `SDL_VIDEODRIVER=dummy`、`SDL_RENDER_DRIVER=software`、`SDL_AUDIODRIVER=dummy` 和 `SDL_MAC_BACKGROUND_APP=1`；Boxedwine 使用 `-novideo`。不使用桌面截图、真实鼠标/键盘输入或 Cocoa 后备窗口。PCM 在 dummy 设备中正常计时，避免关闭整个音频设备影响语音结束通知；这不验证听感。

Mac 的 MIDI 合成器会绕过 SDL 使用 CoreAudio，故仅设 dummy 不足以让背景音乐静音。脚本只在私有副本将 72 字节 `RICH4.CFG` 的 byte 2（BGM 设置）改为 0，保留 byte 3（音效/语音）和所有其它字节。原 EXE 的配置读取函数 `0x411e8f` 读取 16+56 字节；存档加载仅恢复配置中的日期，不覆盖 BGM。钩子从 `0x49715a` 采集实际音乐设置，前后快照与完成报告都必须为 0。正式游戏的设置保持不变。

Wine 11 初始化显示时还会探测 Vulkan/OpenGL，需要两个仅影响测试环境的兼容处理：

- 在私有 prefix 放置空的 `lib/libvulkan.so.1`，遮蔽共享只读 Wine ZIP 中的 Vulkan 桥；不修改 ZIP 或正式 prefix。
- 传入 guest 环境 `WINE_D3D_CONFIG=renderer=no3d`，并沿用已配置的 `DirectDrawRenderer=gdi`，避免 OpenGL 能力探测创建窗口。

底层诊断源码为 `tools/playability/headless_gdi_probe.c`、`headless_ddraw_probe.c`，复现参数在同目录 `headless_probe_config.json`（声明式配置，不是独立执行器）。本机已实际核对 GDI 的 12,288 个像素及 DirectDraw 640×480×16 primary surface 的 307,200 个像素，均零差异。GDI probe 也确认 guest 鼠标、键盘消息各到达一次。这些合成探测不能替代真实游戏流程。

## 操作与快照

`tools/playability` 下的 Win32 代码只供原版测试适配器使用，不链接到 `rich4_core` 或 Android 程序。菜单/骰子通过真实窗口消息驱动，不直接改写骰子结果、玩家位置或回合状态。银行、乐透购票等已识别对话框通过原来的右键取消路径处理，等待原版对白和计时器自然退出。未知 callback 持续 15 秒会捕获诊断并失败，不跳过其游戏逻辑。

启动器用 `SetWindowsHookExA(WH_GETMESSAGE, ..., child.dwThreadId)` 让 Wine 消息队列将观察 DLL 加载到本次子进程。它只针对该 guest 线程，不设置全局或宿主 hook，不修改原 EXE。DLL 仅在已识别的游戏映像中启动观察线程。初次尝试的 `WriteProcessMemory` 被当前 Wine/模拟器组合以错误 5 拒绝；改用消息 hook 后真实流程通过。还修复了 DLL 在 launcher 中错误读取游戏固定地址的问题。

前后存档调用原版 `0x402fd1`，通过自定义窗口消息在游戏主线程执行。槽位 8/9 仅用作私有快照：原函数对槽位只做 `SAVE%d.DAT` 文件名格式化，没有按槽位访问 UI 数组。调用前删除旧捕获，避免保存失败后误用旧文件。

每次使用唯一 run ID；宿主核对 completion、launcher 和内存快照的 run ID，防止沿用旧结果。报告需要全部步骤、请求的全部回合以及 launcher 的成功退出记录。Boxedwine 本机正常 shutdown 会返回 1，单独接受该返回码不足以判定成功。测试完成后如需终止自己的子进程，会标明 `terminated_after_capture`，不能当作正常退出功能的验收。

## 复现

先按仓库 README 建好 Wine 基准环境和原生工具。默认读取槽位 1；源存档需要是人类玩家可继续操作的四人对局。

```sh
cmake -S . -B build/native -DRICH4_SANITIZERS=ON
cmake --build build/native --parallel
python3 scripts/run_playability.py --target wine --slot 1 --turns 1 --timeout 180

# 当前预期返回 3，并写 unavailable 报告：
python3 scripts/run_playability.py --target native

# 单独检查已捕获存档；不能把此命令当作游戏运行：
build/native/rich4-state-check /path/to/before.DAT /path/to/after.DAT
```

报告位于 `artifacts/playability/<run-id>/report.json`，保留 build/Wine 日志、事件序列、存档、内存快照、可用的 guest 截图及原生读取结果。失败也保留已有证据。游戏资源和这些本机产物不进入 Git。

这里尚未固定或完整记录每次播种和 RNG 调用，因此重复执行会得到不同骰子、电脑选择和对话框；这是流程冒烟测试，还不是确定性回放。未知分支会如实失败并留下诊断，不能据有限的取消操作宣称全部玩法已验收。

## 2026-10-05 实测证据

最终默认配置报告：`artifacts/playability/20261005-130207-f5e8002b/report.json`。

| 检查 | 实际结果 |
| --- | --- |
| 真实菜单读取 SAVE1 | 通过，使用原菜单回调与槽位点击 |
| 连续 3 轮 | 玩家节点 55→54→21→48；每轮分别观察到掷骰动作、移动和其它玩家行动 |
| 日期与输入恢复 | 2026-10-05→06→07→08；每轮回到原玩家等待输入 |
| 原存档函数捕获 | 前后各 89,871 字节；725 个字节变化 |
| 原生存档回读 | 两份重新编码均逐字节相同；内存/存档 72 个字段全部一致 |
| 实际状态变化 | 4 位玩家节点、4 位玩家财务、1 位玩家道具、2 个土地记录变化 |
| 测试声音偏好 | 前后快照和完成报告 BGM=0；私有 CFG 的 PCM=4 保留 |
| 退出与原目录保护 | game_exit=0、clean_shutdown=true；无强制终止；源存档/配置/EXE 哈希不变 |
| 原生游戏门禁 | unavailable，退出码 3；没有原生主循环 |

本轮实际经过是/否窗口的取消 2 次。前一份三轮报告 `20261005-125559-33ceed45` 另实际经过乐透取消 2 次、是/否取消 1 次；当时尚未关闭私有 BGM，不能将它列为最终静音配置的验证。更早单轮 `20261005-124901-b7d178b4` 也通过，但不能取代连续运行。

Mac CTest 20 项通过；新设施包含 6 个 C++ 状态 CLI 的 Python 集成用例及 24 个宿主判定/配置回归用例。覆盖旧 nonce、缺步骤、崩溃、空比较、重复玩家/回合、错误状态字段、损坏存档、保护原输入，以及仅修改配置音乐 byte。完整原生主循环缺失时始终拒绝宣称可玩。

## 未覆盖范围

当前流程针对现有台湾地图存档，不是所有地图或整局通关测试；没有验收新开局、保存菜单交互、重载本次新存档、股票操作、主动买地、全部卡片道具、小游戏、破产、胜利、音频或真实过场。已有素材的 23 个 AVI 仍为占位文件。

早期带 `-nosound` 的单轮试跑通过，但一次三轮试跑在第二轮、普通游戏 callback 内停滞；该次已判失败。源码中的 `player_say → 0x4544f6` 确有等待 DirectSound 播放结束的无上限循环，支持音频时序排查方向；历史运行没有捕获 EIP，且后续骰子路径不同，不能把这次停滞的原因断言为已证实。最终配置保留音频时序；观察器新增每 5 秒心跳、停滞 15 秒的 guest 快照和 45 秒无进展失败，便于以后定位。有限轮数通过不代表长局稳定性已经验证。

将来实现原生主循环后，需要为同一组明确输入和播种事件添加原生驱动器，按节点比较状态与帧缓冲；不能把目前的存档回读或 Wine 成功标志直接复用为原生可玩性通过。
