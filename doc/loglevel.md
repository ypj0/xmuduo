# LogLevel

声明位于 `include/LogLevel.h`，使用全局命名空间。

`enum class LogLevel` 按严重程度排列：`trace`、`debug`、`info`、`warn`、`error`、`fatal`；最后的 `off` 表示关闭输出，不是消息等级。

- `LogLevelName(level)`：返回大写等级名的 `std::string_view`。未识别的枚举值返回 `UNKNOWN`。
- `isLogEnable(messageLevel, threshold)`：仅当消息等级与阈值都不是 `off`，且消息等级不低于阈值时返回 `true`。调用者应传入合法枚举值。

```cpp
#include "LogLevel.h"

static_assert(!isLogEnable(LogLevel::debug, LogLevel::info));
static_assert(isLogEnable(LogLevel::error, LogLevel::info));
static_assert(!isLogEnable(LogLevel::error, LogLevel::off));
```

函数均为 `constexpr`、`noexcept`，不持有全局配置。运行时阈值及致命日志的终止行为将由后续 Logger 实现；这里的过滤函数本身没有终止副作用。

`tests/LogLevel_test.cc` 在编译期和运行期检查全部 49 个消息等级与阈值组合，并检查名称映射及未知等级名称。
