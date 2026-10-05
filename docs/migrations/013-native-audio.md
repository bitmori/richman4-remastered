# 013 — 原始音效、语音和 MIDI 的原生播放

状态：Mac 原生应用可直接播放原始 EFFECT.MKF / SPEAKING.MKF 中的 WAV，及原始 MIDI 背景音乐，不需要 Wine、DirectSound、WinMM 或 CoreMIDI。MIDI 由 FluidSynth 软件合成，所有音乐、音效和角色语音混合后进入同一个 SDL3 音频流。

## 来源、依赖与边界

资源选择根据 `mytbk/rich4` 提交 `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`：

- `asm/rich4_sound_effect.asm` 的音效/语音加载、单路语音替换。
- `asm/rich4_media_music.asm` 的正常背景音乐列表和完成后切换。
- `asm/rich4.asm` 的 `_rich4_event_strings`（原地址 `0x48084a`）。
- `asm/rich4_player_say_on_events.asm` 的角色事件分类。

实现文件为 `tools/native_audio.hpp/.cpp`，采用 `GPL-3.0-or-later`。可移植游戏核心没有增加 SDL 或音频依赖；本模块属于原生应用适配层。

本机依赖为 SDL3 3.4.16、FluidSynth 2.6.1，均通过 `pkg-config` 发现。FluidSynth 动态库采用 LGPL-2.1-or-later，其授权文本在安装包 `LICENSE`，源码及授权见 [FluidSynth 官方仓库](https://github.com/FluidSynth/fluidsynth)。SDL 使用 zlib 授权，沿用项目已有依赖记录。本模块没有复制 TinySoundFont 或其它第三方合成器源码。

Mac 默认只读加载系统音色库：

```text
/System/Library/Components/CoreAudio.component/Contents/Resources/gs_instruments.dls
```

本机 FluidSynth 2.6.1 已实测可以解析此 DLS 并输出音乐样本。没有复制、提交或重新分发 Apple 音色库，也没有调用 Apple 的音频合成/输出 API。其它平台或自定义音色库可通过构造函数第三参数，或 `RICH4_SOUNDFONT` 指定兼容的 General MIDI SF2/DLS 文件。音色库缺失时保留 WAV 播放能力，`music_available()` 返回 false，原因由 `music_diagnostic()` 提供给应用日志。

## 音效与语音映射

本机原始资源包括 115 项 EFFECT WAV 和 1,374 项 SPEAKING WAV。资源仍经模块 001 的 MKF 解压器读取，SDL 解码 WAV 后转换为 48 kHz、双声道 float PCM。转换结果按资源编号缓存，原始档案字节不被改写。

| 事件 | EFFECT.MKF 编号 |
|---|---:|
| 菜单选中 | 1 |
| 控件操作 | 9 |
| 掷骰 | 10 |
| 步行 / 机车 / 汽车 / 工程车移动 | 44 / 45 / 46 / 53 |
| 特殊移动 | 47 |
| 买地 / 升级 / 付租 | 49 / 50 / 55 |

`play_sound(index)` 播放明确的原始资源，不自行推断事件。当前适配器播放一次性音效；持续移动的循环、停止及特殊事件的精确调用时机由后续应用事件适配继续迁移。

`play_voice(character, category)` 对应：

```text
SPEAKING.MKF[1050 + character * 27 + category]
character: 0..11
category:  0..26
```

角色顺序为约翰乔、沙隆巴斯、忍太郎、钱夫人、阿土伯、莎拉公主、宫本宝藏、糖糖、乌咪、孙小美、小丹尼、金贝贝。新语音替换旧语音，背景音乐和音效可同时继续。

分类包括 0–2 高兴、3–5 不利反应、6–8 收钱、9–11 花钱、12–14 幸运/建设收益、15 最高建筑等级、16 连片买地、17 连片升级、18 愤怒付款、19 入狱、20 入院、21 睡眠、24 胜利、25 破产、26 重试。分类的选择必须由游戏规则确定；音频模块**不调用随机数**。

尤其不能每次购买都自动播放“高兴”：原版普通买地通常仅有音效 49；拥有至少三块同名土地时才用语音 16。升级的语音 17 还有原版随机条件。收租/付租则按金额阈值及原版分支选择分类。这样可避免添加声音时改变游戏随机序列。

## MIDI 与设备输出

正常背景音乐依次为：`Rich08.mid`、`Rich16.mid`、`Rich17.mid`、`Rich18.mid`、`Rich19.mid`、`Rich20.mid`、`Rich21.mid`、`Rich22.mid`。初始化从第一首开始，播放完成进入下一首，最后回到第一首。`select_music(1..8)` 直接选择，参数 0 前进一首；`set_music(false)` 暂停音乐样本时钟，WAV 混音继续。

FluidSynth 设置 `player.timing-source=sample`，MIDI 时间由实际生成的 PCM 样本推进。应用每帧调用 `update()`，每块生成 512 帧，维持约 64 ms 的 SDL 队列。SDL 可以自行将混音格式转换成设备格式；应用无需处理平台音频句柄。

关键约束：实现从未调用 `new_fluid_audio_driver` 或打开 MIDI 输出端口。根据 [FluidSynth 音频驱动说明](https://www.fluidsynth.org/api/CreatingAudioDriver.html)，合成器本身不会输出到扬声器；本模块只调用 `fluid_synth_write_float`。之后唯一设备入口是 [SDL_OpenAudioDeviceStream](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream)。因此 `SDL_AUDIODRIVER=dummy` 可以覆盖音乐和 WAV 全部声音路径，保留真实播放推进，不会出现 Boxedwine 原生 CoreAudio MIDI 绕过 SDL dummy 的情况。

## 可复现验证

`tests/native_audio_tests.cpp` 在创建任何音频对象之前，用 `SDL_HINT_OVERRIDE` 强制 dummy 驱动。即使调用者环境选择硬件设备，测试也不会发出声音。不创建窗口或使用桌面自动化。

```sh
build/native/native_audio_tests ~/Downloads/rich4-original-game
# 非 Mac 或覆盖默认音色库：
build/native/native_audio_tests /path/to/original-assets /path/to/general-midi.sf2
```

无参数测试检查禁用音频的生命周期：不读取不存在的资源目录、不创建 SDL 设备，播放调用保持无操作。有原始资源时验证：

- 使用真实 `Rich08.mid` 合成非零 PCM；初始休止符结束后再判定能否发声，不能仅凭 `play()` 返回成功。
- 同时播放掷骰 WAV 和角色语音；第二条语音替换第一条。
- 检查两次语音启动仅完成最后一次，加上一次音效完成，共两项完成；完成依据实际混音样本消费。
- 停止补充队列后，dummy 设备将缓冲真正消耗到零。
- 关闭音乐时 MIDI 样本时钟不推进；正常曲目选择 8→0 回到第一首。
- 非法角色分类拒绝，禁用音频不访问资源。

macOS ARM64 上的 `-Wall -Wextra -Wpedantic -Wconversion`、AddressSanitizer、UndefinedBehaviorSanitizer 测试已通过。本次一轮记录：143,360 帧 MIDI、159,176 个非零 MIDI 样本；加入掷骰和语音后共 273,408 帧、387,446 个非零混音样本，SDL 驱动为 dummy，缓冲清空成功。实时队列测试的帧数可能因调度略有变化，判据是播放进展和内容而非固定时间点的计数相等。

FluidSynth 默认加载器在成功以 DLS 回退之前可能记录 `Not a SoundFont file`；原始 MIDI 的端口元数据 `0x21` 也可能被记录为忽略。测试仍必须检查实际非零样本，不能把这些日志单独当作成功或失败。

## 尚未声明一致的行为

- MIDI 音符、节奏与节目编号来自原文件；具体音色取决于所选音色库，不声称与旧 Windows 合成器的波形逐样本一致。
- 尚未实现银行/拍卖/月度等临时事件曲目保存并恢复正常曲目的播放位置；正常八首列表已经实现。
- 尚未迁移原配置五档音量对应的完整增益表。WAV 当前保持解码振幅，混合后限幅至 `[-1,1]`；MIDI 合成增益设置为 0.4。
- 原版每个 DirectSound 缓冲的循环/重复播放规则未全部恢复。适配器允许最多 16 路短音效，达到上限替换最早的音效，语音单独保持一路。
- 语音替换和音乐关闭作用于后续混音，已经排队的至多约 64 ms 音频会完成播放。长时间不调用 `update()` 会让音频暂停推进；应用的所有模态循环也必须持续更新。
- 完成计数表示样本已交给 SDL 队列；只有队列清空才说明 dummy/设备完成消耗。游戏回合规则不应依赖该诊断计数阻塞运行。

Apple DLS 只是当前 Mac 的默认资源选择。Android 的可分发音色库、依赖打包与设备音质验证仍需单独完成。
