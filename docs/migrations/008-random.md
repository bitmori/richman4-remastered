# 008 — 原版随机数流

已实现 `rich4::Random`，文件为 `include/rich4/random.hpp`、`src/random.cpp`、`tests/random_tests.cpp`。生成器没有平台调用、隐式全局状态或宿主 libc 依赖，每个对局可持有独立的流。

## 原版与重编译版的关键区别

原版 v3.11 EXE 的 `rand` 在 `0x456f2d`，`srand` 在 `0x456f50`，两者均为 cdecl。直接检查机器码得到 Watcom CRT 的规则：

```text
state = (state * 1103515245 + 12345) mod 2^32
result = (state >> 16) & 32767
```

上游 `asm/mscrt.ld` 将这两个函数替换为 MSVCRT。MSVCRT 的随机序列与原版不同，不能把重编译版的序列作为这一模块的验收标准；宿主 `rand()`、`std::mt19937` 同样不能替代。

原 CRT 默认 seed 为 1，`srand` 随后按原样写入所有 32 位，seed 0 也有效。原 state 位于 CRT 线程上下文的 `+0xc`，由 `0x456f23` 的 getter 取得，初始化后通过 TLS 分配。新接口要求调用者显式提供种子，避免偷偷从时钟或系统熵源重新播种。`state()` 可供测试和未来回放记录使用；当前没有改变旧存档格式。

所有算术采用 `uint32_t`，以标准规定的模运算保持 x86 的低 32 位结果；返回值明确为 15 位非负整数。

## 对照验证

独立适配器 `tools/oracle/original_random.h` 给映射的原 EXE 提供测试线程上下文，实际调用其 `srand`/`rand`，记录返回值和原函数写入的 state，不计算期望公式。

12 个种子各连续生成 1,024 次，合计 **12,288 个步骤，返回值与完整 32 位 state 均完全一致**。种子包含 0、1、`INT32_MAX`、`0x80000000`、`0xfffffffe` 和 `0xffffffff`。Mac ARM64 ASan/UBSan 构建通过：

```sh
build/native/random_tests .local/oracle/latest/golden/random.csv
```

单元测试另验证独立实例、复制状态继续生成和重新播种。

## 尚未迁移的部分

原游戏启动、读取存档等路径会通过时钟重新播种；目前只迁移生成器，没有迁移所有播种和消费位置。完整回合的行为一致性还需要记录输入、时钟/种子、随机数调用顺序以及每回合状态。生成器一致不等于骰子、AI 决策和事件顺序已经一致。
