# 004 — 游戏日历与八方向计算

状态：已将这两个独立汇编模块转换为可读的 C++20 纯函数；单元测试和 AddressSanitizer/UndefinedBehaviorSanitizer 通过。原版机器码逐项对照使用 `tools/oracle/original_logic.h` 生成的 CSV，验收结果另记于本文末尾。

## 来源与函数映射

上游 `mytbk/rich4` 提交 `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e` 的 `asm/rich4_date_utils.asm`、`asm/rich4_calculate_direction.asm`。派生代码沿用仓库的 `GPL-3.0-or-later`，原素材和 EXE 均不分发。

| 原版地址 | C++ 接口 | 行为 |
|---|---|---|
| `0x451f8c` | `calendar::days_from_1998` | 自 1998-01-01 起的天数，第一天为 0 |
| `0x45201f` | `calendar::from_days` | 天数转换回具名日期 |
| `0x4520a6` | `calendar::weekday` / `days_in_month` | 星期与当月天数，星期日为 0 |
| `0x452117` | `calendar::next_day` | 下一天和是否跨月，不再通过原始指针修改调用方结构 |
| `0x45218f` | `calendar::add_days` | 以日为单位增减 |
| `0x4521aa` | `calendar::days_between` | 第二个日期减第一个日期 |
| `0x4521cb` | `calendar::add_packed_interval` | 旧事件到期日期所用的打包整数加法 |
| `0x454fb4` | `direction::from_delta` | 屏幕坐标差转换为八方向编号 |

文件：`include/rich4/calendar.hpp`、`src/calendar.cpp`、`tests/calendar_tests.cpp`；方向对应 `direction.hpp`、`direction.cpp`、`direction_tests.cpp`。

## 日历兼容规则

原游戏只按 `year % 4 == 0` 判闰年。因此 **2100 年仍有 2 月 29 日**，2200 年亦同。不能换成标准库的公历日期算法，否则长期存档日期、星期、计息和事件到期都会改变。

`Date` 的字段为 `uint16_t year`、`uint8_t month`、`uint8_t day`；序列化通过 `pack`/`unpack` 明确执行 `year << 16 | month << 8 | day`。不依赖结构体内存布局、指针宽度或宿主字节序。星期仍由 `(days + 4) % 7` 得到，1998-01-01 为星期四。

累计年天数使用完整年数和四年一闰的整除计数；反向转换先跳过 1461 天的四年周期，再逐年、逐月处理余数。此变化仅减少遍历次数，不改变日期结果。

`add_packed_interval` 刻意与正常日期加法分开。它执行模 `2^32` 的打包整数相加；当 interval 的月份字节非零，且结果月份大于 12 时，只加一次 `0xf400`。例如 1 月 31 日加 `0x100` 保留成 2 月 31 日；加 `1` 得到 1 月 32 日。它不会偷偷夹紧日期或采用现代日历的月末规则。

有效日历操作的输入域明确为 1998-01-01 至 65535-12-31。非法月日、早于基准日期、计算超出可表示年范围时抛出异常；`pack`/`unpack` 与旧式打包加法保留任意位值。原汇编在非法月份可越界读取、在早于基准或超出 16 位年份时产生不可靠日期，新版不复制这些非法输入行为。`add_days` 使用 64 位中间值，避免有符号溢出。

本模块不读取系统时间。原 `rich4_getdate.c` 的 `GetLocalTime` 平台接口不属于这次迁移；游戏启动日期将由以后实现的平台层提供。

## 方向兼容规则

输入仍是原来的 `int32_t dx, dy`，输出为 0–7 的游戏方向编号。零向量保持返回 **2**，编号顺序保持原表 `[2,3,4,5,6,7,0,1]`。例如正 x 为 2、负 x 为 6、正 y 为 0、负 y 为 4。

没有采用浮点 `atan2`。原汇编先计算 `floor(min(|x|,|y|) * 2048 / max(|x|,|y|))`，再查 2049 项角度表、按象限转换、取最终八方向。最终接口只观察八方向，因此可使用原表的精确整数分界：

- `table[848] = 1023`
- `table[849] = 1024`，恰为 22.5° 的比较边界
- `table[850] = 1025`

表单调，边界值仅在下标 849 出现。保留比 849 小、相等、大三种结果，以及原版象限转换与 16 位角度回绕，就能保留所有最终方向。等号不能并入任何一侧：原版正、负角度的取整在该处存在方向相关的差异。该化简只用于八方向接口，不声称能返回原版完整角度值。

整数斜率乘法使用 `uint64_t`，原版双寄存器除法不会截掉乘积高位。负号与绝对值使用无符号模运算，避免 C++ 的 `-INT32_MIN` 未定义行为；特别保留原版 `NEG dy` 在 `dy == INT32_MIN` 时仍是负数的行为。整个 `int32_t × int32_t` 输入域都有定义，不需要以手机坐标范围为借口跳过极值。

## 验证

不依赖游戏素材的单元测试覆盖闰日、2100 年旧式闰年、跨月跨年、负偏移、打包进位、不规范打包日期保留、上下界与溢出拒绝；另检查前 75,000 天及全支持日期域的采样往返。方向覆盖轴线、对角线、849 分界两侧及等号、零向量、正负整数极值和 257×257 坐标网格的比例缩放性质。

独立原版测试适配器直接调用上述 EXE 函数，生成：

- `golden/calendar.csv`：1998-01-01 至 2200-12-31 的每一天，记录天数、日期、星期、月天数、下一日、跨月标志、±400 日和日期差。
- `golden/calendar_packed.csv`：8 个打包输入与 8 个打包间隔的组合，包含不规范位值和回绕。
- `golden/direction.csv`：257×257 坐标网格、全部 2049 个整数斜率在 8 个象限/坐标次序中的结果，以及 `INT32_MIN/MAX` 等边缘值。

原版金标与实现相互独立：金标直接运行原 EXE，C++ 实现既不调用原 EXE，也不加载该 CSV。单元测试与金标比较入口：

```sh
build/native/calendar_tests .local/oracle/latest/golden/calendar.csv \
  .local/oracle/latest/golden/calendar_packed.csv
build/native/direction_tests .local/oracle/latest/golden/direction.csv
```

单元测试与严格警告、ASan、UBSan 已通过。原版 EXE oracle v2 的 CSV 逐项验收结果：74,146 个逐日记录、64 个打包日期记录、82,562 个方向记录全部一致，0 差异。输入与金标文件 SHA-256 保存于本地 `.local/oracle/latest/manifest.json`，可重新运行 `scripts/run_asset_oracle.py` 复现。

这两个模块的函数结果一致，仍不能推导出完整游戏回合一致。后续接入计时、地图移动和存档时，仍须验证调用顺序和状态更新。
