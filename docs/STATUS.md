# 项目交接与暂停记录 — 2026-10-05

**状态：按用户要求暂停功能开发和测试，只完成本次文档收尾。当前产物是可运行对局的 Mac ARM64 原生原型，UI 复刻和整体体验尚未验收。** 下列后续事项只有在用户明确要求恢复后才继续。

## 用户反馈与当前结论

用户实际体验指出：原生版流畅很多，先前 Wine 版帧率偏低；但原生 UI 没有完整复刻，很多细节缺失，因此先暂停并整理进度。流畅度是用户反馈，尚无同场景 FPS/帧时间对照数据，不能据此给出量化提升。

之前开发和自动验证主要推进了规则、原生运行及操作闭环，使用了大量简化界面。**规则测试通过、按钮能操作、对局能推进，都不能替代原版 UI 和体验验收。** 不能把目前状态标为完整原生移植完成，也不能认为当前界面已达到供父亲日常使用的交付标准。

## 已有产物

| 部分 | 当前状态 | 边界 |
| --- | --- | --- |
| 原版与上游重编译版 | Boxedwine/Wine 下可运行，保留为参考 | 用户已确认该版本背景音乐和语音；这不是原生音频验收 |
| Mac 原生核心 | C++20，ARM64，独立于 Win32/x86/Wine/原 EXE | 原代码保存在 `.local/upstream` 供研究；测试 oracle 可调用原程序，游戏运行时不会调用 |
| 原生对局 | 回合、地产、银行/融资、股票、卡片道具、事件、商店/魔法、彩票、神明/NPC、交易、破产/胜负和存读档已接入 | 具体行为证据各异，不声称整局与原版完全等价 |
| 场景和文字 | SDL3，原始地图/人物/建筑等资源，FreeType，原 Big5 数据显示时转 UTF-8 | 使用原素材不等于完整画面复刻；文字不是原 GDI 栅格化 |
| 音频 | 原 WAV 音效/语音、MIDI 软件混音已接入 | 自动验证为 dummy 输出；真实 CoreAudio 设备检查在暂停时未执行，原生听感未确认 |
| Android | 早期九模块核心/资源测试曾在 ARM64 模拟器通过 | 新增完整游戏未在 Android 重验，没有可玩 APK |
| 过场/小游戏 | 当前 23 个 AVI 为相同占位文件；小游戏采用原版关闭模式 | 未实现三个互动小游戏及完整过场 |

## UI 缺口与恢复后的优先级

下列“已确认”来自当前源码和用户提供的原版画面；不把未经逐页检查的部分假定为已还原。入口主要是 `tools/native_game.cpp`、`tools/native_ui.hpp` 和 `src/board_renderer.cpp`。

| 优先级 | 已确认的当前实现/缺口 | 相关入口 |
| --- | --- | --- |
| 首先 | **小地图未实现**；右下区域被日期、点数和近期消息占用 | `Application::draw_board`；`board::Renderer` 仅绘制棋盘视口 |
| 首先 | **右侧资金/地产/股票/其他分页没有对应交互**；现在只有固定资金信息和简化财富列表 | `Screen`、`draw_board`、`draw` 的财富分支 |
| 首先 | **原图标工具栏被 11 个文字按钮替代**；图标、布局及按钮各状态未复刻 | `draw_board` |
| 随后 | 卡片/道具以名称和数量列表显示，未展示原图像；卡片按 ID 合并，未呈现原手牌顺序 | `draw_inventory` |
| 随后 | 股票为报价及买卖表格，未显示走势图，虽然核心保存行情历史 | `draw_stocks`、`World::stock_history_bits` |
| 随后 | 银行、商店、事件等主要使用通用纸色对话框；目标选择为编号标记/列表；结局为文字弹窗 | `draw_decision`、`draw_map_decision`、`draw_board` |
| 随后 | 开局只暴露单个人物和地图/版本选择；没有逐玩家控制方式、人数、初始资金/交通/租赁等完整设置界面 | `new_game`、`draw` 的新局分支；`NewGameOptions` |
| 随后 | 固定 Save1/Autosave 和命令行导入，没有多槽位存档浏览界面 | `save_path`、`load_path` |
| 对照后确定 | 字体粗细/间距/基线、提示和按钮状态、窗口过渡、完整特效、八地图整帧视觉一致性尚未建立证据 | `ui::Font`、`draw_board`、各界面及 renderer |

棋盘已经使用原始地形和精灵素材；不能把 UI 缺口误写成“所有画面都是占位图”。NPC/机器娃娃部分操作即时结算，动画与原版存在差异。独立模块像素比较也不等于整幅游戏画面一致。

恢复后先建立原版页面和交互状态清单，采集同一存档、同一状态、同一缩放下的原版/原生对照；优先恢复小地图、侧边分页、图标工具栏，再逐页恢复弹窗与内容。视觉检查应覆盖正常、悬停/按下/禁用、选择、取消、金额输入和返回路径。继续保留原生规则测试，但需增加独立的 UI 完整性和人工试玩验收，不能用跑更长回放代替。

