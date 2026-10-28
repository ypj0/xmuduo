# Timestamp

`Timestamp` 是微秒精度的轻量值类型，内部存储 `std::chrono::sys_time<std::chrono::microseconds>`。API 暂放在全局命名空间。

## 语义与接口

- 默认构造表示 Unix epoch，没有特殊无效值。
- 显式从 `TimePoint` 构造，通过 `timePoint()` 和 `timeSinceEpoch()` 与 chrono 互操作。
- `now()` 读取 `system_clock`，向下取整到微秒；微秒单位不保证底层时钟具有微秒分辨率。
- 支持比较，时间戳加减 `Duration`，以及两个时间戳相减得到 `Duration`。不使用裸整数或浮点数表达时间单位。
- `toUtcString()` 固定输出 `YYYY-MM-DDTHH:MM:SS.ffffffZ`，不受进程时区与 locale 影响。

```cpp
const Timestamp epoch;
const auto previous = epoch - std::chrono::microseconds{1};
// previous.toUtcString(): 1969-12-31T23:59:59.999999Z
```

## 使用示例

```cpp
#include "Timestamp.h"
#include <iostream>

using namespace std::chrono_literals;

const auto received = Timestamp::now();
const auto later = received + 500ms;
const auto difference = later - received;  // microseconds
std::cout << received.toUtcString() << '\n';
```

时区转换与当地时间格式化见 [TimeZone](timezone.md)，构建和测试方法见 [构建与测试](build.md)。

## 边界与异常

支持 UTC 年份 0001 到 9999，即 `[0001-01-01, 10000-01-01)`。构造或运算越界抛出 `std::out_of_range`，先检查边界再执行算术，避免有符号溢出。该区间任意两个时刻的微秒差都可表示。

这是基于 Unix 时间的时刻，不单独表达闰秒。系统校时可能让 `system_clock` 前跳或后退；超时与耗时测量应使用 `std::chrono::steady_clock`，不要与这里的时间戳混用。

格式化使用 C++20 日历拆分和局部缓冲区，不调用共享静态缓冲区的 `localtime` / `gmtime`。时区格式化由独立的 `formatTimestamp` 完成。

## 测试

`tests/Timestamp_test.cc` 覆盖 epoch、epoch 前一微秒、闰日、跨年、微秒格式、比较和运算、当前时间、日期上下界与极端 duration 的溢出拒绝。
