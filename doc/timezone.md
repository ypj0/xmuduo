# TimeZone

`TimeZone` 是 `const std::chrono::time_zone*` 的不可变薄封装，与 `Timestamp` 分离，API 暂放在全局命名空间。它使用标准库的 IANA 时区数据库，不修改进程 `TZ`，不自行解析 TZif 文件，也不重复实现夏令时规则。

## 接口

- `TimeZone{"Asia/Shanghai"}`：按 IANA 名称查找；未知名称按标准库约定抛出 `std::runtime_error`。
- `TimeZone::utc()`：查找 UTC。
- `name()`：返回数据库规范名称，别名可能被解析，如 `UTC` 可能返回 `Etc/UTC`。
- `toLocal(timestamp)`：转换为 `std::chrono::local_time<microseconds>`。
- `toTimestamp(local, choice)`：把当地时间转换为确定时刻。
- `info(timestamp)`：返回标准库 `sys_info`，包括 UTC 偏移、缩写和规则生效区间。
- `formatTimestamp(timestamp, zone)`：输出 `YYYY-MM-DDTHH:MM:SS.ffffff±HH:MM:SS[IANA名称]`。保留偏移的秒字段，以支持历史时区规则；这是项目定义的格式，不是 RFC 3339。

当地时间本身没有时区，不能直接当作 Timestamp 使用。两个方向的转换均检查日期范围；当地时间和结果时刻都必须处于年份 0001 到 9999 内，越界抛出 `std::out_of_range`。

## 使用示例

```cpp
#include "Timestamp.h"
#include "TimeZone.h"
#include <iostream>

const auto received = Timestamp::now();
const TimeZone shanghai{"Asia/Shanghai"};
std::cout << formatTimestamp(received, shanghai) << '\n';

const auto local = shanghai.toLocal(received);
const auto restored = shanghai.toTimestamp(local);
// restored == received
```

时间戳接口见 [Timestamp](timestamp.md)，构建和测试方法见 [构建与测试](build.md)。

## 夏令时

当地时间可能唯一、不存在或重复。

- 不存在：例如纽约春季向前拨钟跳过的 `2024-03-10 02:30`。所有策略均抛出 `std::chrono::nonexistent_local_time`，不自动移动到切换点。
- 重复：例如纽约秋季的 `2024-11-03 01:30`。默认 `AmbiguousTime::reject` 抛出 `std::chrono::ambiguous_local_time`；也可明确传入 `earliest` 或 `latest`，选择较早或较晚的系统时刻。

```cpp
using namespace std::chrono;
const TimeZone ny{"America/New_York"};
const auto local = local_days{2024y/November/3} + 1h + 30min;
const auto first = ny.toTimestamp(local, TimeZone::AmbiguousTime::earliest);
const auto second = ny.toTimestamp(local, TimeZone::AmbiguousTime::latest);
// second - first == 1h
```

## 生命周期和运行要求

系统需要提供 IANA 时区数据库；初始化或查找失败的异常会向调用者传播。句柄不拥有数据库条目，使用期间不得通过 `get_tzdb_list().erase_after()` 删除相应数据库版本。组件本身不提供数据库重载或删除接口。

只读句柄可跨线程共享。未来和历史转换以安装的数据库及标准库实现为准，更新 tzdata 可能改变结果。当前环境的 libstdc++ 在纽约 1883 年以前的本地平太阳时偏移上观察到与 IANA 数据不一致，因此该极早期历史转换不作额外保证。

`tests/TimeZone_test.cc` 覆盖 UTC、跨日转换、非整点偏移、巴黎历史秒级偏移、夏令时切换、重复与不存在时间、无效名称、日期范围及并发访问。CTest 还以不同进程 `TZ` 重跑测试，验证显式时区行为。