## 测试证据的准确状态

原始报告只保存在本机忽略目录，不提交商业素材。路径均相对仓库根目录。

| 证据 | 结果 | 能说明什么 |
| --- | --- | --- |
| `artifacts/native-final/ctest.log` | 43/43 通过；Debug ASan/UBSan，代码里程碑 `7941820` | 模块、测试设施、原版金标的已列检查通过；后续 `7d77055` 仅扩展编排并通过 11 个编排测试 |
| `artifacts/native-acceptance/debug-asan-final-20261005/summary.json` | **整体 failed**，19/19 子场景各自通过 | 八地图分别自然开局 30 天；13/13 道具；合成胜利、恢复、交易和存读一致通过；主动卡片只有 25/26，缺 17，因此严格汇总失败 |
| `artifacts/native-acceptance/card17-probe-20261005/report.json` | 陷害卡 17 独立专项通过，3 天 | 该卡实际执行；不能与前一批拼成完整矩阵通过 |
| `artifacts/native-acceptance/debug-asan-complete-20261005/summary.json` | **interrupted**，`native_acceptance_verified=false` | 扩为 20 场后只完成地图 0–4 共 5 场；按用户要求终止，`interruption.partial=true`，后续场景未完成 |
| `artifacts/native-playability/final-release-map5-90days/report.json` | 通过 | Release 地图 5、seed 203 连续 90 天、307 次骰子、1,185 步；DAT/PRNG读回相同，原文件未改 |
| `artifacts/native-platform-smoke/final-release/platform.json` | 通过 | 从终端调用实际 `.command` 启动脚本，隐藏 Cocoa 窗口/Metal、3 天；不是桌面双击操作或 UI 忠实度验收 |
| `artifacts/native-final/audio.log` | dummy 混音检查通过 | 有非零 PCM、完成/替换/静音/循环/排空；不证明真实扬声器听感 |

19 场完整批次的原文件、合成输入、程序和脚本哈希检查均通过。被中断的 20 场不能冒充正常结束，也未重新启动。暂停后未进行 CoreAudio 新检查、功能修改或新的游戏测试。

## 已知规则和验证边界

- 原版会按时钟重新播种，装饰和语音时序也消耗随机数；原生确定性回放不等于原版整局随机轨迹。
- 部分区域选取的原渲染元数据过滤尚未完整差分；NPC/机器人和特效呈现仍有差异。详见 [012](migrations/012-native-turns.md)。
- 原 DAT 不存原生 PRNG，另用 `.rng` 保存；两份文件分别原子写入，不是跨文件事务。时间机器历史是会话内状态，读档后不重建。
- 各模块既有原 EXE 数值差分，也有源码推导和合成测试，证据强度不同；详见 [迁移索引](migrations/README.md) 与 [验收记录](migrations/validation.md)。
- 没有完成整体 UI/体验、原生实际听感、完整过场、互动小游戏和 Android 应用验收。

## 启动和数据位置

这些命令供用户自行运行或恢复工作后使用；暂停期间不自动执行。

- 原生原型：根目录 `Play Rich4 Native.command` 或 `./scripts/run_native_mac.sh`。实际 Release 程序在 `build/mac-native/rich4-native.app`；构建依赖本机 Homebrew SDL3、FreeType、FluidSynth 等，并非可直接分发的自包含应用。
- 原版参考：`Play Rich4.command` / `./scripts/run_mac.sh rich4.exe`。
- 原始资源：`~/Downloads/rich4-original-game`，只读使用。
- 原生存档：`~/Library/Application Support/Rich4Native/saves`，与 Wine 分开。
- Wine 副本/存档：`~/Library/Containers/org.boxedwine/Data/Library/Application Support/Boxedwine/Rich4/game/`。
- 测试数据：`artifacts/`、`.local/`；包含本机素材和存档，不纳入 Git。启动日志 `artifacts/native-runtime.log` 会被下次启动覆盖。

## 本地提交与交接

按用户此前指定直接提交本地 `main`，没有推送或创建 PR。暂停前功能/测试基线为 `7d77055`，之后只有本次文档收尾。主要近期提交：

| 提交 | 内容 |
| --- | --- |
| `4a3e8d3` | 回合生命周期、AI/NPC、债务和胜负存档 |
| `2b391cb` | 原版挂牌买卖和电脑交易 |
| `5a0c24a` | 交易串接、飞行炸楼、附属对象与金融修正 |
| `f089d6b` | 读档前完整世界字段校验 |
| `17e5c84` | 原生交易界面、操作恢复及回放误判修复 |
| `7941820` | 可移植 C++ 统一格式，提高规则可读性 |
| `8939d12`、`7d77055` | 隔离的验收编排、合成交易资料、卡片 17 专项 |

暂停时已终止测试专属进程，没有留下后台验收任务。未提交的代码功能改动为零；本次收尾仅更新状态、缺口、证据和文档导航。
