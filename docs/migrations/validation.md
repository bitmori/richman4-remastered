# 原生迁移验收记录 — 2026-10-05

**项目已按用户要求暂停。** Mac ARM64 原生 SDL3 原型可执行对局，运行时不依赖 Wine 或原 EXE；但用户确认 UI 未完整复刻、细节缺失，整体体验未通过验收。43/43 CTest、SDL 操作回放和 Cocoa/Metal 检查只证明各自范围。19 场批次汇总失败，20 场复验被中断，准确结果见文末及 [交接记录](../STATUS.md)。下方 Android 数据来自较早的九模块核心验证，不表示新增游戏模块已在 Android 重测；目前没有可玩的 APK。

## 环境

| 项目 | 实际环境 |
| --- | --- |
| Mac 核心 | AppleClang 21.0.0、C++20、Debug、AddressSanitizer + UndefinedBehaviorSanitizer |
| Mac 二进制 | `file build/native/rich4-tool` 为 `Mach-O 64-bit executable arm64` |
| 显示后端 | 本机 SDL3 3.4.16；窗口、最近邻缩放、资源/帧切换和退出实测 |
| Android 构建 | NDK 28.2.13676358 / Clang 19.0.1，Release，API 28，静态 libc++ |
| Android 执行 | 已有 `emulator-5554`，`arm64-v8a`，Android API 36 |
| Android 二进制 | ELF 64-bit ARM aarch64；动态依赖仅 `libm.so`、`libdl.so`、`libc.so` |
| 原版适配器 | MinGW GCC 16.2.0，32 位 PE，`-O0`，隔离 Boxedwine/Wine prefix |
| 原版 EXE SHA-256 | `5a90aee28ee5f7a5c3ba5cb935c9e55751a529c25fcd91208748a66293569550` |

Android 未启用 ASan/UBSan；该项检查在 Mac 上执行。没有把交叉编译成功替代为 Android 运行结果：测试可执行文件确实经 adb 上传并在模拟器内执行。

## 比较结果

| 验证 | Mac | Android ARM64 |
| --- | --- | --- |
| 9 个原生模块的合成测试 | 全通过，无 sanitizer 报告 | 全通过 |
| 清单完整性回归 | 22 个 Python 用例通过 | 在上传前由同一宿主检查器执行 |
| 原版 MKF 输出 | 2,673 项 / 296,005,111 字节完全相同 | 全部原生解码，拉回后逐字节相同 |
| RGB555→RGB565 | 65,536 个输入相同 | 全部相同 |
| 16 位 blit | 1,344 个画布相同 | 全部相同 |
| SPR 绘图 | 114 个画布相同 | 全部相同 |
| 游戏日历 | 74,146 个日期 + 64 个打包日期相同 | 全部相同 |
| 八方向 | 82,562 个输入相同 | 全部相同 |
| 卡片/道具纯库存 | 11,412 个案例返回值/状态相同 | 全部相同 |
| 地租/连锁店计数 | 4,705 个案例相同，含 32 位回绕 | 全部相同 |
| 地面 tile 绘图 | 18 张画布 / 5,529,600 像素相同 | 全部相同 |
| Watcom RNG | 12 种子 × 1,024 步，返回值与 state 相同 | 全部相同 |
| 两份真实存档 | 各 89,871 字节，读取/编码完全一致 | 两份读取/编码完全一致 |

早期资源核心里程碑 Mac CTest 合计 **20/20 通过**：9 个模块、清单回归、2 组玩法验证设施回归、原版清单前置检查、7 个原版金标测试。玩法设施的 Python 回归包含 6 个存档状态用例和 24 个宿主判定/配置用例。后续 Mac 游戏完整构建已扩展为 43 项，不能把早期 Android 覆盖范围同步扩大。

资源分类为 462 个压缩项、2,195 个非空未压缩项、16 个空项。Mac 另外对所有 SPR/SMP 实際展开：720 个图形资源 / 13,034 帧；8 张 GND 全部解析。这个全图像展开检查未单独在 Android 上逐帧重跑，Android 覆盖全资源原始解码和列出的图形案例。

