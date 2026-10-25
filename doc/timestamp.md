# `Timestamp` 类设计文档

## 1. 设计目的

`xmuduo::Timestamp` 表示一个 Unix 时间戳：从 Unix epoch（`1970-01-01 00:00:00 UTC`）起经过的微秒数。它是轻量值类型，内部只保存一个 `int64_t`，适合按值传递和复制。

当前接口提供两种操作：`now()` 获取当前系统时间；`toString()` 将时间点格式化为 UTC 日期和时间。该类表示日历时间，不用于计算可靠的经过时长或实现超时。

## 2. 数据表示

```cpp
class Timestamp {
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpoch);
    static Timestamp now();
    std::string toString() const;

private:
    int64_t microSecondsSinceEpoch_;
};
```

`microSecondsSinceEpoch_` 是从 Unix epoch 开始累计的微秒数。默认构造将其设为 `0`，表示 Unix epoch 本身；带参构造允许从已有微秒时间值创建对象。带参构造使用 `explicit`，避免整数在不经意间隐式转换为 `Timestamp`。

## 3. 获取当前时间：`now()`

`now()` 使用 `std::chrono::system_clock::now()` 读取系统日历时钟，然后将时间自 epoch 起的 duration 转成微秒：

```cpp
const auto now = std::chrono::system_clock::now().time_since_epoch();
const auto microsecondsSinceEpoch =
    std::chrono::duration_cast<std::chrono::microseconds>(now).count();
return Timestamp(microsecondsSinceEpoch);
```

`duration_cast` 在这里以微秒为单位表达时间，不意味着操作系统时钟具有微秒级的实际精度。

`system_clock` 会受系统校时影响，例如 NTP 校时或管理员修改时间可能使时钟向前或向后跳。因此它适合生成日志时间和现实日期，不适合用两个 `Timestamp` 差值来保证定时器或耗时测量正确。那些用途应使用 `std::chrono::steady_clock`。

## 4. 格式化：`toString()`

输出格式为：

```text
YYYY-MM-DD HH:MM:SS.ffffff UTC
```

例如：

```text
2026-10-25 14:02:31.365338 UTC
```

实现使用 C++20 `<chrono>` 的 `sys_time`、`floor<days>`、`year_month_day` 和 `hh_mm_ss` 将 epoch 时间转换为日历字段。`sys_time` 基于 Unix epoch，格式明确表示 UTC，不依赖进程本地时区，也不需要调用共享静态缓冲区的时间转换函数。

微秒部分固定输出六位。格式化使用局部字符缓冲区和 `std::snprintf`，缓冲区大小检查失败时返回空字符串。由于 `Timestamp` 内部可由任意 `int64_t` 构造，极端超出日历格式化范围的值不应作为常规有效时间使用。

## 5. 与 Logger 的关系

Logger 在输出每条日志时调用 `Timestamp::now().toString()`，并将其放在等级和正文之前：

```text
[2026-10-25 14:02:31.365338 UTC] [INFO] server started
```

时间戳在 Logger 的输出互斥锁内获取，使日志时间的取得顺序与日志提交到标准输出的顺序一致。

## 6. 当前边界

- 精度单位为微秒，但底层系统时钟不一定提供微秒精度。
- 输出时区固定为 UTC；当前没有本地时间格式化接口。
- 没有时区偏移、闰秒或自定义格式支持。
- `now()` 使用可调整的墙上时钟；定时器和持续时间计算应另用单调时钟。
- 默认值为 Unix epoch；当前类没有 `valid()` 或单独的无效值语义。

声明位于 [`include/timestamp.h`](../include/timestamp.h)，实现位于 [`src/timestamp.cc`](../src/timestamp.cc)。项目通过 CMake 使用 C++20。
