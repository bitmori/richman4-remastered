# 006：连锁店数量与普通土地通行费

状态：**已迁移两个不含外部副作用的整数计算函数；4705 个原版机器码案例全部匹配。** 不是整个经济系统迁移完成。

## 来源与入口

依据上游提交 `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`：

- `asm/rich4_player_core_actions.asm` 中 `_rich4_get_player_num_chain_store`，原版地址 `0x41970f`。
- 同文件 `_rich4_calculate_land_toll`，原版地址 `0x419744`。
- `asm/rich4_map.h` 的普通土地记录字段；记录大小为 52 字节。
- 用户原版 EXE 中 `strcmp` 地址 `0x458370`，确认只是自足的字节比较，不依赖 Win32、语言环境或 Unicode。

原函数读取全局土地指针、土地数量及物价指数。移植版把这些输入显式传入，没有内存地址、全局游戏状态、Win32 或 x87 依赖。

## API 与记录边界

实现位于 `include/rich4/economy.hpp` 和 `src/economy.cpp`：

```cpp
LandEconomyRecord decode_land_economy_record(std::span<const std::uint8_t> bytes);
std::int32_t chain_store_count(std::span<const LandEconomyRecord> lands,
                              std::int32_t owner_code);
std::int32_t land_toll(std::span<const LandEconomyRecord> lands,
                      std::int32_t owner_code,
                      std::int32_t price_index,
                      std::optional<std::string_view> street = std::nullopt);
```

`LandEconomyRecord` 只包括本次函数确实读取的字段，不把未知字段起成推测性名字：

| 原记录偏移 | 类型 | 新字段 |
| --- | --- | --- |
| `+0x04` | 16 字节以内、NUL 终结的名字 | `street_name`，原始编码字节 |
| `+0x18` | u8 | `chain_store`，任意非零都算连锁店 |
| `+0x19` | u8 | `owner_code`，原版玩家槽 + 1；0 表示无主 |
| `+0x1a` | u8 | `level` |
| `+0x20` | 6 个小端 u16 | `toll_by_level[0..5]` |

其余原记录字节不属于这个视图。`decode_land_economy_record` 是读取视图，不是土地保存器；需要无损保存时仍使用存档模块保留完整原始记录。

传入的土地 span **只包含实际土地**，不包含原版索引 0 的哨兵记录。原版循环是从 1 到 `num_lands`，迁移的调用边界应相应地去掉哨兵，而不是把原版整块数组直接传入。

`owner_code` 已经是原版字段值，不会再次加 1。函数保持原版比较方式，所以传入 0 可以计算无主记录，255 可以匹配 owner 字节 255；-1 或 256 不会匹配任何记录。

## 已保留的计算规则

`chain_store_count` 只检查两件事：`chain_store != 0`，以及 `owner_code` 完全匹配。它不读取地名、等级、过期日期或房价，也不擅自把 chain 标志限制成 1。

`land_toll` 有两条原版分支：

1. `street == nullopt`：每一间该业主的连锁店贡献 2000，忽略街道及等级，最后整体乘物价指数。
2. `street` 有值：只计算该业主的普通土地，名字按原始字节完全匹配，取每块地当前等级对应的 u16 通行费，加总后整体乘物价指数。

这里的「同名」必须保持原版 `strcmp` 语义：Big5 字节按字节比较、大小写敏感、不是前缀匹配；NUL 结束字符串。空名字和 `nullopt` 是不同分支。C++ `std::string` 在本模块只是原始字节容器，不意味着已将 Big5 改成 UTF-8；界面文字转换应发生在展示边界，不能悄悄改变游戏内的街道分组规则。

### 明确的 32 位整数行为

原版使用 `add` 和 `imul`，只留下结果的低 32 位，然后将 EAX 解释为有符号整数。直接写成 C++ 有符号 `total * price_index` 会在溢出时产生未定义行为。

迁移版使用 `uint32_t` 累加及乘法实现模 `2^32` 运算，最终用 `bit_cast<int32_t>` 解释结果。这里保留的是**文件/游戏数值规则的固定宽度**，不是 32 位主机或指针依赖。

测试包括：

- `65535 × 65537` 得到 `-1`。
- 65538 块地、每块通行费 65535，加总环绕为 65534。
- `2000 × INT32_MAX` 得到 `-2000`。
- 负物价指数和 `INT32_MIN` 不触发 C++ 有符号溢出。

## 非法输入边界

记录读取器要求恰好 52 字节，并要求原 16 字节名字区域中存在 NUL，拒绝原版可能跨记录继续读取的坏输入。

普通土地在**真正匹配当前业主和街道**时，等级必须在 0..5；否则抛出 `std::invalid_argument`。未被选中的记录或只进入连锁店分支时不读取等级，因此不因为无关记录的等级字节而拒绝合法计算。记录读取器本身保留原 level 字节，避免在无关分支里过早推断其含义。

这些函数没有状态写入或余额扣款。异常不会留下部分游戏状态修改。

## 验证与证据

`tests/economy_tests.cpp` 使用合成记录验证小端读取、Big5 名称、原始 owner 值、非零 chain 标志、全部等级、null/空串区别、前缀和大小写、内嵌 NUL、非法输入以及整数溢出。

`tools/oracle/original_economy.h` 是独立测试适配器。在隔离的原版 EXE 进程中配置合成土地数组，直接调用 `0x41970f` 和 `0x419744`，记录结果后恢复原全局值。原版磁盘文件及用户正在运行的游戏均不被修改。

4705 个差分案例包括：

- 0、1、6、24 条真实记录；索引 0 哨兵故意设置成可匹配的连锁店，确认原版跳过它。
- 业主 -1、0、1..4、255、256；物价 0、1、2、-1、`INT32_MAX`、`INT32_MIN`、65537。
- 无街道、空名字、不同 Big5 名称、Big5 前缀、ASCII 大小写；等级 0..5 和 chain 标志 255。
- 一条加法本身溢出的案例：原版实际处理 65538 条记录；向量文件以单条重复记录压缩表示输入，不简化原版执行。

2026-10-05 已完成实际捕获和原生运行：**4705 案例的连锁店数量和通行费均与原版结果一致**。原生程序以 Clang C++20、严格警告、AddressSanitizer 和 UndefinedBehaviorSanitizer 编译执行，没有诊断。来源与向量校验位于 `.local/oracle/latest/manifest.json`；`golden/economy.txt` 为本地产物，不加入 Git。

独立复现：

```sh
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Iinclude src/economy.cpp tests/economy_tests.cpp -o .local/economy-tests
.local/economy-tests .local/oracle/latest/golden/economy.txt
```

## 尚未迁移

实际付款、卡片/神明对通行费的后续修正、贷款/股票/破产、地产买卖、商业设施和总资产计算不在本次范围。尤其没有迁移使用 x87 的财富总计；尚未建立浮点舍入和溢出行为的原版对照，不能用普通浮点表达式替换后就声称一致。