## 本机证据

资源、存档、图片和原 EXE 都未加入 Git。以下路径是本机验证输出，重新运行会生成新的时间戳目录：

- `.local/oracle/latest` 指向有效捕获 `20261005-121504-959000`，包括原输入、适配器源码快照、解码结果和 31 个金标文件。
- 该 manifest 的 SHA-256：`3571f1f9b3d944a3bd189f114c2863f45b9a3017a39faf470810a24ecbbe3abb`。
- Mac 全资源对照：`artifacts/native-original-comparison.log`；具体报告 `.local/oracle/native-1791174377746223000/comparison.json`。
- 全图片解析：`artifacts/native-graphics-corpus.json`。
- Mac CTest：`build/native/Testing/Temporary/LastTest.log`。
- Android 全量：`artifacts/android-core/20261005-122529-7e9ac6a9/report.json`，同目录 `commands.log` 和 `decoded/` 保留执行记录及实际产物。
- Android 真实存档：`artifacts/android-core/20261005-122712-9cfd8dcf/report.json`。
- 地面人工查看用导出：`artifacts/taiwan-ground.ppm` / `.png`，仅为俯视源纹理，不用于替代逐像素机器对照。

上述原生程序均由本仓库 C++ 构建。原版适配器仅生成金标，不在原生核心、Mac 查看器或 Android 二进制运行路径中。

## 无窗口真实流程验证

`artifacts/playability/20261005-130207-f5e8002b/report.json` 记录最终无窗口配置下原版真实进程连续 3 轮通过：菜单读档、骰子按钮、玩家移动、电脑回合、回到玩家操作，节点 55→54→21→48，日期 2026-10-05→10-08。实际执行是/否窗口取消 2 次，最终正常退出，无强制终止。私有副本 BGM=0，PCM 音效设置保留为 4；此前另一份三轮记录另覆盖乐透取消。

前后两份存档各 89,871 字节，原生无损往返完全一致；与实时内存核对 72 个字段，全部匹配。4 名玩家节点、4 名玩家财务、2 个土地记录有变化。源目录存档/配置/EXE 哈希未改变。该流程没有使用 desktop computer use。方法、失败记录与局限见 [009](009-playability-hooks.md)。

以上是早期 Wine 基准版的流程与原生回读验证，采集时尚无原生主循环。这些报告本身不能证明原生游戏可玩；后续实际 SDL 回放单独记录在文末。

## 验证工具自身的纠错

初次 schema 1 捕获被全量比较判定失败：`-O2` 构建产物省略了压缩分支，产生的“原版结果”实际来自错误的原始字节复制。该批结果已作废；没有通过修改正确的 C++ 解压算法去迎合错误金标。

schema 2 适配器使用 `-O0`，运行前通过固定 ABC+结束标记向量自检、已知 SMP 资源签名自检；正式解压使用两种预填字节和尾部哨兵检查未写入/越界。构建命令、编译器和全部适配器来源都记录在 manifest 中。这里记录的是实际观察及规避方案，不把尚未最小化定位的优化问题笼统断言为编译器缺陷。

代码审查另外发现并修复：

- SPR 合成到带遮罩图片后，目标遮罩也必须更新；自身复制同时快照颜色与遮罩。
- 比较清单删掉条目后不能“空比较通过”。现在对照原始 MKF 索引校验完整集合、各字段、实际文件数量/尺寸/哈希，必需金标必须都在清单中。
- RNG 必须包含 12 个指定且互不重复的种子、每个恰好 1,024 步；少掉边界种子会失败。
- 解压器的声明长度截断是上游 C/新实现的安全边界，原 EXE 仅依赖结束标记。文档已明确区分。

## 证据的边界

函数级差分覆盖指定输入与状态，尚未证明整局事件顺序、AI、动画或输入时序一致。真实存档的无损编解码不等于完整运行时恢复；地面 tile 相同不等于完整相机、建筑和角色图层相同。

Big5 文本和音乐在 Wine 基准版已正常；原生版本现已具有 FreeType 文字、WAV 音效/语音和 MIDI 软件混音。原生音频已验证非零 PCM、替换、静音、播放列表与输出队列，但自动测试使用 dummy 音频，不当作实际听感确认。当前占位 AVI 无法用于完整过场验收。购入包含真实过场的正式版后，应先在单独目录做资源审计和版本识别，再建立新素材验收记录。

## Mac 原生运行与规则回放证据（非 UI 验收）

代码里程碑 `7941820`（之前的功能提交 `17e5c84`、存档校验 `f089d6b`、回合/飞行规则 `5a0c24a`）已完成 Debug 与 Release 原生构建。Release 为 `Mach-O 64-bit executable arm64`，链接 SDL3、FreeType、FluidSynth、iconv 与系统 C++ 库；没有 Wine、Win32 或原版 EXE 运行时依赖。开发工具中的原版调用器仍与游戏构建分离。

- `artifacts/native-final/ctest.log`：格式整理后完整 43/43 通过，Debug 开启 AddressSanitizer/UndefinedBehaviorSanitizer，包括七项原版金标比较与金标清单前置检查。
- `artifacts/native-final/audio.log`：原始资源生成 389,494 个非零音频样本，MIDI、音效、语音完成，队列排空和播放列表循环通过；输出设备为 dummy。
- `artifacts/native-platform-smoke/final-release/platform.json`：通过终端调用双击入口脚本 `Play Rich4 Native.command` 启动 Release 应用，隐藏 Cocoa 窗口 + Metal 渲染器，3 天/12 次骰子/38 步移动，存档与 PRNG 恢复一致。没有实际桌面双击/鼠标输入；音频仍为 dummy。
- `artifacts/native-playability/final-release-map5-90days/report.json`：地图 5、seed 203，Release 正常新局连续 90 天，307 次骰子、1,185 步移动，未实现事件为空，存档和随机状态读回相同，原文件哈希不变。

坏存档恢复覆盖截断和完整布局的非法角色编号；连续取消要求决策消失、世界/RNG不变，并且同批次仅取消按钮被执行。最终读档验证还要求成功加载计数增加，避免保留旧会话时误报成功。股票挂牌数量可直接输入，不限制为固定步进。

这些证据证明列出的原生游戏流程和回归范围，不是整局原版逐帧/随机轨迹完全等价证明。互动小游戏使用原版关闭模式；机器人/NPC部分动画即时结算；原版时钟重播种和部分装饰随机调用仍按模块记录处理。Android应用与完整过场尚未交付。

### 验收矩阵与暂停收尾

- `artifacts/native-acceptance/debug-asan-final-20261005/summary.json`：19/19 子场景通过，八地图各完成 30 天；主动卡片 25/26、道具 13/13，胜利、恢复、交易、存读档/PRNG与原文件完整性通过。缺卡片 17 执行覆盖，**整体结果仍为 failed**。
- `artifacts/native-acceptance/card17-probe-20261005/report.json`：卡片 17 独立专项实际执行并通过，不与旧批次拼接成完整验收。
- 编排提交 `7d77055` 增加第 20 场卡片 17 专项，11 个编排单测通过。`artifacts/native-acceptance/debug-asan-complete-20261005/summary.json` 的复验只完成地图 0–4 共五场，随后按用户要求终止：`status=interrupted`、`interruption.partial=true`、`native_acceptance_verified=false`。没有完整 20 场通过结果。

测试专属进程已退出，没有后台续跑任务。原计划的真实 CoreAudio 静音设备检查尚未执行，不能列为通过。暂停后只整理文档，没有继续游戏测试或功能开发。

用户对 UI 复刻的负面反馈是独立、尚未解决的验收问题；自动操作脚本不检查小地图、侧栏分类、图标工具栏、完整原版弹窗、动画节奏或整体视觉一致性。原生流畅度改善是用户实际反馈，尚未量化 FPS。后续需要单独建立 UI/体验验收，不应再用规则通过率代表产品完整度。
